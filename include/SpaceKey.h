#pragma once
#include "Detector.h"
// Host-testable key intent. Never replay a takeoff after reconnect or re-enable.
class SpaceKey {
 public:
  bool enabled = true;
  bool down = false;
  void release() { down = false; }
  void event(Event e, uint32_t now, bool connected) {
    if (e == Event::LAND) release();
    else if (e == Event::JUMP && enabled && connected && !down) { down = true; pressedAt = now; }
  }
  void update(uint32_t now, bool usable, State state) {
    if (!enabled || !usable || state != State::AIRBORNE ||
        (down && now - pressedAt >= Config::flightTimeoutMs)) release();
  }
 private:
  uint32_t pressedAt = 0;
};
