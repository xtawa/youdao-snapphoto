#include "hdr_fusion.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <vector>
#include <stdexcept>
#include <iostream>
using namespace snapphoto;

int main() {
  // Static 2-pixel aligned scene with three exposure levels -2,0,+2.
  const std::uint8_t dark[]={45,45,45, 100,100,100};
  const std::uint8_t mid[]={95,95,95, 220,220,220};
  const std::uint8_t bright[]={180,180,180, 255,255,255};
  Image result=fuse_hdr({{dark,-2.f},{mid,0.f},{bright,2.f}},2,1);
  assert(result.width==2 && result.height==1);
  assert(result.rgb.size()==6);
  assert(result.rgb[0]>0 && result.rgb[0]<255);
  assert(result.rgb[3]>result.rgb[0]);
  assert(result.rgb[3]<255);
  bool threw=false;
  try{fuse_hdr({{dark,-2.f}},2,1);}catch(const std::invalid_argument&){threw=true;}
  assert(threw);
  threw=false;
  try{fuse_hdr({{nullptr,0.f},{mid,0.f}},2,1);}catch(const std::invalid_argument&){threw=true;}
  assert(threw);
  // Ensure dark and bright areas preserve ordering after color conversion.
  const std::uint8_t red1[]={80,30,20},red2[]={160,60,40};
  const auto tinted=fuse_hdr({{red1,-1.f},{red2,0.f}},1,1);
  assert(tinted.rgb[0]>tinted.rgb[1]);
  assert(tinted.rgb[1]>tinted.rgb[2]);
  std::cout << "HDR CPU pixel fusion tests passed\n";
}
