#include "hdr_fusion.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <limits>

namespace snapphoto {
namespace {
float clamp(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
float to_linear(float s) {
  s = clamp(s, 0.f, 1.f);
  return s <= 0.04045f ? (s / 12.92f) : std::pow((s + 0.055f) / 1.055f, 2.4f);
}
float to_srgb(float v) {
  v = clamp(v, 0.f, 1.f);
  return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.f / 2.4f) - 0.055f;
}
const std::array<float,256>& linear_lut() {
  static const std::array<float,256> lut=[] {
    std::array<float,256> data{};
    for(std::size_t i=0;i<data.size();++i)data[i]=to_linear(static_cast<float>(i)/255.f);
    return data;
  }();
  return lut;
}
const std::array<std::uint8_t,4096>& srgb_lut() {
  static const std::array<std::uint8_t,4096> lut=[] {
    std::array<std::uint8_t,4096> data{};
    for(std::size_t i=0;i<data.size();++i) {
      const float fraction=static_cast<float>(i)/(data.size()-1);
      data[i]=static_cast<std::uint8_t>(std::lround(255.f*to_srgb(fraction)));
    }
    return data;
  }();
  return lut;
}
float luminance(const float rgb[3]) {
  return 0.2126f*rgb[0]+0.7152f*rgb[1]+0.0722f*rgb[2];
}
float certainty(float luma) {
  if(luma <= 0.008f || luma >= 0.985f) return 0.f;
  const float middle = 1.f - 1.8f * std::abs(luma - 0.5f);
  return std::max(0.03f, middle);
}
}  // namespace

Image fuse_hdr(const std::vector<ExposureFrame>& frames, int width, int height) {
  if(frames.size()<2 || frames.size()>5 || width<1 || height<1 ||
     static_cast<std::uint64_t>(width)*height>12000000ULL) {
    throw std::invalid_argument("unsupported exposure count / size");
  }
  int reference=0;
  float min_ev=100;
  for(std::size_t i=0;i<frames.size();++i) {
    if(!frames[i].rgb || !std::isfinite(frames[i].ev) || std::abs(frames[i].ev)>8.f) {
      throw std::invalid_argument("invalid frame or EV");
    }
    if(std::abs(frames[i].ev)<min_ev){min_ev=std::abs(frames[i].ev);reference=static_cast<int>(i);}
  }
  // Work in output scan order without allocating multiple floating-point images.
  // Caller retains ownership of the aligned frames.
  const std::size_t pixels=static_cast<std::size_t>(width)*height;
  Image out{width,height,std::vector<std::uint8_t>(pixels*3)};
  const auto& linear=linear_lut();
  const auto& gamma=srgb_lut();
  for(std::size_t pos=0;pos<pixels;++pos){
    float radiant[5][3]={}, observed[5][3]={}, weights[5]={};
    float reference_rgb[3]={};
    const std::size_t base=pos*3;
    for(std::size_t k=0;k<frames.size();++k){
      const float exposure=std::exp2(frames[k].ev);
      for(int ch=0;ch<3;++ch){
        observed[k][ch]=linear[frames[k].rgb[base+ch]];
        radiant[k][ch]=observed[k][ch] / exposure;
      }
      weights[k]=certainty(luminance(observed[k]));
    }
    for(int ch=0;ch<3;++ch)reference_rgb[ch]=radiant[reference][ch];
    const float ref_luma=luminance(reference_rgb);
    float sum[3]={0,0,0};
    float sum_w=0;
    for(std::size_t k=0;k<frames.size();++k){
      float w=weights[k];
      // Ghost suppression around moving objects where the middle exposure is
      // not clipped. Alignment of static scene is still a native prerequisite.
      if(k!=static_cast<std::size_t>(reference) && weights[reference]>0.05f){
        const float diff=std::abs(luminance(radiant[k])-ref_luma);
        if(diff>std::max(0.10f,ref_luma*0.45f))w*=0.03f;
      }
      sum_w+=w;
      for(int ch=0;ch<3;++ch)sum[ch]+=w*radiant[k][ch];
    }
    float merged[3];
    for(int ch=0;ch<3;++ch){
      merged[ch]=sum_w>0.00001f?sum[ch]/sum_w:reference_rgb[ch];
    }
    // Display-referred SDR tone map. Output remains 8-bit JPEG-friendly RGB,
    // not HDR10 / HLG or wide-gamut HDR screen content.
    for(int ch=0;ch<3;++ch){
      const float mapped=merged[ch] / (merged[ch]+0.35f);
      out.rgb[base+ch]=gamma[static_cast<std::size_t>(std::lround(clamp(mapped,0.f,1.f)*4095.f))];
    }
  }
  return out;
}
}  // namespace snapphoto
