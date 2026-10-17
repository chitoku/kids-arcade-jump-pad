#pragma once
#include <stdint.h>
#include <math.h>

struct SampleGuard {
  bool recovering = false, timing = false, justRecovered = false;
  uint32_t since = 0;
  void reset() { recovering = timing = justRecovered = false; }
  bool update(bool plausible, uint32_t now) {
    justRecovered = false;
    if (!plausible) { recovering = true; timing = false; return false; }
    if (!recovering) return true;
    if (!timing) { since = now; timing = true; }
    if (now - since < 200) return false;
    recovering = timing = false; justRecovered = true;
    return true;
  }
};
