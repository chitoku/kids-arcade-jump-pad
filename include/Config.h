#pragma once
namespace Config {
constexpr int dt = 5, sck = 6;
enum class OutputMode { SERIAL_ONLY, SERIAL_PLUS_HID };
constexpr OutputMode outputMode = OutputMode::SERIAL_ONLY;
// HX711 RATE is a hardware strap, not a firmware setting.
// Software smoothing is disabled; detector consumes each net sample.
constexpr unsigned sensorTimeoutMs = 600;
constexpr float enterKg = 8, leaveKg = 3, airKg = 2, landKg = 5;
constexpr unsigned standingMs = 500, airConfirmMs = 25, landingMs = 200;
constexpr unsigned unweightTimeoutMs = 450, flightTimeoutMs = 1500;
}
