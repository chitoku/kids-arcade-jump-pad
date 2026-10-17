#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <math.h>
#include <Wire.h>
#include <Adafruit_NAU7802.h>
#include "AdcButton.h"
#include "SerialLines.h"
#include "Config.h"
#include "Detector.h"
#include "CommandLine.h"
#include "HidOutput.h"
#include "PeakWeight.h"


class SerialConsole : public Print {
 public:
  SerialLines queue;
  size_t write(uint8_t c) override { queue.push(char(c)); return 1; }
  using Print::write;
  void pump() {
    if (!Serial) { queue.clear(); return; }
    const int space = Serial.availableForWrite();
    size_t bytes = queue.remaining();
    if (space <= 0 || !bytes) return;
    if (bytes > size_t(space)) bytes = size_t(space);
    if (bytes > 64) bytes = 64;
    queue.consume(Serial.write(reinterpret_cast<const uint8_t*>(queue.data()), bytes));
  }
} console;

HidOutput hidOutput;
static_assert(ARDUINO_USB_MODE == 0, "Native TinyUSB mode required for future CDC + HID");
bool streamEnabled = true;
bool editing = false;
M5Canvas lcd(&M5.Display);
bool lcdReady = false;
Preferences prefs;
Adafruit_NAU7802 nau;
AdcButton adcButton;
bool useNau = false, adcReady = false;
const char* adcInitStage = "not_started";
int nauBusStatus = -1;
uint32_t lastNauInitAttempt = 0;
unsigned nauInitRetries = 0;
const char* adcName() { return useNau ? "NAU7802" : "HX711"; }
const char* scaleKey() { return useNau ? "scale-nau" : "scale-hx"; }
Detector detector;
struct SavedDetectorTuning { uint32_t version; DetectorTuning values; };
constexpr uint32_t tuningVersion = 1;
bool tuningDirty = false;
PeakWeight peakWeight;
portMUX_TYPE hxMux = portMUX_INITIALIZER_UNLOCKED;
int32_t raw = 0;
float zero = 0, filtered = 0, scale = 0;
bool zeroValid = false, fault = true, taring = true;
uint32_t lastSample = 0, lastDisplay = 0, tareStart = 0, lastStatus = 0;
uint32_t feedbackAt = 0, sampleCount = 0, rateStart = 0;
float sps = 0;
int64_t tareSum = 0;
unsigned tareCount = 0;
const char* feedback = "";

int readNauControl() {
  Wire.beginTransmission(NAU7802_I2CADDR_DEFAULT);
  Wire.write(NAU7802_CTRL2);
  if (Wire.endTransmission(false) != 0 ||
      Wire.requestFrom(uint8_t(NAU7802_I2CADDR_DEFAULT), uint8_t(1)) != 1)
    return -1;
  return Wire.read();
}
bool calibrateNauInternal() {
  // Wait for CALS to clear, with a deadline. The library's calibrate()
  // polls the opposite condition and can hang when the device is absent.
  const int control = readNauControl();
  if (control < 0) return false;
  Wire.beginTransmission(NAU7802_I2CADDR_DEFAULT);
  Wire.write(NAU7802_CTRL2);
  Wire.write(uint8_t((control & ~0x07) | 0x04));
  if (Wire.endTransmission() != 0) return false;
  const uint32_t started = millis();
  while (millis() - started < 1000) {
    const int status = readNauControl();
    if (status < 0) return false;
    if (!(status & 0x04)) return !(status & 0x08);
    delay(2);
  }
  return false;
}

bool initNau() {
  adcInitStage = "i2c_bus";
  // Reclaim controller 0 after M5 initialization before choosing external pins.
  Wire.end();
  M5.Ex_I2C.release();
  if (!Wire.begin(Config::nauSda, Config::nauScl)) return false;
  Wire.setClock(100000); Wire.setTimeOut(50);
  adcInitStage = "i2c_address_0x2A";
  for (unsigned attempt = 0; attempt < 3; ++attempt) {
    delay(20);
    Wire.beginTransmission(NAU7802_I2CADDR_DEFAULT);
    nauBusStatus = Wire.endTransmission();
    if (nauBusStatus == 0) break;
  }
  if (nauBusStatus != 0) return false;
  adcInitStage = "device_begin";
  if (!nau.begin(&Wire)) return false;
  adcInitStage = "ldo_3V0";
  if (!nau.setLDO(NAU7802_3V0)) return false;
  adcInitStage = "gain_128";
  if (!nau.setGain(NAU7802_GAIN_128)) return false;
  adcInitStage = "channel_A";
  if (!nau.setChannel(0)) return false; // Library: 0 = A, 1 = B.
  adcInitStage = "rate_80";
  if (!nau.setRate(NAU7802_RATE_80SPS)) return false;
  delay(250); // Settle after changing LDO/gain/rate before internal calibration.
  adcInitStage = "internal_calibration";
  if (!calibrateNauInternal()) return false;
  adcInitStage = "ready";
  return true;
}

