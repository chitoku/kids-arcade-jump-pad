#pragma once
#include "SpaceKey.h"
#ifndef JUMP_PAD_HID
#define JUMP_PAD_HID 1
#endif
#if JUMP_PAD_HID
#include <USB.h>
#include "tusb.h"
#include <USBHIDKeyboard.h>
#include <atomic>
// Registers the standard keyboard descriptor during global initialization,
// before Arduino starts the composite CDC+HID USB device.
static USBHIDKeyboard keyboardDescriptor;
static std::atomic<bool> usbResetPending{true};
static void usbLifecycle(void*, esp_event_base_t, int32_t, void*) {
  usbResetPending.store(true);
}
#endif
class HidOutput {
 public:
  SpaceKey key;
  void begin() {
#if JUMP_PAD_HID
    USB.onEvent(usbLifecycle);
    keyboardDescriptor.begin();
    USB.begin();
#else
    key.enabled = false;
#endif
  }
  bool connected() const {
#if JUMP_PAD_HID
    return tud_mounted() && !tud_suspended() && !usbResetPending.load();
#else
    return false;
#endif
  }
  void event(Event e, uint32_t now) { key.event(e, now, connected()); }
  void release() { key.release(); }
  void update(uint32_t now, bool usable, State state) {
#if JUMP_PAD_HID
    bool active = tud_mounted() && !tud_suspended();
    if (usbResetPending.exchange(false) || active != wasActive) {
      key.release(); needsSync = true; wasActive = active;
    }
    key.update(now, usable && active, state);
    if (!active || !tud_hid_ready()) return;
    // TinyUSB queues a report without waiting for transfer completion.
    // Retry if busy; release intent always supersedes an unsent press.
    if (needsSync || sentDown != key.down) {
      uint8_t keys[6] = {};
      bool nextDown = needsSync ? false : key.down;
      if (nextDown) keys[0] = HID_KEY_SPACE;
      if (tud_hid_keyboard_report(HID_REPORT_ID_KEYBOARD, 0, keys)) {
        sentDown = nextDown; needsSync = false;
      }
    }
#else
    key.release();
#endif
  }
 private:
  bool wasActive = false, needsSync = true, sentDown = false;
};
