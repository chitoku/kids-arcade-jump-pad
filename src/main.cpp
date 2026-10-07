#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <math.h>
#include "Config.h"
#include "Detector.h"
#include "CommandLine.h"
#include "HidOutput.h"
#include "PeakWeight.h"

HidOutput hidOutput;
static_assert(ARDUINO_USB_MODE == 0, "Native TinyUSB mode required for future CDC + HID");
bool streamEnabled = true;
bool editing = false;
M5Canvas lcd(&M5.Display);
bool lcdReady = false;
Preferences prefs;
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

// No wait-for-ready loop. Protect short clock pulses from interrupt stretching
// (PD_SCK high >60 us powers the HX711 down). Channel A, gain 128: 25 pulses.
bool readSensor(int32_t& value) {
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
  if (!editing) Serial.println("STATUS,MAX_RESET");
}
void printTuning() {
  const auto& t = detector.tuning;
  Serial.printf("STATUS,TUNE,enter=%.2f,leave=%.2f,air=%.2f,land=%.2f,standing_ms=%u,air_ms=%u,landing_ms=%u,dirty=%u\n",
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
  if (!editing) Serial.println("STATUS,TARE,keep_pad_empty");
}
// Fan out to HID even when the interactive Serial console is quiet.
void emitEvent(Event event, uint32_t now) {
  if (event == Event::NONE) return;
  hidOutput.event(event, now);
  feedback = event == Event::JUMP ? "JUMP!" : "LAND";
  feedbackAt = now;
  if (!editing) Serial.printf("EVENT,%s,%lu\n", event == Event::JUMP ? "JUMP" : "LAND", (unsigned long)now);
}
void commands(uint32_t now) {
  static CommandLine input;
  // First keystroke enters quiet mode. Sensor/LCD continue running.
  for (unsigned budget = 0; budget < 64 && Serial.available(); ++budget) {
    char c = Serial.read();
    if (!editing && c >= 32 && c <= 126) {
      streamEnabled = false; editing = true;
      Serial.print("\r\nSTATUS,STREAM,OFF\r\n> ");
    }
    const size_t oldUsed = input.used;
    if (!input.push(c)) {
      if (editing && (c == '\b' || c == 127) && oldUsed > input.used) Serial.print("\b \b");
      else if (editing && input.used > oldUsed) Serial.write(c);
      continue;
    }
    if (editing) Serial.println();
    editing = false;
    char* line = input.text;
    if (input.overflow) Serial.println("ERROR,COMMAND_TOO_LONG");
    else if (!*line) Serial.println("STATUS,HELP,tare | cal <known_kg> | status | stream on/off | hid on/off | max reset | tune show/set/reset/save");
    else {
      Serial.printf("ACK,%s\n", line);
      if (!strcmp(line, "stream on")) { streamEnabled = true; Serial.println("STATUS,STREAM,ON"); }
      else if (!strcmp(line, "stream off")) { streamEnabled = false; Serial.println("STATUS,STREAM,OFF"); }
      else if (!strcmp(line, "hid off")) { hidOutput.key.enabled = false; hidOutput.release(); Serial.println("STATUS,HID,OFF"); }
      else if (!strcmp(line, "hid on")) {
        hidOutput.key.enabled = JUMP_PAD_HID; hidOutput.release();
        Serial.println(JUMP_PAD_HID ? "STATUS,HID,ON" : "ERROR,HID_NOT_COMPILED");
      }
      else if (!strcmp(line, "max reset")) resetPeak();
      else if (!strcmp(line, "tune show")) printTuning();
      else if (!strcmp(line, "tune reset")) applyTuning(DetectorTuning{});
      else if (!strcmp(line, "tune save")) {
        const SavedDetectorTuning saved = { tuningVersion, detector.tuning };
        if (prefs.putBytes("tuning-v1", &saved, sizeof(saved)) == sizeof(saved)) {
          tuningDirty = false; printTuning();
        } else Serial.println("ERROR,TUNE_SAVE_FAILED");
      }
      else if (!strncmp(line, "tune set ", 9)) {
        DetectorTuning next;
        if (parseDetectorTuning(line, next)) applyTuning(next);
        else Serial.println("ERROR,TUNE,use tune set <enter_kg> <leave_kg> <air_kg> <land_kg> <standing_ms> <air_ms> <landing_ms>");
      }
      else if (!strcmp(line, "status")) {
        Serial.printf("STATUS,MAX,kg=%.3f,valid=%u,clipped=%u\n",
                      peakWeight.kg, peakWeight.valid, peakWeight.clipped);
        Serial.printf("STATUS,HID,compiled=%u,enabled=%u,connected=%u,space_requested=%u\n",
                      unsigned(JUMP_PAD_HID), hidOutput.key.enabled, hidOutput.connected(), hidOutput.key.down);
        Serial.printf("STATUS,SENSOR,%s,taring=%u,zero_valid=%u,raw=%ld,filtered=%.2f,sps=%.1f,counts_per_kg=%.6f\n",
                      fault ? "ERROR" : "OK", taring, zeroValid, (long)raw, filtered, sps, scale);
        printTuning();
      }
      else if (!strcmp(line, "tare")) startTare(now);
    else if (!strncmp(line, "cal ", 4)) {
      char* end; float kg = strtof(line + 4, &end);
      float candidate = kg > 0 ? filtered / kg : 0;
      if (*end || !isfinite(kg) || kg <= 0 || !isfinite(candidate) || fabsf(candidate) < 0.001f || !zeroValid || taring || fault || now - lastSample > Config::sensorTimeoutMs || fabsf(filtered) < 100)
        Serial.println("ERROR,CAL,use_known_kg_after_tare_and_stable_load");
      else {
        if (prefs.putFloat("scale", candidate) != sizeof(float)) {
          Serial.println("ERROR,CAL_SAVE_FAILED");
          input.clear(); continue;
        }
        scale = candidate; detector.reset(); hidOutput.release(); resetPeak();
        Serial.printf("STATUS,COUNTS_PER_KG,%.6f\n", scale);
      }
    } else Serial.println("ERROR,UNKNOWN_COMMAND,use tare | cal <known_kg> | status | stream on/off | hid on/off | max reset | tune show/set/reset/save");
    }
    input.clear();
    if (!streamEnabled) Serial.println("READY,quiet_mode,type_command_then_Enter");
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
  M5.Display.setTextSize(2); M5.Display.println("Fun Arcade\nJump Pad\nBOOT OK");
  lcd.setColorDepth(16);
  lcdReady = lcd.createSprite(128, 128) != nullptr;
  if (!lcdReady) M5.Display.println("LCD BUFFER ERROR");
  hidOutput.begin();
  Serial.begin(115200); Serial.setTxTimeoutMs(0);
  pinMode(Config::sck, OUTPUT); digitalWrite(Config::sck, LOW);
  pinMode(Config::dt, INPUT_PULLUP);
  prefs.begin("jump-pad", false); scale = prefs.getFloat("scale", 0);
  if (!isfinite(scale)) scale = 0;
  SavedDetectorTuning saved;
  if (prefs.getBytesLength("tuning-v1") == sizeof(saved) &&
      prefs.getBytes("tuning-v1", &saved, sizeof(saved)) == sizeof(saved) &&
      saved.version == tuningVersion && validDetectorTuning(saved.values))
    detector.setTuning(saved.values);
  Serial.println("BOOT,Fun Arcade,AtomS3,0.3.0");
  Serial.println("HEADER,ms,raw,net,filtered,kg,state,sps,ready,calibrated");
  startTare(millis()); rateStart = millis();
}
void loop() {
  uint32_t now = millis(); M5.update(); commands(now);
  if (M5.BtnA.wasPressed()) resetPeak();
  if (readSensor(raw)) {
    bool recovering = fault || now - lastSample > Config::sensorTimeoutMs; lastSample = now;
    fault = raw == -8388608 || raw == 8388607;
    if (fault) { peakWeight.clipped = true; detector.reset(); zeroValid = false; }
    else {
      if (recovering) startTare(now);
      ++sampleCount;
      // Discard first 500 ms for power-up/settling; average at least 2 seconds.
      if (taring && now - tareStart >= 500) {
        tareSum += raw; ++tareCount;
        if (now - tareStart >= 2500 && tareCount >= 20) {
          zero = double(tareSum) / tareCount; zeroValid = true; taring = false;
          if (!editing) Serial.printf("STATUS,ZERO,%.2f\n", zero);
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
    Serial.printf("DATA,%lu,%ld,%.2f,%.2f,", (unsigned long)now, (long)raw, net, filtered);
    if (ready && scale != 0) Serial.printf("%.3f", filtered / scale); else Serial.print("nan");
    Serial.printf(",%s,%.1f,%u,%u\n", stateName(detector.state), sps, ready, scale != 0);
    }
  }
  if (now - lastSample > Config::sensorTimeoutMs) {
    fault = true; zeroValid = false; detector.reset();
  }
  hidOutput.update(millis(), !fault && zeroValid && !taring && scale != 0, detector.state);
  if (now - rateStart >= 2000) {
    sps = sampleCount * 1000.0f / (now - rateStart); sampleCount = 0; rateStart = now;
  }
  if (streamEnabled && !editing && now - lastStatus >= 2000) {
    lastStatus = now;
    Serial.printf("STATUS,%s,sps=%.1f\n", fault ? "HX711_ERROR" : (sps < 60 ? "CHECK_RATE_80SPS" : "OK"), sps);
  }
  if (lcdReady && now > 1000 && now - lastDisplay >= 100) {
    lastDisplay = now;
    lcd.fillScreen(TFT_BLACK);
    if (fault) {
      lcdLine("HX711", 12, 3);
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
    snprintf(rateText, sizeof(rateText), "%.0f SPS%s", sps, sps < 60 ? " / NEED 80" : "");
    lcdLine(rateText, 80, 1);
    lcd.drawFastHLine(4, 91, 120, TFT_DARKGREY);
    lcdLine(peakWeight.clipped ? "MAX kg / ADC CLIP!" : "MAX kg", 94, 1);
    char peakText[32];
    if (peakWeight.valid) snprintf(peakText, sizeof(peakText), "%.1f", peakWeight.kg);
    else snprintf(peakText, sizeof(peakText), "--");
    lcdLine(peakText, 106, 2);
    lcd.pushSprite(0, 0); // Transfer the completed frame; never clear the live LCD.
  }
  delay(1);
}
