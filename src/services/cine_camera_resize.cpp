// Sensor JPEG preview and gated on-device 3MP -> VGA cost experiment.
#include "services/cine_camera.h"
#include "services/photo_storage.h"
#include "core/watchdog.h"
#include "core/cine_frame.h"
#include "hal/hal_sd.h"
#include <Arduino.h>
#include <esp_camera.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <img_converters.h>
#include <JPEGDEC.h>
#include <new>
#include <math.h>
#include <string.h>
namespace services {
namespace cine_camera {
namespace {
struct Decode {
  Decode(const uint8_t *j, size_t b, uint16_t *p, uint8_t *r, unsigned w, unsigned h, bool v)
      : jpeg(j), bytes(b), preview(p), rgb(r), width(w), height(h), valid(v) {}
  const uint8_t *jpeg;
  size_t bytes;
  uint16_t *preview = nullptr;
  uint8_t *rgb = nullptr;
  unsigned width = 0, height = 0;
  bool valid = false;
};
size_t readJpeg(void *arg, size_t index, uint8_t *data, size_t bytes) {
  if (!arg)
    return 0;
  auto &c = *static_cast<Decode *>(arg);
  if (!c.jpeg || index > c.bytes)
    return 0;
  if (bytes > c.bytes - index)
    bytes = c.bytes - index;
  if (data)
    memcpy(data, c.jpeg + index, bytes);
  return bytes;
}
bool writeJpeg(void *arg, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t *data) {
  if (!arg)
    return false;
  auto &c = *static_cast<Decode *>(arg);
  core::watchdog::feed();
  if (!data) {
    if (!x && !y)
      c.valid = w && h && w == c.width && h == c.height && (c.rgb || c.preview);
    return c.valid;
  }
  if (!c.valid || !c.width || !c.height || !w || !h || x > c.width || y > c.height ||
      w > c.width - x || h > c.height - y || (!c.rgb && !c.preview))
    return false;
  if (c.rgb) {
    for (unsigned j = 0; j < h; ++j)
      memcpy(c.rgb + (size_t(y + j) * c.width + x) * 3, data + size_t(j) * w * 3, size_t(w) * 3);
    return true;
  }
  constexpr unsigned dw = Session::previewWidth, dh = Session::previewHeight;
  const unsigned x0 = (unsigned(x) * dw + c.width - 1) / c.width,
                 x1 = (unsigned(x + w) * dw + c.width - 1) / c.width;
  const unsigned y0 = (unsigned(y) * dh + c.height - 1) / c.height,
                 y1 = (unsigned(y + h) * dh + c.height - 1) / c.height;
  for (unsigned dy = y0; dy < y1 && dy < dh; ++dy)
    for (unsigned dx = x0; dx < x1 && dx < dw; ++dx) {
      const unsigned sx = dx * c.width / dw - x, sy = dy * c.height / dh - y;
      const auto *p = data + (size_t(sy) * w + sx) * 3;
      c.preview[dy * dw + dx] = uint16_t((p[0] >> 3) << 11 | (p[1] >> 2) << 5 | (p[2] >> 3));
    }
  return true;
}
// Same reduced-IDCT decoder as the stock camera; direct RGB565 callback.
int writeBlock565(JPEGDRAW *block) {
  if (!block || !block->pUser || !block->pPixels)
    return 0;
  auto &c = *static_cast<Decode *>(block->pUser);
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
struct Encode {
  Encode(uint8_t *p, size_t n, size_t cap) : bytes(p), used(n), capacity(cap) {}
  uint8_t *bytes;
  size_t used = 0, capacity = 0;
};
size_t encoded(void *arg, size_t index, const void *data, size_t bytes) {
  if (!arg)
    return 0;
  auto &c = *static_cast<Encode *>(arg);
  core::watchdog::feed();
  if (!c.bytes || c.used > c.capacity || index != c.used || bytes > c.capacity - c.used ||
      (!data && bytes))
    return 0;
  if (bytes)
    memcpy(c.bytes + c.used, data, bytes);
  c.used += bytes;
  return bytes;
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
bool Session::renderIspPreview(const hal::camera::CameraFrame &frame) {
  const bool native = frame.width == 2048;
  if (!preview_ || !core::cine::validJpeg(frame.buf, frame.len, frame.width, frame.height,
                                          native ? 2048 : 640, native ? 1536 : 480))
    return false;
  if (!previewWork_)
    previewWork_ = static_cast<uint16_t *>(
        heap_caps_malloc(previewWidth * previewHeight * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!previewWork_)
    return false;
  Decode c{frame.buf, frame.len, previewWork_, nullptr, native ? 256u : 320u, native ? 192u : 240u,
           false};
  bool ok = false;
  if (referencePreview_)
    ok = esp_jpg_decode(frame.len, native ? JPG_SCALE_8X : JPG_SCALE_2X, readJpeg, writeJpeg, &c) ==
             ESP_OK &&
         c.valid;
  else {
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
      ok = ispDecoder_->decode(0, 0, native ? JPEG_SCALE_EIGHTH : JPEG_SCALE_HALF) != 0 && c.valid;
    }
    ispDecoder_->close();
  }
  // Never present a partially decoded frame, even if another UI action redraws.
  if (ok) {
    auto *completed = preview_;
    preview_ = previewWork_;
    previewWork_ = completed;
  }
  return ok;
}
bool Session::benchmarkIspResize() {
  if (!status_.open || mode_ != Mode::Isp3Mp)
    return false;
  if (!jpeg_)
    jpeg_ =
        static_cast<uint8_t *>(heap_caps_malloc(512u * 1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!jpeg_)
    return false;
  hal::camera::CameraFrame f{};
  camera_.returnFrame();
  if (!camera_.read(&f))
    return false;
  const bool valid = f.format == PIXFORMAT_JPEG &&
                     core::cine::validJpeg(f.buf, f.len, f.width, f.height, 2048, 1536) &&
                     f.len <= 512u * 1024;
  const size_t bytes = f.len;
  if (valid)
    memcpy(jpeg_, f.buf, bytes);
  camera_.returnFrame();
  if (!valid)
    return false;
  // No full RGB 3MP buffer (9.4MB). Half-size IDCT needs 2.36MB;
  // the subsequent exact-area 1024x768 -> VGA reduction uses linear-light Q12.
  auto *rgb = static_cast<uint8_t *>(
      heap_caps_malloc(1024u * 768 * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  auto *bgr =
      static_cast<uint8_t *>(heap_caps_malloc(640u * 480 * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  auto *tables =
      static_cast<uint8_t *>(heap_caps_malloc(4608, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!rgb || !bgr || !tables) {
    heap_caps_free(rgb);
    heap_caps_free(bgr);
    heap_caps_free(tables);
    return false;
  }
  auto native = photos::prepare();
  const uint64_t start = esp_timer_get_time();
  bool ok = native.valid() && photos::write(native, jpeg_, bytes);
  const uint32_t directUs = esp_timer_get_time() - start;
  Decode c{jpeg_, bytes, nullptr, rgb, 1024, 768, false};
  uint64_t at = esp_timer_get_time();
  ok = ok && esp_jpg_decode(bytes, JPG_SCALE_2X, readJpeg, writeJpeg, &c) == ESP_OK && c.valid;
  const uint32_t decodeUs = esp_timer_get_time() - at;
  at = esp_timer_get_time();
  auto *linear = reinterpret_cast<uint16_t *>(tables);
  auto *srgb = tables + 512;
  for (unsigned i = 0; i < 256; ++i) {
    const float v = i / 255.f;
    linear[i] =
        uint16_t((v <= .04045f ? v / 12.92f : powf((v + .055f) / 1.055f, 2.4f)) * 4095 + .5f);
  }
  for (unsigned i = 0; i < 4096; ++i) {
    const float v = i / 4095.f;
    srgb[i] =
        uint8_t((v <= .0031308f ? v * 12.92f : 1.055f * powf(v, 1 / 2.4f) - .055f) * 255 + .5f);
  }
  // Ratio is 8/5 in each axis; integer overlap weights sum to64.
  for (unsigned y = 0; ok && y < 480; ++y) {
    core::watchdog::feed();
    for (unsigned x = 0; x < 640; ++x) {
      uint32_t sum[3] = {};
      const unsigned left = x * 8, right = left + 8, top = y * 8, bottom = top + 8;
      for (unsigned sy = top / 5; sy < (bottom + 4) / 5; ++sy) {
        const unsigned wy =
            (bottom < (sy + 1) * 5 ? bottom : (sy + 1) * 5) - (top > sy * 5 ? top : sy * 5);
        for (unsigned sx = left / 5; sx < (right + 4) / 5; ++sx) {
          const unsigned wx =
              (right < (sx + 1) * 5 ? right : (sx + 1) * 5) - (left > sx * 5 ? left : sx * 5);
          const auto *p = rgb + (size_t(sy) * 1024 + sx) * 3;
          for (unsigned ch = 0; ch < 3; ++ch)
            sum[ch] += linear[p[ch]] * wx * wy;
        }
      }
      auto *p = bgr + (size_t(y) * 640 + x) * 3;
      for (unsigned ch = 0; ch < 3; ++ch)
        p[2 - ch] = srgb[(sum[ch] + 32) / 64];
    }
  }
  const uint32_t resizeUs = esp_timer_get_time() - at;
  heap_caps_free(rgb);
  heap_caps_free(tables);
  at = esp_timer_get_time();
  Encode out{jpeg_, 0, 512u * 1024};
  ok = ok && fmt2jpg_cb(bgr, 640u * 480 * 3, 640, 480, PIXFORMAT_RGB888, 90, encoded, &out);
  const uint32_t encodeUs = esp_timer_get_time() - at;
  heap_caps_free(bgr);
  auto small = photos::prepare();
  at = esp_timer_get_time();
  ok = ok && small.valid() && photos::write(small, jpeg_, out.used);
  const uint32_t saveUs = esp_timer_get_time() - at;
  Serial.printf(
      "[cine-resize] ok=%u native_bytes=%u vga_bytes=%u native_save_us=%lu decode_us=%lu "
      "resize_us=%lu encode_us=%lu vga_save_us=%lu native=%s vga=%s method=half-IDCT+linear-area\n",
      ok, unsigned(bytes), unsigned(out.used), (unsigned long)directUs, (unsigned long)decodeUs,
      (unsigned long)resizeUs, (unsigned long)encodeUs, (unsigned long)saveUs, native.path,
      small.path);
  return ok;
}
} // namespace cine_camera
} // namespace services
