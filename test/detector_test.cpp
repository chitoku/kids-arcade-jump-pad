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
  std::puts("Detector tests passed: dwell, jump/land, debounce, reset, timeout, wrap.");
}
