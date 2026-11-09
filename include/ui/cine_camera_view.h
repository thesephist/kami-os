#pragma once
#include "ui/camera_photographer_view.h"
#include "config/ui_theme.h"
// Cine uses the stock framing, photographer, shutter and settings marks.
// A opens settings; the wheel toggles shutter/WB. Popup is transient.
namespace ui {
namespace cine_view {
template <class C>
void image(C &c, const uint16_t *source, unsigned width, unsigned height, bool pulse = false) {
  if (!source || !width || !height) {
    c.fillRect(0, 0, 280, 240, 0);
    return;
  }
  uint16_t row[280], sourceX[280];
  // Stock's center280 of a320x240 feed: same aspect-preserving center crop.
  for (unsigned x = 0; x < 280; ++x)
    sourceX[x] = (x + 20) * width / 320;
  for (unsigned y = 0; y < 240; ++y) {
    const auto *src = source + size_t(y * height / 240) * width;
    for (unsigned x = 0; x < 280; ++x) {
      row[x] = src[sourceX[x]];
      if (pulse)
        row[x] = raster::blend(0xffff, row[x], 12);
    }
    c.pushImage(0, y, 280, 1, row);
  }
}
// Native20px, eight-tooth stock-style gear. Horizontal and vertical teeth
// share the same geometry; no stretching or interpolated pixel gaps.
static constexpr uint8_t settingsMask[] = {
    0x00, 0xf0, 0x00, 0x00, 0x60, 0x00, 0x18, 0x61, 0x80, 0x3b, 0xfd, 0xc0, 0x3f, 0xff, 0xc0,
    0x0f, 0xff, 0x00, 0x1f, 0xff, 0x80, 0x1f, 0x0f, 0x80, 0x9e, 0x07, 0x90, 0xfe, 0x07, 0xf0,
    0xfe, 0x07, 0xf0, 0x9e, 0x07, 0x90, 0x1f, 0x0f, 0x80, 0x1f, 0xff, 0x80, 0x0f, 0xff, 0x00,
    0x3f, 0xff, 0xc0, 0x3b, 0xfd, 0xc0, 0x18, 0x61, 0x80, 0x00, 0x60, 0x00, 0x00, 0xf0, 0x00};
static_assert(sizeof(settingsMask) == ((20 + 7) / 8) * 20,
              "Scene bitmap rows must be byte-aligned");
template <class C> void settingsIcon(C &c) {
  for (int y = 0; y < 20; ++y)
    for (int x = 0; x < 20; ++x) {
      if (settingsMask[y * 3 + x / 8] & (128 >> (x & 7)))
        c.fillRect(18 + x, 14 + y, 1, 1, 0xffff);
    }
}
inline uint16_t composite(uint16_t fg, uint16_t bg, unsigned alpha) {
  const unsigned inv = 255 - alpha;
  return uint16_t(((((fg >> 11) * alpha + (bg >> 11) * inv + 127) / 255) << 11) |
                  (((((fg >> 5) & 63) * alpha + ((bg >> 5) & 63) * inv + 127) / 255) << 5) |
                  (((fg & 31) * alpha + (bg & 31) * inv + 127) / 255));
}
// Fixed on-screen boxes only; read/write each rounded span in a stack row.
template <class C> void backdrop(C &c, int x, int y, int w, int h, unsigned alpha) {
  if (!alpha)
    return;
  uint16_t row[134];
  if (x < 0 || y < 0 || w < 12 || w > 134 || h < 12 || h > 240 || x > 280 - w || y > 240 - h)
    return;
  if (alpha > 255)
    alpha = 255;
  const int inset[6] = {5, 3, 2, 1, 1, 0};
  for (int yy = 0; yy < h; ++yy) {
    int edge = yy < h - 1 - yy ? yy : h - 1 - yy;
    int pad = edge < 6 ? inset[edge] : 0;
    const int count = w - 2 * pad;
    for (int xx = 0; xx < count; ++xx)
      row[xx] = composite(0, c.at(x + pad + xx, y + yy), alpha);
    c.pushImage(x + pad, y + yy, count, 1, row);
  }
}
template <class C> struct FadedText {
  C &base;
  unsigned alpha;
  uint16_t at(int x, int y) const { return base.at(x, y); }
  uint16_t readPixel(int x, int y) const { return base.at(x, y); }
  void fillRect(int x, int y, int w, int h, uint16_t ink) {
    if (alpha == 255) {
      base.fillRect(x, y, w, h, ink);
      return;
    }
    if (!alpha)
      return;
    for (int yy = 0; yy < h; ++yy)
      for (int xx = 0; xx < w; ++xx)
        base.fillRect(x + xx, y + yy, 1, 1, composite(ink, base.at(x + xx, y + yy), alpha));
  }
};
template <class C> void regular(C &c, const char *text, int x, int y, uint16_t ink = 0xffff) {
#ifdef ORIGAMI_INSTRUMENT_FONT
  instrument_text::text(c, text, x, y, instrument::fonts[1], ink);
#else
  pixel_text::text(c, text, x, y, 2, ink);
#endif
}
inline int regularWidth(const char *text) {
#ifdef ORIGAMI_INSTRUMENT_FONT
  return instrument_text::width(text, instrument::fonts[1]);
#else
  return int(strlen(text)) * 12;
#endif
}
template <class C>
void chrome(C &c, const char *wb, const char *shutter, unsigned phase, bool wbWheel = false,
            unsigned opacity = 0) {
  const unsigned stage = phase / 1000;
  const char *caption = stage == 1 || stage == 2   ? "CAPTURE"
                        : stage == 3 || stage == 4 ? "SAVING"
                        : stage == 5               ? "SAVED"
                        : stage == 6               ? "SAVE FAILED"
                                                   : "B: PHOTO";
  // Backing goes first so the shared capture caption is drawn only once.
  backdrop(c, 52, 197, int(strlen(caption)) * 6 + 8, 18, 128);
  camera_photographer::overlays(c, phase, false, false, false);
  settingsIcon(c);
  if (!stage)
    pixel_text::text(c, caption, 56, 202, 1, 0xffff);
  if (!opacity)
    return;
  if (opacity > 255)
    opacity = 255;
  int valueWidth = regularWidth(wb);
  const int sw = regularWidth(shutter);
  if (sw > valueWidth)
    valueWidth = sw;
  int width = valueWidth + 46;
  if (width < 94)
    width = 94;
  if (width > 126)
    width = 126;
  const int x = 268 - width;
  // Separate pills hug the rows, with a50%-opaque black backing at full opacity.
  backdrop(c, x, 43, width, 28, (opacity * 128 + 127) / 255);
  backdrop(c, x, 73, width, 28, (opacity * 128 + 127) / 255);
  FadedText<C> text{c, opacity};
  pixel_text::text(text, "SS", x + 8, 52, 1, wbWheel ? 0xffff : kUiEdit);
  pixel_text::text(text, "WB", x + 8, 82, 1, wbWheel ? kUiEdit : 0xffff);
  regular(text, shutter, x + 38, 46, wbWheel ? 0xffff : kUiEdit);
  regular(text, wb, x + 38, 76, wbWheel ? kUiEdit : 0xffff);
}
} // namespace cine_view
} // namespace ui
