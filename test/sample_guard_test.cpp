#include <cassert>
#include "SampleGuard.h"
#include "Detector.h"
int main() {
  SampleGuard g;
  assert(g.update(true, 0));
  assert(!g.update(false, 10));
  assert(!g.update(true, 20));
  assert(!g.update(true, 219));
  assert(g.update(true, 220) && g.justRecovered);
  assert(g.update(true, 221) && !g.justRecovered);
  g.update(false, 300); g.update(true, 310); g.update(false, 450);
  assert(!g.update(true, 460)); assert(!g.update(true, 650));
  assert(g.update(true, 660));
  Detector d; d.resumeLoaded(37, 660);
  assert(d.state == State::STANDING);
  d.update(0, 672); d.update(0, 684);
  assert(d.update(0, 720) == Event::JUMP);
  d.resumeLoaded(0, 800); assert(d.state == State::EMPTY);
}
