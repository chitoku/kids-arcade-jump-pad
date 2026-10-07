#include <cassert>
#include <cstdio>
#include "Detector.h"

int main() {
  DetectorTuning tuning;
  assert(validDetectorTuning(tuning));
  assert(parseDetectorTuning("tune set 4 1.5 1 2.5 200 12 80", tuning));
  assert(tuning.enterKg == 4 && tuning.airConfirmMs == 12 && tuning.landingMs == 80);
  DetectorTuning rejected;
  assert(!parseDetectorTuning("tune set 4 1 1.5 2.5 200 12 80", rejected));
  assert(!parseDetectorTuning("tune set 4 1.5 1 2.5 200 12 80 extra", rejected));
  assert(!parseDetectorTuning("tune set nan 1.5 1 2.5 200 12 80", rejected));

  Detector d;
  d.update(5, 0); d.update(5, 500);
  assert(d.state == State::EMPTY); // The default 8 kg gate excludes this player.
  d.setTuning(tuning);
  d.update(5, 600); d.update(5, 800);
  assert(d.state == State::STANDING);
  d.update(0, 812); d.update(0, 825);
  assert(d.update(0, 838) == Event::JUMP);
  assert(d.update(3, 900) == Event::LAND);
  d.update(3, 980); assert(d.state == State::STANDING);
  d.update(0, 992); d.update(0, 1005);
  assert(d.update(0, 1018) == Event::JUMP);
  std::puts("Tuning tests passed: validation, lighter player, repeat jump.");
}
