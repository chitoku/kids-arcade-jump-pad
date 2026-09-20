# kids-arcade-jump-pad

Firmware for **Fun Arcade**, Jun's official Kids Business Fair booth. Target: original M5Stack AtomS3 (C123, ESP32-S3), HX711 channel A / gain 128, four 50 kg three-wire half-bridge cells, plywood pad.

## Develop on this Mac

Open this folder in VS Code and install the recommended PlatformIO IDE extension. PlatformIO's Build action uses `platformio.ini`; no Arduino IDE is needed. Dependencies are pinned. CLI equivalent:

```sh
pio run
pio device list
# Only after identifying the physical AtomS3 port:
pio run -t upload --upload-port /dev/cu.YOUR_ATOMS3
pio device monitor --port /dev/cu.YOUR_ATOMS3 --baud 115200
```

For this initial build, PlatformIO was installed in the workspace's `work/pio-venv`. From this repository, the exact build command is `../../work/pio-venv/bin/pio run`. The VS Code extension can use its own managed PlatformIO installation.

## First bring-up

1. Follow [wiring](docs/wiring.md), with USB disconnected. Verify the HX711 board supports 3.3 V supply/logic and select 80 SPS in hardware.
2. Connect AtomS3 by a USB data cable, identify its serial port, then upload. If necessary, hold its reset button about two seconds to enter download mode; see the official board guide linked in wiring.
3. Leave the pad empty at boot or after a sensor fault. Automatic tare discards 500 ms, then averages for at least two seconds (20 samples minimum). Button A or Serial `tare` followed by newline repeats this. Tare assumes the pad is empty; it cannot distinguish a stationary person from the pad.
4. Check raw readings under small loads at each corner. All four must change the sum in the same direction. Check reported SPS: near 80, not 10.
5. After tare, place a known stable weight, e.g. 10 kg, and send `cal 10` followed by newline. Signed counts/kg are saved in NVS; zero is measured again each boot. Remove weight and verify near zero. Calibration is deliberately manual; wait for a steady reading before sending it.
6. Start with controlled loading/unloading, then tune `include/Config.h` before trying small hops on a mechanically validated platform.

## Serial protocol

115200 USB CDC. Lines have a record tag; parsers should branch on the first field. Repeated STATUS lines allow a late monitor connection to see health. No host connection is required for sensing/LCD.

```text
HEADER,ms,raw,net,filtered,kg,state,sps,ready,calibrated
DATA,4120,843221,23122.00,23010.50,10.002,STANDING,79.8,1,1
EVENT,JUMP,5000
EVENT,LAND,5420
STATUS,OK,sps=79.8
```

`raw` is the unchanged signed ADC count; `net` subtracts tare; `filtered` is EMA in counts (alpha 0.5). `kg` is `nan` before valid tare/calibration or during faults. `ready` indicates valid tare and sensor, not calibration; `calibrated` indicates a stored scale. Pre-tare net/filtered counts are diagnostic only. Timestamps are unsigned milliseconds and wrap after about 49.7 days. SPS is the last two-second acquisition average. No-ready for 600 ms or ADC saturation causes HX711 ERROR, suppresses events, and requires automatic empty-pad tare on recovery.

Serial writes have zero timeout so disconnected/slow hosts cannot indefinitely block acquisition. Lines/events can be dropped under host backpressure; this prototype is not a lossless recorder. Raw values are never overwritten by filtering. LCD refresh is limited to 10 Hz; actual achievable sample rate must be checked on hardware.

## Detector and limits

`EMPTY` requires >=8 kg for 500 ms to arm `STANDING`. A drop below 65% of the tracked load enters `UNWEIGHTING`; <2 kg for 25 ms emits JUMP and enters `AIRBORNE`. >=5 kg emits LAND and enters `LANDING`; after 200 ms loaded, it returns to STANDING. Unweighting expires after 450 ms and flight after 1500 ms. LANDING unloading resets EMPTY. Sensor errors, tare, and calibration reset detection.

Thresholds are starting values, not measured pad tuning. Walking off can look like a jump; one total-force sensor cannot prove a person is airborne. Long absence resets EMPTY without a LAND event. Bounce/noise and real takeoff latency require physical testing. Automatic tare after recovery requires everyone to step off.

Sensor acquisition, the hardware-independent detector, and event fan-out are separate. USB uses native TinyUSB CDC (`ARDUINO_USB_MODE=0`) to leave room for composite CDC+HID. Only SERIAL_ONLY is implemented; selecting SERIAL_PLUS_HID deliberately fails compilation until that transport is added. See [PLAN](PLAN.md).

## Validation

Build: `pio run`. Detector regression test (Mac compiler):

```sh
c++ -std=c++11 -Iinclude test/detector_test.cpp -o /tmp/jump-pad-detector-test
/tmp/jump-pad-detector-test
```

Hardware bring-up remains pending. See [mechanical notes](docs/mechanical.md). No network features are included.
