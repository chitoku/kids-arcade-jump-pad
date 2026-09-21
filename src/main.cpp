#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <math.h>
#include "Config.h"
#include "Detector.h"
#include "CommandLine.h"
#include "HidOutput.h"

HidOutput hidOutput;
static_assert(ARDUINO_USB_MODE == 0, "Native TinyUSB mode required for future CDC + HID");
bool streamEnabled = true;
bool editing = false;
M5Canvas lcd(&M5.Display);
bool lcdReady = false;
Preferences prefs;
Detector detector;
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
    else if (!*line) Serial.println("STATUS,HELP,tare | cal <known_kg> | status | stream on | stream off | hid on | hid off");
    else {
      Serial.printf("ACK,%s\n", line);
      if (!strcmp(line, "stream on")) { streamEnabled = true; Serial.println("STATUS,STREAM,ON"); }
      else if (!strcmp(line, "stream off")) { streamEnabled = false; Serial.println("STATUS,STREAM,OFF"); }
      else if (!strcmp(line, "hid off")) { hidOutput.key.enabled = false; hidOutput.release(); Serial.println("STATUS,HID,OFF"); }
      else if (!strcmp(line, "hid on")) {
        hidOutput.key.enabled = JUMP_PAD_HID; hidOutput.release();
        Serial.println(JUMP_PAD_HID ? "STATUS,HID,ON" : "ERROR,HID_NOT_COMPILED");
      }
      else if (!strcmp(line, "status")) {
        Serial.printf("STATUS,HID,compiled=%u,enabled=%u,connected=%u,space_requested=%u\n",
                      unsigned(JUMP_PAD_HID), hidOutput.key.enabled, hidOutput.connected(), hidOutput.key.down);
        Serial.printf("STATUS,SENSOR,%s,taring=%u,zero_valid=%u,raw=%ld,filtered=%.2f,sps=%.1f,counts_per_kg=%.6f\n",
                      fault ? "ERROR" : "OK", taring, zeroValid, (long)raw, filtered, sps, scale);
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
        scale = candidate; detector.reset(); hidOutput.release();
        Serial.printf("STATUS,COUNTS_PER_KG,%.6f\n", scale);
      }
    } else Serial.println("ERROR,UNKNOWN_COMMAND,use tare | cal <known_kg> | status | stream on | stream off | hid on | hid off");
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
  Serial.println("BOOT,Fun Arcade,AtomS3,0.2.0");
  Serial.println("HEADER,ms,raw,net,filtered,kg,state,sps,ready,calibrated");
  startTare(millis()); rateStart = millis();
}
void loop() {
  uint32_t now = millis(); M5.update(); commands(now);
  if (M5.BtnA.wasPressed()) startTare(now);
  if (readSensor(raw)) {
    bool recovering = fault || now - lastSample > Config::sensorTimeoutMs; lastSample = now;
    fault = raw == -8388608 || raw == 8388607;
    if (fault) { detector.reset(); zeroValid = false; }
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
      lcdLine("HX711", 22, 3);
      lcdLine("ERROR", 62, 3);
    } else if (taring) {
      lcdLine("TARE", 24, 4);
      lcdLine("Keep empty", 80, 2);
    } else if (feedbackAt && now - feedbackAt < 450) {
      lcdLine(feedback, 40, 4);
    } else {
      lcdLine(stateName(detector.state), 4, 2);
      if (scale != 0) {
        char weight[32];
        float kg = filtered / scale;
        if (fabsf(kg) < 0.05f) kg = 0; // Avoid confusing "-0.0" at rest.
        snprintf(weight, sizeof(weight), "%.1f", kg);
        lcdLine(weight, 34, 5);
        lcdLine("kg", 80, 2);
      } else {
        lcdLine("CAL", 30, 4);
        lcdLine("REQUIRED", 70, 2);
      }
      char rawText[32];
      snprintf(rawText, sizeof(rawText), "RAW %ld", (long)raw);
      lcdLine(rawText, 112, 1);
      char rateText[24];
      snprintf(rateText, sizeof(rateText), "%.0f SPS%s", sps, sps < 60 ? " / NEED 80" : "");
      lcdLine(rateText, 100, 1);
    }
    lcd.pushSprite(0, 0); // Transfer the completed frame; never clear the live LCD.
  }
  delay(1);
}
