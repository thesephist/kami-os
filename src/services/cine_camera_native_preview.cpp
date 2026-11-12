// Native ESP32-S3 reduced JPEG preview; capture bytes and sensor settings are untouched.
#include "services/cine_camera.h"
#include "core/watchdog.h"
#include <esp_jpeg_dec.h>
#include <esp_heap_caps.h>
#include <limits.h>

namespace services {
namespace cine_camera {
namespace {
// The vendor codec does not support non-unit chroma sampling. Bound and inspect
// the original SOF: its public parse_header reports transformed dimensions.
bool nativeHeader(const uint8_t *data, size_t size) {
  if (!data || size < 4 || data[0] != 0xff || data[1] != 0xd8)
    return false;
  bool frame = false;
  size_t at = 2;
  while (at < size) {
    if (data[at++] != 0xff)
      return false;
    while (at < size && data[at] == 0xff)
      ++at;
    if (size - at < 3)
      return false;
    const uint8_t marker = data[at++];
    const size_t length = (size_t(data[at]) << 8) | data[at + 1];
    if (marker < 0xc0 || length < 2 || length > size - at)
      return false;
    if (marker == 0xda)
      return frame;
    if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4) {
      if (frame || marker != 0xc0 || length != 17 || data[at + 2] != 8 || data[at + 3] != 6 ||
          data[at + 4] != 0 || data[at + 5] != 8 || data[at + 6] != 0 || data[at + 7] != 3 ||
          (data[at + 9] != 0x11 && data[at + 9] != 0x21 && data[at + 9] != 0x22) ||
          data[at + 12] != 0x11 || data[at + 15] != 0x11)
        return false;
      frame = true;
    }
    at += length;
  }
  return false;
}
} // namespace
void Session::releaseNativePreview() {
  if (nativeDecoder_)
    jpeg_dec_close(nativeDecoder_);
  nativeDecoder_ = nullptr;
  heap_caps_free(nativePixels_);
  nativePixels_ = nullptr;
  nativeUnavailable_ = false;
}
bool Session::renderNativePreview(const hal::camera::CameraFrame &frame,
                                  bool (*shouldAbort)(void *), void *context) {
  constexpr unsigned width = 256, height = 192;
  constexpr int bytes = width * height * 2;
  auto interrupted = [&]() {
    if (shouldAbort && shouldAbort(context))
      status_.previewInterrupted = true;
    return status_.previewInterrupted;
  };
  if (!frame.buf || !frame.len || frame.len > INT_MAX || frame.width != 2048 ||
      frame.height != 1536 || !previewWork_ || interrupted() || !nativeHeader(frame.buf, frame.len))
    return false;
  if (!nativePixels_)
    nativePixels_ = static_cast<uint16_t *>(
        heap_caps_aligned_calloc(16, 1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!nativePixels_)
    return false;
  if (!nativeDecoder_) {
    jpeg_dec_config_t config{};
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    config.scale.width = width;
    config.scale.height = height;
    config.rotate = JPEG_ROTATE_0D;
    if (jpeg_dec_open(&config, &nativeDecoder_) != JPEG_ERR_OK || !nativeDecoder_)
      return false;
  }
  jpeg_dec_io_t io{};
  jpeg_dec_header_info_t header{};
  io.inbuf = const_cast<uint8_t *>(frame.buf);
  io.inbuf_len = int(frame.len);
  // Native headers report the transformed dimensions, as in stock's VGA path.
  if (jpeg_dec_parse_header(nativeDecoder_, &io, &header) != JPEG_ERR_OK || header.width != width ||
      header.height != height)
    return false;
  int outputBytes = 0, count = 0;
  if (jpeg_dec_get_outbuf_len(nativeDecoder_, &outputBytes) != JPEG_ERR_OK ||
      outputBytes != bytes || jpeg_dec_get_process_count(nativeDecoder_, &count) != JPEG_ERR_OK ||
      count != 1 || interrupted())
    return false;
  io.outbuf = reinterpret_cast<uint8_t *>(nativePixels_);
  core::watchdog::feed();
  const bool ok = jpeg_dec_process(nativeDecoder_, &io) == JPEG_ERR_OK && io.out_size == bytes;
  core::watchdog::feed();
  if (interrupted() || !ok)
    return false;
  // The full transformed output stays private until decoding and fitting succeed.
  for (unsigned y = 0; y < previewHeight; ++y) {
    if (interrupted())
      return false;
    const auto *source = nativePixels_ + (y * height / previewHeight) * width;
    for (unsigned x = 0; x < previewWidth; ++x)
      previewWork_[y * previewWidth + x] = source[x * width / previewWidth];
  }
  return true;
}
} // namespace cine_camera
} // namespace services
