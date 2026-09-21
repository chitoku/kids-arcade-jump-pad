#pragma once
#include <math.h>
// Session peak of valid calibrated samples, independent of LCD refresh rate.
class PeakWeight {
 public:
  float kg = 0;
  bool valid = false;
  bool clipped = false;
  void reset() { kg = 0; valid = false; clipped = false; }
  void observe(float value, bool usable) {
    if (!usable || !isfinite(value)) return;
    if (!valid || value > kg) kg = value > 0 ? value : 0;
    valid = true;
  }
};
