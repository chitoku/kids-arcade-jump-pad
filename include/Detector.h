#pragma once
#include <stdint.h>
#include "Config.h"
#include "DetectorTuning.h"
enum class State { EMPTY, STANDING, UNWEIGHTING, AIRBORNE, LANDING };
enum class Event { NONE, JUMP, LAND };
inline const char* stateName(State s) {
  static const char* names[] = {"EMPTY","STANDING","UNWEIGHTING","AIRBORNE","LANDING"};
  return names[static_cast<unsigned>(s)];
}
class Detector {
 public:
  State state = State::EMPTY;
  DetectorTuning tuning;
  void setTuning(const DetectorTuning& next) { tuning = next; reset(); }
  void reset() { state = State::EMPTY; timing = lowTiming = false; baseline = 0; }
  Event update(float kg, uint32_t now) {
    switch (state) {
      case State::EMPTY:
        if (kg >= tuning.enterKg) {
          if (!timing) { since = now; timing = true; }
          if (now - since >= tuning.standingMs) { baseline = kg; go(State::STANDING, now); }
        } else timing = false;
        break;
      case State::STANDING:
        if (kg < baseline * 0.65f) go(State::UNWEIGHTING, now);
        else baseline += 0.01f * (kg - baseline);
        break;
      case State::UNWEIGHTING:
        if (kg < tuning.airKg) {
          if (!lowTiming) { lowSince = now; lowTiming = true; }
          if (now - lowSince >= tuning.airConfirmMs) { go(State::AIRBORNE, now); return Event::JUMP; }
        } else lowTiming = false;
        if (kg >= baseline * 0.8f) go(State::STANDING, now);
        else if (now - since > Config::unweightTimeoutMs) reset();
        break;
      case State::AIRBORNE:
        if (kg >= tuning.landKg) { go(State::LANDING, now); return Event::LAND; }
        if (now - since > Config::flightTimeoutMs) reset();
        break;
      case State::LANDING:
        if (kg < tuning.leaveKg) reset();
        else if (now - since >= tuning.landingMs) { baseline = kg; go(State::STANDING, now); }
        break;
    }
    return Event::NONE;
  }
 private:
  uint32_t since = 0, lowSince = 0;
  bool timing = false, lowTiming = false;
  float baseline = 0;
  void go(State next, uint32_t now) { state = next; since = now; timing = lowTiming = false; }
};
