# kids-arcade-jump-pad

Firmware for **Fun Arcade**, Jun's official Kids Business Fair booth. Target: original M5Stack AtomS3 (C123, ESP32-S3), HX711 channel A / gain 128, four 50 kg three-wire half-bridge cells, plywood pad.

[日本語の導入手順](docs/quickstart-ja.md)

## Try it on another computer (VS Code + PlatformIO)

The default `atoms3` build is a composite **USB keyboard + USB Serial** device: JUMP holds Space, LAND releases it. Serial telemetry remains available concurrently. `atoms3-serial` is the diagnostic-only alternative. HID implementation is build/test verified; actual game/USB validation is pending.

1. Install Git, VS Code, and the **PlatformIO IDE** extension (Windows, macOS or Linux).
2. Clone this repository and open its root folder (the one containing `platformio.ini`):

   ```sh
   git clone https://github.com/chitoku/kids-arcade-jump-pad.git
   cd kids-arcade-jump-pad
   code .
   ```

   Alternatively use VS Code's **Git: Clone**, then Open Folder. Do not create a new PlatformIO project or search for AtomS3 in Board Explorer: the board configuration is included.
3. Let PlatformIO install the pinned dependencies. Select **Project Tasks → atoms3 → General → Build**.
4. Connect the original AtomS3 with a USB data cable. Close any Serial Monitor before **Upload**. Identify the correct board if multiple serial devices are connected.
5. If Upload reports `No serial data received`, hold the AtomS3 reset button about two seconds until its internal green LED lights, release it, wait for USB enumeration, then retry Upload. This is the reset button, not the front display button. The port may change in download mode.
6. After uploading, briefly reset if needed, then open **Monitor**. Click the terminal and type `status`, then Enter. Firmware echoes input, pauses telemetry while typing, and acknowledges commands.

PlatformIO's terminal also supports these commands:

```sh
pio run
pio device list
pio run -t upload --upload-port YOUR_BOARD_PORT
pio device monitor --port YOUR_BOARD_PORT --baud 115200
```

Use the port shown on your own machine (e.g. `COM5` on Windows, `/dev/cu.usbmodem...` on macOS or `/dev/ttyACM0` on Linux). Linux users may need serial-device permissions configured for their distribution. No project-specific secrets or local paths are required.

Calibration is stored on the AtomS3, not the computer. Moving the same pad to another computer retains it; a different board/pad needs its own calibration. The physical HX711 RATE connection must select 80 SPS; flashing firmware alone cannot change that.

## First bring-up

1. Follow [wiring](docs/wiring.md), with USB disconnected. Verify the HX711 board supports 3.3 V supply/logic and select 80 SPS in hardware.
2. Connect AtomS3 by a USB data cable, identify its serial port, then upload. If necessary, hold its reset button about two seconds to enter download mode; see the official board guide linked in wiring.
3. Leave the pad empty at boot or after a sensor fault. Automatic tare discards 500 ms, then averages for at least two seconds (20 samples minimum). Serial `tare` followed by newline repeats this. The display button resets the recorded maximum weight. Tare assumes the pad is empty; it cannot distinguish a stationary person from the pad.
4. Check raw readings under small loads at each corner. All four must change the sum in the same direction. Check reported SPS: near 80, not 10.
5. After tare, place a known stable weight, e.g. 10 kg, and send `cal 10` followed by newline. Signed counts/kg are saved in NVS; zero is measured again each boot. Remove weight and verify near zero. Calibration is deliberately manual; wait for a steady reading before sending it.
6. Start with controlled loading/unloading, then use the live detector tuning commands below before trying small hops on a mechanically validated platform.

## Serial protocol

115200 USB CDC. Lines have a record tag; parsers should branch on the first field. Repeated STATUS lines allow a late monitor connection to see health. No host connection is required for sensing/LCD.

