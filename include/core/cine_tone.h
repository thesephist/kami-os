#pragma once
#include <stddef.h>
#include <stdint.h>
namespace core {
namespace cine {
// Bayer8 BG/GR, sensor BLC pedestal, factory daylight gains. No scene-neutral estimation.
struct Parameters {
  int8_t exposureThirds = 0;
  uint8_t look = 1, black = 4, color = 0;
  uint16_t gains[3] = {1472, 1024, 1344};
  bool filmic = true;
};
// Startup rendering meter; never includes the existing exposure adjustment.
struct Metering {
  bool valid = false, highlightLimited = false;
  uint16_t samples = 0;
  int8_t exposureThirds = 0;
  float geometricMean = 0, arithmeticMean = 0, highlight = 0;
};
Metering meterBayer(const uint8_t *raw, size_t bytes, unsigned width, unsigned height,
                    uint8_t black);
// Allocate in PSRAM. All transcendentals run in configure, never in the pixel loop.
class ToneMap {
public:
  void configure(const Parameters &p, bool neutral = false);
  void pixel(uint8_t r, uint8_t g, uint8_t b, uint8_t &ro, uint8_t &go, uint8_t &bo) const;
  bool renderBgr(const uint8_t *raw, size_t bytes, unsigned width, unsigned height, uint8_t *bgr,
                 size_t capacity) const;
  // Bayer-cell reconstruction and exact fractional area averaging in linear Q12.
  // Every native photosite contributes when reducing to VGA; no ISP gamma/JPEG.
  bool renderBgrArea(const uint8_t *raw, size_t bytes, unsigned width, unsigned height,
                     uint8_t *bgr, size_t capacity, unsigned outW, unsigned outH) const;
  // RGB scanlines for bounded-memory resolution experiments. Native rows use
  // bilinear CFA reconstruction; cell rows retain the 2x2 Bayer-cell method.
  bool renderRgbRow(const uint8_t *raw, size_t bytes, unsigned width, unsigned height, unsigned row,
                    bool native, uint8_t *rgb, size_t capacity) const;
  bool preview565(const uint8_t *raw, size_t bytes, unsigned width, unsigned height, uint16_t *rgb,
                  unsigned outW, unsigned outH) const;

private:
  void linearPixel(uint32_t r, uint32_t g, uint32_t b, uint8_t &ro, uint8_t &go, uint8_t &bo) const;
  bool d2_ = false, sunny_ = false, displayTablesReady_ = false, filmic_ = true;
  float colorMatrix_[9] = {};
  // Dense near black/unit range, coarser in HDR. Precomputed once in PSRAM;
  // no powf or dynamic allocation in either preview or capture pixel loops.
  float encodeDisplay(float value) const;
  float decodeDisplay(float value) const;
  uint16_t linear_[3][256] = {};
  uint16_t luminance_[4096] = {};
  uint8_t srgb_[4096] = {};
  float displayEncode_[4865] = {}, displayDecode_[2817] = {};
};
// Experimental OV3660 gamma seed: fifteen points plus highest-segment slope.
bool makeIspGamma(bool filmic, int exposureThirds, uint8_t output[16]);
const char *lookName(unsigned look);
const char *colorName(unsigned color);
} // namespace cine
} // namespace core
