#pragma once
#include <math.h>
#include <stdio.h>
#include <stdint.h>

struct DetectorTuning {
  float enterKg = 8, leaveKg = 3, airKg = 2, landKg = 5;
  uint16_t standingMs = 500, airConfirmMs = 25, landingMs = 200;
};

inline bool validDetectorTuning(const DetectorTuning& t) {
  return isfinite(t.enterKg) && isfinite(t.leaveKg) && isfinite(t.airKg) && isfinite(t.landKg) &&
    t.airKg >= 0.2f && t.airKg < t.leaveKg && t.leaveKg < t.landKg &&
    t.landKg < t.enterKg && t.enterKg <= 100 &&
    t.standingMs >= 50 && t.standingMs <= 2000 &&
    t.airConfirmMs <= 200 && t.landingMs <= 1000;
}

// Change all dependent thresholds together, so lighter-player settings can be applied atomically.
inline bool parseDetectorTuning(const char* line, DetectorTuning& out) {
  float enter, leave, air, land;
  unsigned standing, airConfirm, landing;
  char extra;
  if (sscanf(line, "tune set %f %f %f %f %u %u %u %c",
             &enter, &leave, &air, &land, &standing, &airConfirm, &landing, &extra) != 7 ||
      standing > UINT16_MAX || airConfirm > UINT16_MAX || landing > UINT16_MAX) return false;
  DetectorTuning candidate;
  candidate.enterKg = enter; candidate.leaveKg = leave;
  candidate.airKg = air; candidate.landKg = land;
  candidate.standingMs = static_cast<uint16_t>(standing);
  candidate.airConfirmMs = static_cast<uint16_t>(airConfirm);
  candidate.landingMs = static_cast<uint16_t>(landing);
  if (!validDetectorTuning(candidate)) return false;
  out = candidate;
  return true;
}
