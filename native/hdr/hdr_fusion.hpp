#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace snapphoto {
// Input RGB frames must share identical dimensions and be spatially aligned.
// EV = log2(exposure / reference_exposure); one frame should have EV 0.
// Input color should be approximately sRGB and 8-bit. For highest dynamic range
// a RAW/linear pipeline is preferable; JPEG fusion is a practical fallback.
struct ExposureFrame {
    const std::uint8_t* rgb;
    float ev;
};
struct Image {
    int width;
    int height;
    std::vector<std::uint8_t> rgb;
};
Image fuse_hdr(const std::vector<ExposureFrame>& frames, int width, int height);
}  // namespace snapphoto
