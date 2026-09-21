# Fun Arcade jump pad plan

Repository: `kids-arcade-jump-pad`; intended remote: `https://github.com/chitoku/kids-arcade-jump-pad.git`.

## Implemented initial milestones (prototype tested)

- M0: AtomS3 display boot screen, USB CDC, no wait for a host.
- M1: nonblocking ready polling, signed raw HX711 channel A/gain 128, timestamped telemetry and observed SPS. RATE is hardware-controlled.
- M2: empty-pad tare at boot/button/command, signed rough calibration saved in NVS, filtered counts independent of raw values.
- M3: inspectable five-state detector with dwell times, hysteresis, timeout/reset handling. Constants in Config.h. Host regression tests.
- M4: one JUMP on confirmed unloading, one LAND on reloading, event fan-out separate from sensor and detector; LCD feedback and fault status.

## Prototype evidence and remaining acceptance

Builder confirmed four-corner 1 kg checks, responsive unloading, flicker-free large LCD and working JUMP/LAND on the assembled prototype. Telemetry confirmed about 82–83 SPS after the builder modified HX711 RATE wiring. Software smoothing is disabled.

Remaining: finish and validate the mechanical assembly for the event, check stability on the actual grass surface, collect hop traces and tune thresholds as needed. Unplug sensor during standing and flight; verify no stale events and empty-pad recovery tare. Test with host disconnected and reconnected. Physical results above are builder reports and supplied telemetry; do not treat them as load-rating certification.

## M5: concurrent USB HID + CDC

Keep `SERIAL_ONLY` as default; add `SERIAL_PLUS_HID` to the event transport. TinyUSB native USB mode is already selected, using ESP32-S3 USB pins internally. Register USBHIDKeyboard alongside CDC before USB startup; verify Arduino 2.0.17 composite descriptor/startup ordering against its bundled examples. On JUMP, enqueue a Space press and scheduled release (e.g. 30 ms); never delay sensor reading, and always release on reset/fault/disconnect. LAND does not press a key. Keep all existing DATA/STATUS/EVENT records concurrently. Test both modes on macOS, including key-up, reconnect, serial graphing, sustained sample rate and absence of repeated keys. Do not enable HID until explicitly testing in a safe focused application.

## Deferred

No Wi-Fi, BLE, OTA, networking, cloud, or multiple pads. Consider bounded telemetry buffering/drop counters only if hardware measurements demonstrate a need. Tune walking-off rejection after collecting actual traces; do not add speculative complexity now.
