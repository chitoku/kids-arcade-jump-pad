#include <initializer_list>

#include <cassert>
#include <cstring>
#include <cstdio>
#include "CommandLine.h"
int main() {
  for (const char* ending : {"\r", "\n", "\r\n"}) {
    CommandLine p; unsigned commands = 0;
    for (const char* s = "cal 5"; *s; ++s) assert(!p.push(*s));
    for (const char* s = ending; *s; ++s) if (p.push(*s)) {
      assert(!strcmp(p.text,"cal 5")); ++commands; p.clear();
    }
    assert(commands == 1);
  }
  CommandLine p;
  p.push('5'); p.push(127); p.push('2'); p.push('\b'); assert(p.used == 0);
  for (unsigned i=0;i<100;++i) p.push('x');
  assert(p.push('\r') && p.overflow); p.clear();
  p.push('t'); assert(p.push('\n') && !strcmp(p.text,"t") && !p.overflow);
  puts("Command tests passed: CR/LF/CRLF, editing, overflow and recovery");
}
