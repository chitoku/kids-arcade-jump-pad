#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <math.h>
#include "Config.h"
#include "Detector.h"

static_assert(Config::outputMode == Config::OutputMode::SERIAL_ONLY,
              "HID transport is a future milestone; implement it before enabling");
static_assert(ARDUINO_USB_MODE == 0, "Native TinyUSB mode required for future CDC + HID");
Preferences prefs;
Detector detector;
portMUX_TYPE hxMux = portMUX_INITIALIZER_UNLOCKED;
int32_t raw = 0;
float zero = 0, filtered = 0, scale = 0;
bool filterValid = false, zeroValid = false, fault = true, taring = true;
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
  detector.reset(); filterValid = false;
  Serial.println("STATUS,TARE,keep_pad_empty");
}
// Single event fan-out point: future HID receives the same event as Serial.
void emitEvent(Event event, uint32_t now) {
  if (event == Event::NONE) return;
  feedback = event == Event::JUMP ? "JUMP!" : "LAND";
  feedbackAt = now;
  Serial.printf("EVENT,%s,%lu\n", event == Event::JUMP ? "JUMP" : "LAND", (unsigned long)now);
}
void commands(uint32_t now) {
  static char line[64]; static unsigned used = 0; static bool overflow = false;
  // Bound command work so incoming traffic cannot starve acquisition.
  for (unsigned budget = 0; budget < 64 && Serial.available(); ++budget) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (used < sizeof(line)-1) line[used++] = c; else overflow = true;
      continue;
    }
    line[used] = 0;
    if (overflow) Serial.println("ERROR,COMMAND_TOO_LONG");
    else if (!strcmp(line, "tare")) startTare(now);
    else if (!strncmp(line, "cal ", 4)) {
      char* end; float kg = strtof(line + 4, &end);
      float candidate = kg > 0 ? filtered / kg : 0;
      if (*end || !isfinite(kg) || kg <= 0 || !isfinite(candidate) || fabsf(candidate) < 0.001f || !zeroValid || fault || fabsf(filtered) < 100)
        Serial.println("ERROR,CAL,use_known_kg_after_tare_and_stable_load");
      else {
        scale = candidate; prefs.putFloat("scale", scale); detector.reset();
        Serial.printf("STATUS,COUNTS_PER_KG,%.6f\n", scale);
      }
    } else Serial.println("STATUS,HELP,tare | cal <known_kg>");
    used = 0; overflow = false;
  }
}
void setup() {
  auto cfg = M5.config(); cfg.internal_imu = false; cfg.internal_rtc = false;
  M5.begin(cfg);
  M5.Display.setTextSize(2); M5.Display.println("Fun Arcade\nJump Pad\nBOOT OK");
  Serial.begin(115200); Serial.setTxTimeoutMs(0);
  pinMode(Config::sck, OUTPUT); digitalWrite(Config::sck, LOW);
  pinMode(Config::dt, INPUT_PULLUP);
  prefs.begin("jump-pad", false); scale = prefs.getFloat("scale", 0);
  if (!isfinite(scale)) scale = 0;
  Serial.println("BOOT,Fun Arcade,AtomS3,0.1.0");
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
          filterValid = false; Serial.printf("STATUS,ZERO,%.2f\n", zero);
        }
      }
    }
    float net = raw - zero;
    filtered = filterValid ? filtered + Config::alpha * (net - filtered) : net;
    filterValid = true;
    bool ready = zeroValid && !fault && !taring;
    if (ready && scale != 0) emitEvent(detector.update(filtered / scale, now), now);
    // Nonblocking USB writes may drop telemetry if host is absent/slow.
    Serial.printf("DATA,%lu,%ld,%.2f,%.2f,", (unsigned long)now, (long)raw, net, filtered);
    if (ready && scale != 0) Serial.printf("%.3f", filtered / scale); else Serial.print("nan");
    Serial.printf(",%s,%.1f,%u,%u\n", stateName(detector.state), sps, ready, scale != 0);
  }
  if (now - lastSample > Config::sensorTimeoutMs) {
    fault = true; zeroValid = false; detector.reset();
  }
  if (now - rateStart >= 2000) {
    sps = sampleCount * 1000.0f / (now - rateStart); sampleCount = 0; rateStart = now;
  }
  if (now - lastStatus >= 2000) {
    lastStatus = now;
    Serial.printf("STATUS,%s,sps=%.1f\n", fault ? "HX711_ERROR" : (sps < 60 ? "CHECK_RATE_80SPS" : "OK"), sps);
  }
  if (now > 1000 && now - lastDisplay >= 100) {
    lastDisplay = now; M5.Display.fillScreen(TFT_BLACK); M5.Display.setCursor(0, 0);
    M5.Display.setTextSize(1); M5.Display.println("Fun Arcade / Jump Pad");
    if (fault) { M5.Display.setTextSize(2); M5.Display.println("HX711\nERROR"); }
    else if (taring) M5.Display.println("TARE: keep pad empty");
    else {
      M5.Display.printf("RAW %ld\n", (long)raw);
      if (scale != 0) M5.Display.printf("%.1f kg\n", filtered / scale);
      else M5.Display.println("CAL REQUIRED");
      M5.Display.println(stateName(detector.state));
      if (feedbackAt && now - feedbackAt < 450) { M5.Display.setTextSize(3); M5.Display.println(feedback); }
    }
  }
  delay(1);
}
