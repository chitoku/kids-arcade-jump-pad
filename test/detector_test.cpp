#include <cassert>
#include <cstdio>
#include "Detector.h"
int main() {
  Detector d;
  assert(d.update(30, 0) == Event::NONE);
  d.update(30, 500); assert(d.state == State::STANDING);
  d.update(0, 510); d.update(0, 520);
  assert(d.update(0, 540) == Event::NONE);
  assert(d.update(0, 545) == Event::JUMP);
  assert(d.update(0, 600) == Event::NONE);
  assert(d.update(40, 700) == Event::LAND);
  assert(d.update(35, 720) == Event::NONE);
  d.update(30, 900); assert(d.state == State::STANDING);
  d.reset(); assert(d.state == State::EMPTY);
  d.update(0, 1000); assert(d.state == State::EMPTY);
  d.update(30, 1100); d.update(0, 1200); d.update(30, 1300);
  d.update(30, 1700); assert(d.state == State::EMPTY);
  d.update(30, 1800); assert(d.state == State::STANDING);
  d.update(15, 1810); d.update(30, 1820); assert(d.state == State::STANDING);
  d.update(0, 1830); d.update(0, 1840); assert(d.update(0, 1865) == Event::JUMP);
  d.update(0, 3400); assert(d.state == State::EMPTY);
  d.reset(); d.update(30, UINT32_MAX - 200); d.update(30, 300);
  assert(d.state == State::STANDING);
  // Recorded failure: standing load drops to ~37 kg, UNWEIGHTING times out,
  // then successive <500 ms hops used to remain EMPTY for several seconds.
  Detector shifted;
  shifted.update(70, 0); shifted.update(70, 500);
  shifted.update(37, 600);
  assert(shifted.state == State::UNWEIGHTING);
  assert(shifted.update(37, 1060) == Event::NONE);
  assert(shifted.state == State::STANDING);
  shifted.update(0, 1072); shifted.update(0, 1084);
  assert(shifted.update(0, 1110) == Event::JUMP);
  assert(shifted.update(80, 1200) == Event::LAND);
  Detector steppedOff;
  steppedOff.update(70, 0); steppedOff.update(70, 500);
  steppedOff.update(5, 600);
  steppedOff.update(5, 1060);
  assert(steppedOff.state == State::EMPTY);
  std::puts("Detector tests passed: dwell, jump/land, debounce, reset, timeout, wrap.");
}
