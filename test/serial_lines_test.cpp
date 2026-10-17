#include <cassert>
#include <string>
#include "SerialLines.h"
void put(SerialLines& q, const char* s) { while (*s) q.push(*s++); }
int main() {
 SerialLines q;
 put(q,"DATA,1\n"); q.consume(3);
 assert(std::string(q.data(),q.remaining())=="A,1\n"); q.consume(q.remaining());
 assert(q.remaining()==0);
 for(int i=0;i<30;++i)put(q,"DATA,2\n");
 assert(q.count==q.capacity);
 put(q,"EVENT,JUMP,123\n");
 for(int i=0;i<15;++i)q.consume(q.remaining());
 assert(std::string(q.data(),q.remaining())=="EVENT,JUMP,123\n");
 q.consume(q.remaining());
 for(int i=0;i<500;++i)q.push('x'); q.push('\n');
 assert(q.remaining()==0); put(q,"OK\n"); assert(q.remaining()==3);
}
