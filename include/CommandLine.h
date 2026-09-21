#pragma once
#include <stddef.h>
// CR, LF and CRLF all terminate a command once; overlong input is rejected.
class CommandLine {
 public:
  char text[64] = {};
  size_t used = 0;
  bool overflow = false;
  bool push(char c) {
    if (c == '\r' || c == '\n') {
      if (c == '\n' && afterCR) { afterCR = false; return false; }
      afterCR = c == '\r'; text[used] = 0; return true;
    }
    afterCR = false;
    if (c == '\b' || c == 127) { if (used) text[--used] = 0; }
    else if (c >= 32 && c <= 126) {
      if (used < sizeof(text)-1) { text[used++] = c; text[used] = 0; }
      else overflow = true;
    }
    return false;
  }
  void clear() { used = 0; text[0] = 0; overflow = false; }
 private:
  bool afterCR = false;
};
