#include "services/cine_camera.h"
#include "core/watchdog.h"
#include "core/cine_frame.h"
#include <esp_heap_caps.h>
#include <JPEGDEC.h>
#include <new>
namespace services {
namespace cine_camera {
namespace {
struct Decode {
  explicit Decode(uint16_t *pixels) : preview(pixels) {}
  uint16_t *preview = nullptr;
  unsigned width = 256, height = 192;
  bool valid = false, interrupted = false;
  bool (*shouldAbort)(void *) = nullptr;
  void *context = nullptr;
  bool abort() {
    if (shouldAbort && shouldAbort(context))
      interrupted = true;
    return interrupted;
  }
};
// Same reduced-IDCT decoder as the stock camera; direct RGB565 callback.
int writeBlock565(JPEGDRAW *block) {
  if (!block || !block->pUser || !block->pPixels)
    return 0;
  auto &c = *static_cast<Decode *>(block->pUser);
  if (c.abort())
    return 0;
  core::watchdog::feed();
  const int x = block->x, y = block->y, w = block->iWidthUsed, h = block->iHeight;
  if (!c.preview || !c.width || !c.height || x < 0 || y < 0 || w <= 0 || h <= 0 ||
      block->iWidth < w || unsigned(block->iWidth) > c.width || unsigned(x) > c.width ||
      unsigned(y) > c.height || unsigned(w) > c.width - unsigned(x) ||
      unsigned(h) > c.height - unsigned(y) ||
      size_t(block->iWidth) * unsigned(h) > MAX_BUFFERED_PIXELS) {
    c.valid = false;
    return 0;
  }
  constexpr unsigned dw = Session::previewWidth, dh = Session::previewHeight;
  const unsigned x0 = (unsigned(x) * dw + c.width - 1) / c.width,
                 x1 = (unsigned(x + w) * dw + c.width - 1) / c.width;
  const unsigned y0 = (unsigned(y) * dh + c.height - 1) / c.height,
                 y1 = (unsigned(y + h) * dh + c.height - 1) / c.height;
  for (unsigned dy = y0; dy < y1 && dy < dh; ++dy)
    for (unsigned dx = x0; dx < x1 && dx < dw; ++dx)
      c.preview[dy * dw + dx] =
          block->pPixels[(dy * c.height / dh - y) * block->iWidth + (dx * c.width / dw - x)];
  return 1;
}
} // namespace
void Session::releaseIspPreview() {
  if (ispDecoder_) {
    ispDecoder_->close();
    ispDecoder_->~JPEGDEC();
    heap_caps_free(ispDecoder_);
    ispDecoder_ = nullptr;
  }
  heap_caps_free(previewWork_);
  previewWork_ = nullptr;
}
bool Session::renderIspPreview(const hal::camera::CameraFrame &frame, bool (*shouldAbort)(void *),
                               void *context) {
  if (!preview_ ||
      !core::cine::validJpeg(frame.buf, frame.len, frame.width, frame.height, 2048, 1536))
    return false;
  if (!previewWork_)
    previewWork_ = static_cast<uint16_t *>(
        heap_caps_malloc(previewWidth * previewHeight * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!previewWork_)
    return false;
  Decode c{previewWork_};
  c.shouldAbort = shouldAbort;
  c.context = context;
  bool ok = false;
  if (!ispDecoder_) {
    void *memory = heap_caps_malloc(sizeof(JPEGDEC), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (memory)
      ispDecoder_ = new (memory) JPEGDEC;
  }
  if (!ispDecoder_)
    return false;
  ok = ispDecoder_->openRAM(const_cast<uint8_t *>(frame.buf), int(frame.len), writeBlock565) != 0;
  if (ok)
    ok = ispDecoder_->getWidth() == frame.width && ispDecoder_->getHeight() == frame.height;
  if (ok) {
    ispDecoder_->setUserPointer(&c);
    ispDecoder_->setPixelType(RGB565_LITTLE_ENDIAN);
    c.valid = true;
    ok = ispDecoder_->decode(0, 0, JPEG_SCALE_EIGHTH) != 0 && c.valid;
  }
  ispDecoder_->close();
  status_.previewInterrupted = c.interrupted;
  // JPEGDEC treats a draw-callback stop as success; it is not a complete image.
  ok = ok && !c.interrupted;
  // Never present a partially decoded frame, even if another UI action redraws.
  if (ok) {
    auto *completed = preview_;
    preview_ = previewWork_;
    previewWork_ = completed;
  }
  return ok;
}
} // namespace cine_camera
} // namespace services
