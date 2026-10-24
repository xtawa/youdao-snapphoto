#include "rknn_enhancer.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <vector>
#include <string>
#include <iostream>
int main(){
  assert(!snapphoto::rknn_supported_by_build());
  const unsigned char rgb[3]={127,127,127};
  std::vector<unsigned char> output;
  std::string err;
  assert(!snapphoto::rknn_denoise_tile("missing.rknn",rgb,1,1,output,err));
  assert(output.empty()&&!err.empty());
  std::cout<<"RKNN SDK-unavailable guard passed\n";
}
