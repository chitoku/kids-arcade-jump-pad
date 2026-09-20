# Authoritative wiring — original AtomS3 + HX711

Verified 2026-09-19 against [M5Stack AtomS3 official pinout and peripheral table](https://docs.m5stack.com/en/core/AtomS3). Use rear header labels, not a guessed connector orientation. DT = **G5**, SCK = **G6**. LCD uses G21/G17/G15/G33/G34/G16; onboard IMU uses G38/G39. G5/G6 avoid those buses. Do not substitute an AtomS3R pinout. No Atomic base is assumed installed.

| AtomS3 rear header | HX711 breakout |
|---|---|
| 3V3 | VCC (only a breakout verified to operate at 3.3 V) |
| GND | GND |
| G5 | DT / DOUT |
| G6 | SCK / PD_SCK |

USB supplies AtomS3. Do not use the Grove red 5V lead for this 3.3V wiring. ESP32 GPIO is not 5V tolerant. If the module exposes separate VCC/VSUP and VDD/DVDD, identify its schematic first: digital DVDD must be 3.3 V. Some 5V analog regulator arrangements do not operate correctly at 3.3V; do not infer compatibility from the HX711 chip alone. For a verified split-supply board, analog supply may differ, but that is not the default diagram. Check E+/E- excitation with a meter before attaching the bridge.

[Editable system diagram](wiring.mmd) and [vector diagram](wiring.svg) accompany this table. The table and cell netlist below define actual connections; diagram positions do not define physical header orientation.

## Four three-wire half bridges

Each cell contains two resistive sections with a center tap C and two outer ends X/Y. With power disconnected, the largest resistance is between the two outer wires; C-to-each-end is approximately half that. Wire colors vary: measure and label C1..C4, X1..X4, Y1..Y4 rather than assuming red is center.

A standard four-cell ring combines the eight resistive sections into four bridge arms. Each outer-to-outer junction below floats; do not ground it. Center taps are the four bridge nodes:

| Net | Connect exactly |
|---|---|
| Excitation positive | C1 → HX711 E+ |
| Signal positive | C2 → HX711 A+ |
| Excitation negative | C3 → HX711 E- |
| Signal negative | C4 → HX711 A- |
| Floating junction J12 | Y1 ↔ X2 |
| Floating junction J23 | Y2 ↔ X3 |
| Floating junction J34 | Y3 ↔ X4 |
| Floating junction J41 | Y4 ↔ X1 |

X/Y assignment must be chosen for additive response: after assembling, press each cell separately in its intended mounting/load direction. All four contributions must have the same sign. If one subtracts, exchange only that cell's X/Y wires, leaving C fixed, and retest all four. If all have negative response, signed calibration handles that, or swap A+/A- globally. Do not simply parallel four center taps. A proper four-sensor combinator board is an alternative to these junctions; follow its own netlist and orientation.

The topology follows the [SparkFun four-sensor combinator concept](https://github.com/sparkfun/Load_Sensor_Combinator). The physical cell locations and colors are intentionally not prescribed without inspecting the actual cells. Check balanced bridge voltage before loading, then small corner loads before calibration.

## 80 SPS is a hardware selection

The [AVIA HX711 datasheet](https://cdn.sparkfun.com/assets/b/f/5/a/e/hx711F_EN.pdf) specifies RATE pin 15 low for 10 SPS and high (DVDD) for 80 SPS. Use the breakout's documented RATE switch/jumper; some boards require cutting a ground trace and bridging RATE to DVDD. Inspect the actual board before modifying it. Never short a pin still tied to ground to the supply. Firmware cannot switch this through DT/SCK; there is no configuration register. A hardwired 10 SPS board will stream but is unsuitable for the default short jump timing.

After changing RATE, power-cycle and check the firmware's measured SPS. 80 SPS nominal intervals are about 12.5 ms; 10 SPS about 100 ms. Firmware warns below 60 SPS. No-ready and full-scale saturation are detectable; other wiring faults may produce plausible counts, so software health alone does not validate wiring.

## Fritzing

No Fritzing executable/app was found in PATH or /Applications on this Mac, and no verified AtomS3/HX711 parts were available for this task. No .fzz/.fz was fabricated. Mermaid, SVG, and this Markdown netlist remain editable in the repository.
