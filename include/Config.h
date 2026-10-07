#pragma once
namespace Config {
constexpr int dt = 5, sck = 6;
enum class OutputMode { SERIAL_ONLY, SERIAL_PLUS_HID };
#ifndef JUMP_PAD_HID
#define JUMP_PAD_HID 1
#endif
constexpr OutputMode outputMode = JUMP_PAD_HID ? OutputMode::SERIAL_PLUS_HID : OutputMode::SERIAL_ONLY;
// HX711 RATE is a hardware strap, not a firmware setting.
// Software smoothing is disabled; detector consumes each net sample.
constexpr unsigned sensorTimeoutMs = 600;
constexpr unsigned unweightTimeoutMs = 450, flightTimeoutMs = 1500;
}
