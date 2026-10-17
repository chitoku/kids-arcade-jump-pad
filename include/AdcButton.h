#pragma once
#include <stdint.h>

// Ignore a held boot button until released; a long press must fire only once.
struct AdcButton {
  enum class Action { NONE, SHORT_PRESS, LONG_PRESS };
  bool armed = false, down = false, fired = false;
  uint32_t started = 0;
  Action update(bool pressed, uint32_t now) {
    if (!armed) { if (!pressed) armed = true; return Action::NONE; }
    if (pressed && !down) { down = true; fired = false; started = now; }
    if (pressed && !fired && now - started >= 2000) {
      fired = true; return Action::LONG_PRESS;
    }
    if (!pressed && down) {
      down = false;
      if (!fired) return Action::SHORT_PRESS;
    }
    return Action::NONE;
  }
};
