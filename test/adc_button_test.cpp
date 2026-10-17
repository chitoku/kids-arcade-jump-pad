#include <cassert>
#include "AdcButton.h"
int main() {
  using A = AdcButton::Action;
  AdcButton b;
  assert(b.update(true, 0) == A::NONE);
  assert(b.update(true, 3000) == A::NONE);
  b.update(false, 3001);
  b.update(true, 3100);
  assert(b.update(false, 3150) == A::SHORT_PRESS);
  b.update(true, 4000);
  assert(b.update(true, 5999) == A::NONE);
  assert(b.update(true, 6000) == A::LONG_PRESS);
  assert(b.update(true, 9000) == A::NONE);
  assert(b.update(false, 9001) == A::NONE);
}
