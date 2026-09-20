#pragma once
namespace Config {
constexpr int dt = 5, sck = 6;
enum class OutputMode { SERIAL_ONLY, SERIAL_PLUS_HID };
constexpr OutputMode outputMode = OutputMode::SERIAL_ONLY;
constexpr float alpha = 0.5f;
constexpr unsigned sensorTimeoutMs = 600;
constexpr float enterKg = 8, leaveKg = 3, airKg = 2, landKg = 5;
constexpr unsigned standingMs = 500, airConfirmMs = 25, landingMs = 200;
constexpr unsigned unweightTimeoutMs = 450, flightTimeoutMs = 1500;
}
