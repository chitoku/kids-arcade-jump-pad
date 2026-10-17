#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Bounded, complete-line queue. USB backpressure must never stop sensing.
struct SerialLines {
  static constexpr size_t capacity = 16, lineSize = 384;
  char lines[capacity][lineSize] = {};
  size_t lengths[capacity] = {}, head = 0, count = 0, offset = 0;
  char building[lineSize] = {};
  size_t used = 0;
  bool overflow = false;
  void clear() { head = count = offset = used = 0; overflow = false; }
  void push(char c) {
    if (!overflow && used < lineSize) building[used++] = c;
    else overflow = true;
    if (c != '\n') return;
    if (!overflow) {
      if (count == capacity && used >= 6 && !memcmp(building, "EVENT,", 6)) {
        const size_t last = (head + count - 1) % capacity;
        if (memcmp(lines[last], "EVENT,", 6)) --count;
      }
      if (count < capacity) {
        const size_t tail = (head + count) % capacity;
        memcpy(lines[tail], building, used); lengths[tail] = used; ++count;
      }
    }
    used = 0; overflow = false;
  }
  const char* data() const { return lines[head] + offset; }
  size_t remaining() const { return count ? lengths[head] - offset : 0; }
  void consume(size_t bytes) {
    offset += bytes;
    if (count && offset == lengths[head]) {
      head = (head + 1) % capacity; --count; offset = 0;
    }
  }
};