// No wait-for-ready loop. Protect short clock pulses from interrupt stretching
// (PD_SCK high >60 us powers the HX711 down). Channel A, gain 128: 25 pulses.
bool readSensor(int32_t& value) {
  if (!adcReady) return false;
  if (useNau) {
    if (!nau.available()) return false;
    value = nau.read();
    return true;
  }
  if (digitalRead(Config::dt)) return false;
  uint32_t bits = 0;
  portENTER_CRITICAL(&hxMux);
  for (unsigned i = 0; i < 25; ++i) {
    digitalWrite(Config::sck, HIGH);
    delayMicroseconds(1);
    if (i < 24) bits = (bits << 1) | digitalRead(Config::dt);
    digitalWrite(Config::sck, LOW);
    delayMicroseconds(1);
  }
  portEXIT_CRITICAL(&hxMux);
  value = (bits & 0x800000) ? static_cast<int32_t>(bits | 0xff000000) : bits;
  return true;
}
void resetPeak() {
  peakWeight.reset();
  if (!editing) console.println("STATUS,MAX_RESET");
}
void printTuning() {
  const auto& t = detector.tuning;
  console.printf("STATUS,TUNE,enter=%.2f,leave=%.2f,air=%.2f,land=%.2f,standing_ms=%u,air_ms=%u,landing_ms=%u,dirty=%u\n",
                t.enterKg, t.leaveKg, t.airKg, t.landKg,
                t.standingMs, t.airConfirmMs, t.landingMs, tuningDirty);
}
void applyTuning(const DetectorTuning& next) {
  detector.setTuning(next);
  hidOutput.release(); // A changed threshold never keeps a stale Space press.
  tuningDirty = true;
  printTuning();
}
void startTare(uint32_t now) {
  taring = true; zeroValid = false; tareSum = 0; tareCount = 0; tareStart = now;
  detector.reset(); hidOutput.release();
  if (!editing) console.println("STATUS,TARE,keep_pad_empty");
}
// Fan out to HID even when the interactive Serial console is quiet.
void emitEvent(Event event, uint32_t now) {
  if (event == Event::NONE) return;
  hidOutput.event(event, now);
  feedback = event == Event::JUMP ? "JUMP!" : "LAND";
  feedbackAt = now;
  if (!editing) console.printf("EVENT,%s,%lu\n", event == Event::JUMP ? "JUMP" : "LAND", (unsigned long)now);
}
void commands(uint32_t now) {
  static CommandLine input;
  // First keystroke enters quiet mode. Sensor/LCD continue running.
  for (unsigned budget = 0; budget < 64 && Serial.available(); ++budget) {
    char c = Serial.read();
    if (!editing && c >= 32 && c <= 126) {
      streamEnabled = false; editing = true;
      console.print("\r\nSTATUS,STREAM,OFF\r\n> ");
    }
    const size_t oldUsed = input.used;
    if (!input.push(c)) {
      if (editing && (c == '\b' || c == 127) && oldUsed > input.used) console.print("\b \b");
      else if (editing && input.used > oldUsed) console.write(c);
      continue;
    }
    if (editing) console.println();
    editing = false;
    char* line = input.text;
    if (input.overflow) console.println("ERROR,COMMAND_TOO_LONG");
    else if (!*line) console.println("STATUS,HELP,tare | cal <known_kg> | status | stream on/off | hid on/off | max reset | tune show/set/reset/save");
    else {
      console.printf("ACK,%s\n", line);
      if (!strcmp(line, "stream on")) { streamEnabled = true; console.println("STATUS,STREAM,ON"); }
      else if (!strcmp(line, "stream off")) { streamEnabled = false; console.println("STATUS,STREAM,OFF"); }
      else if (!strcmp(line, "hid off")) { hidOutput.key.enabled = false; hidOutput.release(); console.println("STATUS,HID,OFF"); }
      else if (!strcmp(line, "hid on")) {
        hidOutput.key.enabled = JUMP_PAD_HID; hidOutput.release();
        console.println(JUMP_PAD_HID ? "STATUS,HID,ON" : "ERROR,HID_NOT_COMPILED");
      }
      else if (!strcmp(line, "max reset")) resetPeak();
      else if (!strcmp(line, "adc scan")) {
        if (!useNau) console.println("ERROR,select_NAU7802_first");
        else {
          unsigned found = 0;
          for (uint8_t address = 8; address < 120; ++address) {
            Wire.beginTransmission(address);
            if (Wire.endTransmission() == 0) {
              console.printf("I2C_FOUND,0x%02X\n", address); ++found;
            }
          }
          console.printf("I2C_SCAN_DONE,found=%u,sda=G%d,scl=G%d\n", found, Config::nauSda, Config::nauScl);
        }
      }
      else if (!strcmp(line, "tune show")) printTuning();
      else if (!strcmp(line, "tune reset")) applyTuning(DetectorTuning{});
      else if (!strcmp(line, "tune save")) {
        const SavedDetectorTuning saved = { tuningVersion, detector.tuning };
        if (prefs.putBytes("tuning-v1", &saved, sizeof(saved)) == sizeof(saved)) {
          tuningDirty = false; printTuning();
        } else console.println("ERROR,TUNE_SAVE_FAILED");
      }
      else if (!strncmp(line, "tune set ", 9)) {
        DetectorTuning next;
        if (parseDetectorTuning(line, next)) applyTuning(next);
        else console.println("ERROR,TUNE,use tune set <enter_kg> <leave_kg> <air_kg> <land_kg> <standing_ms> <air_ms> <landing_ms>");
      }
      else if (!strcmp(line, "status")) {
        console.printf("STATUS,MAX,kg=%.3f,valid=%u,clipped=%u\n",
                      peakWeight.kg, peakWeight.valid, peakWeight.clipped);
        console.printf("STATUS,HID,compiled=%u,enabled=%u,connected=%u,space_requested=%u\n",
                      unsigned(JUMP_PAD_HID), hidOutput.key.enabled, hidOutput.connected(), hidOutput.key.down);
        console.printf("STATUS,SENSOR,%s,taring=%u,zero_valid=%u,raw=%ld,filtered=%.2f,sps=%.1f,counts_per_kg=%.6f\n",
                      fault ? "ERROR" : "OK", taring, zeroValid, (long)raw, filtered, sps, scale);
        console.printf("STATUS,ADC,%s,initialized=%u,stage=%s,i2c_status=%d,sda=%d,scl=%d\n",
                      adcName(), adcReady, adcInitStage, nauBusStatus,
                      useNau ? digitalRead(Config::nauSda) : -1,
                      useNau ? digitalRead(Config::nauScl) : -1);
        printTuning();
      }
      else if (!strcmp(line, "tare")) startTare(now);
    else if (!strncmp(line, "cal ", 4)) {
      char* end; float kg = strtof(line + 4, &end);
      float candidate = kg > 0 ? filtered / kg : 0;
      if (*end || !isfinite(kg) || kg <= 0 || !isfinite(candidate) || fabsf(candidate) < 0.001f || !zeroValid || taring || fault || now - lastSample > Config::sensorTimeoutMs || fabsf(filtered) < 100)
        console.println("ERROR,CAL,use_known_kg_after_tare_and_stable_load");
      else {
        if (prefs.putFloat(scaleKey(), candidate) != sizeof(float)) {
          console.println("ERROR,CAL_SAVE_FAILED");
          input.clear(); continue;
        }
        scale = candidate; detector.reset(); hidOutput.release(); resetPeak();
        console.printf("STATUS,COUNTS_PER_KG,%.6f\n", scale);
      }
    } else console.println("ERROR,UNKNOWN_COMMAND,use tare | cal <known_kg> | status | stream on/off | hid on/off | max reset | tune show/set/reset/save");
    }
    input.clear();
    if (!streamEnabled) console.println("READY,quiet_mode,type_command_then_Enter");
  }
}
// Fit each line to the 128-pixel screen, including negative/three-digit weights.
void lcdLine(const char* text, int y, int preferredSize) {
  int size = preferredSize;
  lcd.setTextSize(size);
  while (size > 1 && lcd.textWidth(text) > lcd.width() - 4)
    lcd.setTextSize(--size);
  int x = (lcd.width() - lcd.textWidth(text)) / 2;
  lcd.setCursor(x < 0 ? 0 : x, y);
  lcd.print(text);
}
void setup() {
  auto cfg = M5.config(); cfg.internal_imu = false; cfg.internal_rtc = false;
  M5.begin(cfg);
  // The installed AtomS3 is mounted upside down.
  M5.Display.setRotation((M5.Display.getRotation() + 2) % 4);
  M5.Display.setTextSize(2); M5.Display.println("Fun Arcade\nJump Pad\nBOOT OK");
  lcd.setColorDepth(16);
  lcdReady = lcd.createSprite(128, 128) != nullptr;
  if (!lcdReady) M5.Display.println("LCD BUFFER ERROR");
  hidOutput.begin();
  Serial.begin(115200); Serial.setTxTimeoutMs(0);
  prefs.begin("jump-pad", false);
  useNau = prefs.getBool("adc-nau", false);
  // Preserve existing HX711 calibration when upgrading older firmware.
  if (!prefs.isKey("scale-hx") && prefs.isKey("scale"))
    prefs.putFloat("scale-hx", prefs.getFloat("scale", 0));
  scale = prefs.getFloat(scaleKey(), 0);
  if (useNau) {
    adcReady = initNau();
    lastNauInitAttempt = millis();
  } else {
    pinMode(Config::sck, OUTPUT); digitalWrite(Config::sck, LOW);
    pinMode(Config::dt, INPUT_PULLUP);
    adcReady = true; adcInitStage = "ready";
  }
  if (!isfinite(scale)) scale = 0;
  SavedDetectorTuning saved;
  if (prefs.getBytesLength("tuning-v1") == sizeof(saved) &&
      prefs.getBytes("tuning-v1", &saved, sizeof(saved)) == sizeof(saved) &&
      saved.version == tuningVersion && validDetectorTuning(saved.values))
    detector.setTuning(saved.values);
  console.println("BOOT,Fun Arcade,AtomS3,0.4.1");
  console.printf("STATUS,ADC,%s,initialized=%u,stage=%s,i2c_status=%d\n", adcName(), adcReady, adcInitStage, nauBusStatus);
  console.println("HEADER,ms,raw,net,filtered,kg,state,sps,ready,calibrated");
  startTare(millis()); rateStart = millis();
}
void loop() {
  uint32_t now = millis(); M5.update(); commands(now);
  // The powered breakout may not ACK the first probe immediately after reset.
  // Retry a bounded number of times so the screen/button remain usable.
  if (useNau && !adcReady && nauInitRetries < 3 &&
      now - lastNauInitAttempt >= 2000) {
    ++nauInitRetries;
    adcReady = initNau();
    now = millis(); lastNauInitAttempt = now;
    console.printf("STATUS,ADC_RETRY,%u,initialized=%u,stage=%s,i2c_status=%d\n",
                  nauInitRetries, adcReady, adcInitStage, nauBusStatus);
    if (adcReady) { startTare(now); sampleCount = 0; rateStart = now; }
  }
  const auto buttonAction = adcButton.update(M5.BtnA.isPressed(), now);
  if (buttonAction == AdcButton::Action::SHORT_PRESS) resetPeak();
  if (buttonAction == AdcButton::Action::LONG_PRESS) {
    hidOutput.release();
    if (prefs.putBool("adc-nau", !useNau) != sizeof(bool)) {
      console.println("ERROR,ADC_SAVE_FAILED");
    } else {
      // Reboot releases GPIO5/6 from their old ADC role and reloads calibration.
      M5.Display.fillScreen(TFT_BLACK);
      M5.Display.setCursor(0, 30); M5.Display.setTextSize(2);
      M5.Display.printf("ADC ->\n%s", useNau ? "HX711" : "NAU7802");
      delay(300); ESP.restart();
    }
  }
  if (readSensor(raw)) {
    bool recovering = fault || now - lastSample > Config::sensorTimeoutMs; lastSample = now;
    // The NAU7802 log alternates between the negative rail and rail + 1.
    // Neither code is a usable weight sample.
    fault = raw <= -8388607 || raw >= 8388607;
    if (fault) { peakWeight.clipped = true; detector.reset(); }
    else {
      // Never redefine zero under a player after a transient sensor fault.
      // Only startup or an explicit tare command may establish a new zero.
      if (recovering && !zeroValid) startTare(now);
      ++sampleCount;
      // Discard first 500 ms for power-up/settling; average at least 2 seconds.
      if (taring && now - tareStart >= 500) {
        tareSum += raw; ++tareCount;
        if (now - tareStart >= 2500 && tareCount >= 20) {
          zero = double(tareSum) / tareCount; zeroValid = true; taring = false;
          if (!editing) console.printf("STATUS,ZERO,%.2f\n", zero);
        }
      }
    }
    float net = raw - zero;
    filtered = net; // Preserve CSV schema; software smoothing is disabled.
    bool ready = zeroValid && !fault && !taring;
    peakWeight.observe(scale != 0 ? filtered / scale : 0, ready && scale != 0);
    if (ready && scale != 0) emitEvent(detector.update(filtered / scale, now), now);
    // Nonblocking USB writes may drop telemetry if host is absent/slow.
    if (streamEnabled && !editing) {
    console.printf("DATA,%lu,%ld,%.2f,%.2f,", (unsigned long)now, (long)raw, net, filtered);
    if (ready && scale != 0) console.printf("%.3f", filtered / scale); else console.print("nan");
    console.printf(",%s,%.1f,%u,%u\n", stateName(detector.state), sps, ready, scale != 0);
    }
  }
  if (now - lastSample > Config::sensorTimeoutMs) {
    fault = true; detector.reset();
  }
  hidOutput.update(millis(), !fault && zeroValid && !taring && scale != 0, detector.state);
  if (now - rateStart >= 2000) {
    sps = sampleCount * 1000.0f / (now - rateStart); sampleCount = 0; rateStart = now;
  }
  if (streamEnabled && !editing && now - lastStatus >= 2000) {
    lastStatus = now;
    console.printf("STATUS,%s,sps=%.1f\n", fault ? (useNau ? "NAU7802_ERROR" : "HX711_ERROR") : (sps < 60 ? "CHECK_RATE_80SPS" : "OK"), sps);
  }
  if (lcdReady && now > 1000 && now - lastDisplay >= 100) {
    lastDisplay = now;
    lcd.fillScreen(TFT_BLACK);
    if (fault) {
      lcdLine(adcName(), 12, 3);
      lcdLine("ERROR", 44, 3);
    } else if (taring) {
      lcdLine("TARE", 24, 4);
      lcdLine("Keep empty", 64, 2);
    } else if (feedbackAt && now - feedbackAt < 450) {
      lcdLine(feedback, 40, 4);
    } else {
      lcdLine(stateName(detector.state), 4, 2);
      if (scale != 0) {
        char weight[32];
        float kg = filtered / scale;
        if (fabsf(kg) < 0.05f) kg = 0; // Avoid confusing "-0.0" at rest.
        snprintf(weight, sizeof(weight), "%.1f", kg);
        lcdLine(weight, 26, 4);
        lcdLine("kg", 60, 2);
      } else {
        lcdLine("CAL", 26, 4);
        lcdLine("REQUIRED", 60, 2);
      }
    }
    char rateText[24];
    snprintf(rateText, sizeof(rateText), "%s %.0f SPS", adcName(), sps);
    lcdLine(rateText, 80, 1);
    lcd.drawFastHLine(4, 91, 120, TFT_DARKGREY);
    lcdLine(peakWeight.clipped ? "MAX kg / ADC CLIP!" : "MAX kg", 94, 1);
    char peakText[32];
    if (peakWeight.valid) snprintf(peakText, sizeof(peakText), "%.1f", peakWeight.kg);
    else snprintf(peakText, sizeof(peakText), "--");
    lcdLine(peakText, 106, 2);
    lcd.pushSprite(0, 0); // Transfer the completed frame; never clear the live LCD.
  }
  console.pump();
  delay(1);
}
