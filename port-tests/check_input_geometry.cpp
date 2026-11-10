#include <cassert>
#include "../iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/I9100InputTest.h"
int main()
{
 assert(i9100_input_valid(0,0,479,799,80));
 assert(!i9100_input_valid(480,0,0,0,80));
 assert(!i9100_input_valid(0,800,0,0,80));
 assert(!i9100_input_valid(0,0,0,0,79));
 assert(!i9100_input_valid(0,0,0,0,1501));
 unsigned forward=80,backward=360;
 for(unsigned elapsed=0;elapsed<=500;elapsed+=10){
  unsigned x=i9100_input_coordinate(80,360,elapsed,400);
  unsigned reverse=i9100_input_coordinate(360,80,elapsed,400);
  assert(x>=forward && x>=80 && x<=360);
  assert(reverse<=backward && reverse>=80 && reverse<=360);
  forward=x;backward=reverse;
 }
 assert(forward==360 && backward==80);
 assert(i9100_input_coordinate(0,479,UINT64_MAX,400)==479);
}