```text
HEADER,ms,raw,net,filtered,kg,state,sps,ready,calibrated
DATA,4120,843221,23122.00,23122.00,10.002,STANDING,79.8,1,1
EVENT,JUMP,5000
EVENT,LAND,5420
STATUS,OK,sps=79.8
```

`raw` is the unchanged signed ADC count; `net` subtracts tare; `filtered` equals `net` in version 0.1.2: software smoothing is disabled. `kg` is `nan` before valid tare/calibration or during faults. `ready` indicates valid tare and sensor, not calibration; `calibrated` indicates a stored scale. Pre-tare net/filtered counts are diagnostic only. Timestamps are unsigned milliseconds and wrap after about 49.7 days. SPS is the last two-second acquisition average. No-ready for 600 ms or ADC saturation causes HX711 ERROR, suppresses events, and requires automatic empty-pad tare on recovery.

Serial writes have zero timeout so disconnected/slow hosts cannot indefinitely block acquisition. Lines/events can be dropped under host backpressure; this prototype is not a lossless recorder. Raw values are never overwritten by filtering. LCD refresh is limited to 10 Hz; actual achievable sample rate must be checked on hardware.

## Detector and limits

By default, `EMPTY` requires >=8 kg for 500 ms to arm `STANDING`. A drop below 65% of the tracked load enters `UNWEIGHTING`; <2 kg for 25 ms emits JUMP and enters `AIRBORNE`. >=5 kg emits LAND and enters `LANDING`; after 200 ms loaded, it returns to STANDING. Unweighting expires after 450 ms and flight after 1500 ms. LANDING unloading below 3 kg resets EMPTY. Sensor errors, tare, and calibration reset detection.

Thresholds are starting values, not measured pad tuning. Walking off can look like a jump; one total-force sensor cannot prove a person is airborne. Long absence resets EMPTY without a LAND event. Bounce/noise and real takeoff latency require physical testing. Automatic tare after recovery requires everyone to step off.

Sensor acquisition, the hardware-independent detector, and event fan-out are separate. USB uses native TinyUSB CDC (`ARDUINO_USB_MODE=0`) to leave room for composite CDC+HID. Config::outputMode follows the build: SERIAL_PLUS_HID by default, SERIAL_ONLY with `JUMP_PAD_HID=0`. See [PLAN](PLAN.md).

## Validation

Build: `pio run`. Detector regression test (Mac compiler):

```sh
c++ -std=c++11 -Iinclude test/detector_test.cpp -o /tmp/jump-pad-detector-test
/tmp/jump-pad-detector-test
```

Prototype validation: the builder confirmed 1 kg readings at all four corners, prompt unloading response, flicker-free LCD, and working JUMP/LAND detection. Supplied telemetry confirmed approximately 82–83 SPS and equal net/filtered values. This is prototype validation, not a completed fair-ready mechanical qualification. See [mechanical notes](docs/mechanical.md). No network features are included.

## Interactive calibration console (0.1.1)

Restart PlatformIO Monitor after uploading this version. Monitor uses LF and disables local echo because the firmware echoes input itself. Firmware accepts CR, LF and CRLF, including terminals using CR-only Enter. Backspace/Delete edits the current line; overlong commands are rejected.

Typing the first printable character immediately pauses DATA and periodic STATUS output and shows `> ` with your input. Acquisition, filtering, detection and LCD updates continue. Events are suppressed on Serial while editing a line to avoid corrupting the prompt (not queued); they resume after Enter. Tare notifications are also suppressed while editing; `status` can check completion. The console is therefore not a lossless event log while typing.

1. With pad empty, type `tare` and press Enter. Look for `ACK,tare`, then wait for `STATUS,ZERO,...` (at least 2.5 seconds).
2. Place a known load and wait for settling. Type `cal 5` for exactly 5 kg, then Enter.
3. `ACK,cal 5` confirms receipt; `STATUS,COUNTS_PER_KG,...` confirms calibration and saving. An ERROR means the command arrived but was rejected.
4. `status` prints sensor health, tare status, raw/filtered counts, SPS and calibration factor once.
5. `stream on` resumes telemetry. `stream off` keeps the console quiet. Streaming starts enabled after reboot.

