#pragma once
#include <stdint.h>
namespace core {
namespace cine {
enum class WhiteBalance : uint8_t { Sunny = 1, Cloudy, Office, Home };
// Exact bytes of the selected sRGB approximation and pinned vendor +1 CMX row.
constexpr uint8_t gamma[16] = {34,  49,  71,  99,  110, 120, 129, 137,
                               145, 152, 165, 177, 198, 216, 233, 31};
constexpr uint8_t matrix[11] = {29, 96, 3, 13, 132, 145, 138, 118, 20, 1, 152};
constexpr int8_t aeLevel = -1;
} // namespace cine
} // namespace core
