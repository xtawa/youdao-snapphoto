#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace snapphoto {
// Optional RKNN backend: requires a compatible .rknn denoiser AND device runtime.
// One 256x256 RGB tile per call; packing and sigma conditioning are internal.
bool rknn_supported_by_build();
bool rknn_denoise_tile(const std::string& model_path, const std::uint8_t* rgb,
                      int width,int height,std::vector<std::uint8_t>& output,
                      std::string& error);
}
