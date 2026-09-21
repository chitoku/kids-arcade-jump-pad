# Fun Arcade jump pad plan

Repository: `kids-arcade-jump-pad`; intended remote: `https://github.com/chitoku/kids-arcade-jump-pad.git`.

## Implemented initial milestones (prototype tested)

- M0: AtomS3 display boot screen, USB CDC, no wait for a host.
- M1: nonblocking ready polling, signed raw HX711 channel A/gain 128, timestamped telemetry and observed SPS. RATE is hardware-controlled.
- M2: empty-pad tare at boot/command, signed rough calibration saved in NVS, filtered counts independent of raw values.
- M3: inspectable five-state detector with dwell times, hysteresis, timeout/reset handling. Constants in Config.h. Host regression tests.
- M4: one JUMP on confirmed unloading, one LAND on reloading, event fan-out separate from sensor and detector; LCD feedback and fault status.

## Prototype evidence and remaining acceptance

Builder confirmed four-corner 1 kg checks, responsive unloading, flicker-free large LCD and working JUMP/LAND on the assembled prototype. Telemetry confirmed about 82–83 SPS after the builder modified HX711 RATE wiring. Software smoothing is disabled.

Remaining: finish and validate the mechanical assembly for the event, check stability on the actual grass surface, collect hop traces and tune thresholds as needed. Unplug sensor during standing and flight; verify no stale events and empty-pad recovery tare. Test with host disconnected and reconnected. Physical results above are builder reports and supplied telemetry; do not treat them as load-rating certification.

## M5: concurrent USB HID + CDC — implemented, hardware verification pending

Default atoms3 registers a standard USB keyboard alongside CDC. JUMP requests Space down; LAND requests Space up. The user's chosen behavior is a hold through flight, replacing the earlier short-pulse proposal. The atoms3-serial environment omits HID.

The transport sends nonblocking TinyUSB reports, retries busy endpoints and synchronizes all-keys-up after USB lifecycle changes. Key intent is cleared on invalid sensor/tare/calibration/reset/timeout, or hid off. Reconnect and hid on never replay old jumps. Existing sensor and detector logic is unchanged.

Both builds and host policy tests are required. Complete the README hardware acceptance checklist (enumeration, real key-up, reconnect/suspend, simultaneous telemetry and game input) on the physical pad before claiming M5 accepted.

## Deferred

No Wi-Fi, BLE, OTA, networking, cloud, or multiple pads. Consider bounded telemetry buffering/drop counters only if hardware measurements demonstrate a need. Tune walking-off rejection after collecting actual traces; do not add speculative complexity now.
