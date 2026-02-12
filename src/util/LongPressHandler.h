// LongPressHandler.h
// Small header-only helper to centralize long-press state machine logic
#pragma once

#include <cstdint>
#include <initializer_list>

class MappedInputManager;  // forward
class CrossPointSettings;

class LongPressHandler {
 public:
  LongPressHandler() : armed(true), waitForNewCycle(false), seenPress(false), rearmAfterRender(false) {}

  void reset(bool startArmed = true) {
    armed = startArmed;
    waitForNewCycle = false;
    seenPress = false;
    rearmAfterRender = false;
  }

  // Observe press/release events (call with any relevant buttons' wasPressed/wasReleased state)
  // anyWasPressed/anyWasReleased should be true when any of the buttons we care about were pressed/released.
  void observePressRelease(bool anyWasPressed, bool anyWasReleased) {
    if (!waitForNewCycle) return;
    if (anyWasReleased) {
      waitForNewCycle = false;
      seenPress = false;
      armed = true;
    }
  }

  // Call this after a render completes to re-arm when using repeating long-press behavior.
  void onRenderComplete() {
    if (rearmAfterRender) {
      rearmAfterRender = false;
      armed = true;
    }
  }

  struct PollResult {
    bool mediumPrev = false;
    bool mediumNext = false;
    bool longPress = false;
  };

  // Poll the handler to see if a medium/long event should fire.
  // prevPressed/nextPressed indicate which directional buttons are currently held down.
  // heldMs is the held time for the button currently pressed (from MappedInputManager::getHeldTime()).
  // mediumMs/longMs are threshold values provided by settings.
  // repeatEnabled controls whether long-press should re-arm after render or wait for a new cycle.
  PollResult poll(bool prevPressed, bool nextPressed, unsigned long heldMs, unsigned long mediumMs,
                  unsigned long longMs, bool repeatEnabled) {
    PollResult r;
    if (waitForNewCycle) return r;
    if (!armed) return r;

    if (prevPressed && heldMs >= mediumMs) {
      r.mediumPrev = true;
      armed = false;
      if (repeatEnabled)
        rearmAfterRender = true;
      else
        waitForNewCycle = true;
      return r;
    }
    if (nextPressed && heldMs >= mediumMs) {
      r.mediumNext = true;
      armed = false;
      if (repeatEnabled)
        rearmAfterRender = true;
      else
        waitForNewCycle = true;
      return r;
    }
    // Generic long-press (e.g., BACK held)
    if ((prevPressed || nextPressed) && heldMs >= longMs) {
      r.longPress = true;
      armed = false;
      if (repeatEnabled)
        rearmAfterRender = true;
      else
        waitForNewCycle = true;
      return r;
    }
    return r;
  }

  // Should release be suppressed (i.e., consumed) because it followed a medium/long hold?
  bool suppressRelease(unsigned long heldMs, unsigned long mediumMs, bool prevReleased, bool nextReleased) const {
    return (prevReleased || nextReleased) && (heldMs >= mediumMs);
  }

 private:
  bool armed;
  bool waitForNewCycle;
  bool seenPress;
  bool rearmAfterRender;
};
