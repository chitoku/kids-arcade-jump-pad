#include <cassert>
#include "SpaceKey.h"
int main() {
 SpaceKey k;
 k.event(Event::JUMP,100,true); assert(k.down);
 k.update(120,true,State::AIRBORNE); assert(k.down);
 k.event(Event::LAND,200,true); assert(!k.down);
 k.event(Event::JUMP,300,true); k.update(301,false,State::AIRBORNE); assert(!k.down);
 k.update(302,true,State::AIRBORNE); assert(!k.down);
 k.event(Event::JUMP,400,false); assert(!k.down);
 k.event(Event::JUMP,500,true); k.update(2000,true,State::AIRBORNE); assert(!k.down);
 k.event(Event::JUMP,3000,true); k.update(3001,true,State::EMPTY); assert(!k.down);
 k.enabled=false; k.event(Event::JUMP,4000,true); assert(!k.down);
 k.enabled=true; k.update(4001,true,State::AIRBORNE); assert(!k.down);
 k.event(Event::JUMP,UINT32_MAX-100,true); k.update(1500,true,State::AIRBORNE); assert(!k.down);
 k.event(Event::JUMP,2000,true); k.release(); assert(!k.down);
 // Drive the real detector with an 80 Hz-ish takeoff/landing trace.
 Detector d; SpaceKey integrated;
 auto sample = [&](float kg, uint32_t now) {
   integrated.event(d.update(kg, now), now, true);
   integrated.update(now, true, d.state);
 };
 sample(30,0); sample(30,500); sample(0,512); sample(0,524);
 sample(0,536); assert(!integrated.down);
 sample(0,560); assert(integrated.down);
 sample(0,700); assert(integrated.down);
 sample(35,800); assert(!integrated.down);
 sample(35,1010); sample(0,1022); sample(0,1034); sample(0,1060);
 assert(integrated.down);
 sample(0,2561); assert(!integrated.down && d.state == State::EMPTY);
}
