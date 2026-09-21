#include <cassert>
#include <limits>
#include "PeakWeight.h"
int main() {
 PeakWeight p; assert(!p.valid);
 p.observe(200,false); assert(!p.valid);
 p.observe(-0.2,true); assert(p.valid && p.kg==0);
 p.observe(12,true); p.observe(87.5,true); p.observe(0,true); assert(p.kg==87.5);
 p.observe(999,false); p.observe(std::numeric_limits<float>::infinity(),true);
 p.observe(std::numeric_limits<float>::quiet_NaN(),true); assert(p.kg==87.5);
 p.clipped=true; p.observe(1,true); assert(p.clipped && p.kg==87.5);
 p.reset(); assert(!p.valid && !p.clipped && p.kg==0);
 p.observe(15,true); assert(p.kg==15);
 p.observe(225,true); assert(p.kg==225); // Never clamp to nominal 200kg.
}