Host parser regression test:

```sh
c++ -std=c++11 -Iinclude test/command_line_test.cpp -o /tmp/jump-pad-command-test
/tmp/jump-pad-command-test
```

## Display and response (0.1.2)

LCD frames are drawn into a 128x128, 16-bit RAM canvas and transferred at 10 Hz, avoiding visible clear-then-redraw flashes. Acquisition and detection continue on each available HX711 sample independently of LCD refresh. The builder confirmed flicker-free operation; telemetry showed approximately 82–83 SPS on the modified HX711 board. The LCD shows measured SPS; `NEED 80` means the observed rate is below 60 SPS. The tare average remains intentional; ongoing measurements have no software smoothing. Raw ADC data and CSV columns are preserved, and saved calibration is retained.

80 SPS cannot be selected by firmware through DT/SCK. Follow docs/wiring.md: RATE (chip pin 15) must be connected to DVDD (pin 16) instead of ground. The photographed board has no identified rate jumper; do not guess a resistor or cut point. Trace the unpowered board or use a breakout with a documented 10/80 switch. After the hardware change, power-cycle unloaded, verify approximately 80 SPS in `status` with streaming/display active, and recheck the known calibration mass.

## USB keyboard + Serial (0.2.0)

Build/upload environment `atoms3` to send **Space down on JUMP, Space up on LAND** while keeping USB CDC telemetry. There is no timed 30 ms tap. Host/game key-repeat behavior applies while held. No Serial Monitor connection is required. Focus the intended game and bind its jump action to Space. On macOS a keyboard-identification assistant may appear; this pad only sends Space and cannot perform the normal left/right-Shift identification sequence.

The hold is cancelled on tare, successful calibration, sensor fault/stale readings, detector reset, `hid off`, USB suspend/disconnect, or after 1500 ms without landing. If disconnected, a release cannot reach the host until it reconnects; the first report after reconnect is all keys up. Old takeoffs are never replayed. Endpoint-busy reports are retried in the main loop without waiting for USB completion; an unsent press is discarded if a release becomes necessary first.

`hid off` disables key output until `hid on` or reboot. `hid on` waits for a new JUMP; it does not press Space immediately. `status` reports compiled/enabled/connected state and requested Space state (not host acknowledgement). Console typing pauses telemetry but does not disable HID; use `hid off` while calibrating if needed. `stream off` does not disable HID.

For the old Serial-only behavior, select **atoms3-serial** in Project Tasks or use `pio run -e atoms3-serial -t upload`. This build does not register a keyboard descriptor. Normal `pio run` selects only `atoms3`.

### Hardware acceptance checklist (pending)

- Verify host enumerates both keyboard and CDC; retain about 80 SPS with Monitor active and game focused.
- Confirm one Space-down on takeoff, held through flight, then Space-up on landing with a key-event viewer/game (a text editor alone cannot verify release).
- While airborne, test the tare command, `hid off`, no-landing timeout and sensor fault: key must release.
- Disconnect/reconnect and suspend/resume while held; no stale press on reconnect, and a new standing/jump cycle works.
- Confirm Serial-only build sends no keys. Flashing remains manual to avoid typing into an unintended application.

Host regression test: `c++ -std=c++11 -Iinclude test/space_key_test.cpp -o /tmp/jump-pad-space-test && /tmp/jump-pad-space-test`.

## Maximum recorded weight (0.2.1)

The LCD always shows MAX kg, including during JUMP/LAND, tare and sensor errors. It records every valid calibrated sample (~80 SPS), not just the 10 Hz display updates. The current weight remains large; raw counts remain available in Serial. Press the display button once or send `max reset` to clear MAX. The next valid sample starts a new record immediately (so reset unloaded if you want it near zero). This button no longer tares or changes calibration or HID state.

