#pragma once
#include <stdint.h>
namespace core {
namespace cine {
enum class WhiteBalance : uint8_t { Sunny = 1, Cloudy, Office, Home };
// Exact bytes of the selected sRGB approximation and pinned vendor +1 CMX row.
constexpr uint8_t gamma[16] = {34,  49,  71,  99,  110, 120, 129, 137,
                               145, 152, 165, 177, 198, 216, 233, 31};
constexpr uint8_t matrix[11] = {29, 96, 3, 13, 132, 145, 138, 118, 20, 1, 152};
// OV3660 quantization scale: lower means higher quality. See
// docs/cine-jpeg-compression-study.md.
constexpr uint8_t jpegQuantizationScale = 8;
// OV3660 real gain uses Q4: 128 / 16 = 8x. Shared by Auto and manual handoff.
constexpr uint16_t gainCeilingQ4 = 8 * 16;
constexpr int8_t aeLevel = -1;
} // namespace cine
} // namespace core