MAX is held in RAM until reset/reboot or successful recalibration. Tare and sensor recovery preserve the existing maximum. Before any valid sample, MAX shows `--`. `status` includes `STATUS,MAX,kg=...,valid=...,clipped=...`; existing DATA columns are unchanged. ADC saturation latches `ADC CLIP!` next to MAX until reset/reboot/recalibration; saturated samples are not valid peaks. The flag means the recorded maximum may understate the actual peak.

This is the largest sampled equivalent load in kg, not a certified peak-force or safe-capacity indicator. Short landing impulses may be missed or attenuated by the ADC, and four nominal 50 kg cells do not establish a safe 200 kg platform capacity: an individual corner can overload below 200 kg total. No percentage-of-safe-capacity or 200 kg clamp is applied. Validate mounting, stops and actual load ratings separately.

Host test: `c++ -std=c++11 -Iinclude test/peak_weight_test.cpp -o /tmp/jump-pad-peak-test && /tmp/jump-pad-peak-test`.

On hardware: apply and remove a known mass, confirm MAX stays; press the display button unloaded, confirm it resets without starting tare; verify MAX remains visible during JUMP/LAND and both USB outputs still work. Hardware acceptance for this display change is pending.

## Live detector tuning (0.3.0)

The composite USB Serial + keyboard firmware now accepts these 115200 baud commands. `status` or `tune show` includes the active thresholds and `dirty=1` while they are not saved. `tune set` applies all seven values together and resets detection, releasing Space if held. Leave the pad empty when applying a new profile, then measure several takeoff/landing cycles before saving.

The firmware currently emits both HID Space and Serial `EVENT,JUMP` / `EVENT,LAND`. Crossing Fair uses HID for gameplay and Serial for diagnostics, avoiding duplicate inputs. For a future Serial gameplay trial, use the firmware's event records as the input (not raw ADC samples), compare their timing against host Space keydown, and turn off HID with `hid off` or ignore it in the game for that pad. `hid on` restores the current HID behavior. This preserves both test paths without choosing one prematurely.

```text
tune show
tune set <enter_kg> <leave_kg> <air_kg> <land_kg> <standing_ms> <air_ms> <landing_ms>
tune reset
tune save
```

The default profile is `8 3 2 5 500 25 200`. `enter` is the weight needed to arm, `leave` detects stepping off during landing, `air` confirms takeoff, and `land` confirms landing. The thresholds must satisfy `0.2 <= air < leave < land < enter <= 100`. Time ranges are 50–2000 ms for standing, 0–200 ms for takeoff confirmation, and 0–1000 ms for landing recovery. `tune reset` restores defaults in RAM; use `tune save` to make either a custom profile or the defaults survive reboot. Existing calibration is stored separately and remains intact. For a lighter child, first record raw weight and state in Crossing Fair's Operator Settings, then choose thresholds based on those measurements rather than applying a guessed profile.

Since 0.3.1, takeoff also accepts a drop below 25% of the player's established standing weight, and landing waits for at least 40% of that weight. The configured `air` and `land` thresholds remain minimums. A landing impact no longer replaces the standing-weight baseline. In an adult's recorded ten-hop trace, 0.3.0 detected seven hops; 0.3.1 with `air_ms=20` detected all ten plus the final step off the pad. Stepping off cannot always be distinguished from takeoff using weight alone; check for that extra event in playtesting.

Changing the HX711 to the Adafruit NAU7802 breakout requires a separate firmware sensor driver and I2C wiring. The NAU7802 supports up to 320 samples/s, but simply selecting a faster rate cannot fix thresholds or browser/game hop timing. Keep the current HX711 wiring and calibrated profile until the new board is wired, readings validated at known weights, and the detector rechecked with several consecutive jumps. [Adafruit NAU7802 board and guide](https://learn.adafruit.com/adafruit-nau7802-24-bit-adc-stemma-qt-qwiic).
