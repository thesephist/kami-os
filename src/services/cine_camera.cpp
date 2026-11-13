#include "services/cine_camera.h"
#include "services/photo_storage.h"
#include "services/photo_print.h"
#include "hal/hal_sd.h"
#include "core/watchdog.h"
#include "core/cine_shutter.h"
#include "core/cine_frame.h"
#include "core/cine_isp.h"
#include <Arduino.h>
#include <esp_camera.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <stdio.h>
namespace services {
namespace cine_camera {
#if ORIGAMI_CINE_PROFILING
#define CINE_PROFILE(...) __VA_ARGS__
#else
#define CINE_PROFILE(...)
#endif
bool Session::open() {
  CINE_PROFILE(const uint64_t startupAt = esp_timer_get_time();)
  if (!close(false))
    return false;
  autoFrameVts_ = 0;
  configured_ = false;
  ispReadback_ = {};
  status_ = {};
  shutter_ = {};
  shutterReadyUs_ = 0;
  colorMode_ = ColorMode::Sunny;
  CINE_PROFILE(profile_ = {};)
  if (!photo_print::readyForCapture()) {
    snprintf(status_.message, sizeof(status_.message), "Printer task still finishing");
    close();
    return false;
  }
  if (!photos::initialize() || !hal::sd::ensureDir("/cine")) {
    snprintf(status_.message, sizeof(status_.message), "SD unavailable");
    close();
    return false;
  }
  if (camera_.prepareCapture(false) != camera::Session::PrepareResult::Ready) {
    snprintf(status_.message, sizeof(status_.message), "Camera resources busy");
    close();
    return false;
  }
  hal::camera::Tuning tuning;
  tuning.wb_mode = 1;
  tuning.ae_level = core::cine::aeLevel;
  tuning.allowNightMode = false;
  tuning.lenc = false;
  CINE_PROFILE(profile_.startupPrepareUs = uint32_t(esp_timer_get_time() - startupAt);)
  CINE_PROFILE(const uint64_t initAt = esp_timer_get_time();)
  if (!camera_.start(FRAMESIZE_QXGA, PIXFORMAT_JPEG, &tuning, core::cine::jpegQuantizationScale)) {
    snprintf(status_.message, sizeof(status_.message), "Camera init failed: %s",
             camera_.lastError());
    close();
    return false;
  }
  CINE_PROFILE(profile_.startupInitUs = uint32_t(esp_timer_get_time() - initAt);)
  preview_ = static_cast<uint16_t *>(
      heap_caps_malloc(previewWidth * previewHeight * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!preview_) {
    snprintf(status_.message, sizeof(status_.message), "Image buffer allocation failed");
    close();
    return false;
  }
  const uint64_t aeAt = esp_timer_get_time();
  // Observe AE over distinct frames rather than always sleeping 1.1 seconds.
  // Require three stable transitions and >=450ms; retain the old maximum.
  hal::camera::ShutterInfo previous{};
  unsigned stable = 0;
  while (esp_timer_get_time() - aeAt < 1100000) {
    hal::camera::CameraFrame frame{};
    if (!camera_.read(&frame))
      break;
    camera_.returnFrame();
    hal::camera::ShutterInfo value{};
    if (hal::camera::readJpegShutter(&value) && value.valid) {
      const uint64_t before = uint64_t(previous.actualUs) * previous.gainQ4,
                     now = uint64_t(value.actualUs) * value.gainQ4;
      const uint64_t difference = before > now ? before - now : now - before;
      stable = before && difference * 100 <= before * 3 ? stable + 1 : 0;
      previous = value;
      if (stable >= 3 && esp_timer_get_time() - aeAt >= 450000) {
        CINE_PROFILE(profile_.startupAeStable = true;)
        break;
      }
    } else
      stable = 0;
  }

  CINE_PROFILE(profile_.startupAeUs = uint32_t(esp_timer_get_time() - aeAt);)
  CINE_PROFILE(const uint64_t ispAt = esp_timer_get_time();)
  status_.open = true;
  if (!configureExposure(ExposureMode::Automatic, 16667, 16))
    return false;
  CINE_PROFILE(profile_.startupIspUs = uint32_t(esp_timer_get_time() - ispAt);)
  CINE_PROFILE(const uint64_t previewAt = esp_timer_get_time();)
  // Keep the two-frame freshness guard after ISP writes. Drain transitional
  // frames until an actual fresh preview is decoded, with a bounded deadline.
  bool first = false;
  do {
    first = preview();
  } while (!first && shutterReadyUs_ &&
           uint64_t(esp_timer_get_time()) <
               shutterReadyUs_ + uint64_t(shutter_.frameUs) * 3 + 250000);
  if (!first) {
    snprintf(status_.message, sizeof(status_.message), "Initial camera frame unavailable");
    close();
    return false;
  }

  CINE_PROFILE(profile_.startupPreviewUs = uint32_t(esp_timer_get_time() - previewAt);
               profile_.startupUs = uint32_t(esp_timer_get_time() - startupAt);)
  return true;
}
bool Session::close(bool restoreUi) {
  status_.open = false;
  camera_.returnFrame();
  camera_.stop();
  // Failed teardown retains ownership and allocations until a safe retry.
  if (camera_.running()) {
    snprintf(status_.message, sizeof(status_.message), "Camera cleanup pending; retry");
    return false;
  }
  releaseIspPreview();
  heap_caps_free(preview_);
  preview_ = nullptr;
  CINE_PROFILE(cachedFrameUs_ = lastPreviewDoneUs_ = 0;)
  if (restoreUi)
    camera_.restoreUi(camera::Session::Resume::DeferredWithInputRecovery);
  return true;
}
bool Session::preview(bool (*shouldAbort)(void *), void *context) {
  if (!status_.open)
    return false;
  status_.previewInterrupted = false;
  if (shouldAbort && shouldAbort(context)) {
    status_.previewInterrupted = true;
    return false;
  }
  hal::camera::CameraFrame frame{};
  CINE_PROFILE(const uint64_t readAt = esp_timer_get_time();)
  if (!camera_.read(&frame))
    return false;
  CINE_PROFILE(profile_.previewReadUs = uint32_t(esp_timer_get_time() - readAt);)
  if (shouldAbort && shouldAbort(context)) {
    camera_.returnFrame();
    status_.previewInterrupted = true;
    return false;
  }
  if (frame.dmaStartUs < shutterReadyUs_) {
    camera_.returnFrame();
    return false;
  }
  bool ok = frame.format == PIXFORMAT_JPEG &&
            core::cine::validJpeg(frame.buf, frame.len, frame.width, frame.height, 2048, 1536);
  if (ok) {
    CINE_PROFILE(const uint64_t at = esp_timer_get_time();)
    ok = renderIspPreview(frame, shouldAbort, context);
    CINE_PROFILE(profile_.previewUs = uint32_t(esp_timer_get_time() - at);
                 if (ok) cachedFrameUs_ = frame.dmaStartUs;)
  }
  camera_.returnFrame();
  if (ok) {
    CINE_PROFILE(
        ++profile_.frames; const uint64_t done = esp_timer_get_time();
        profile_.previewAgeUs = done >= cachedFrameUs_ ? uint32_t(done - cachedFrameUs_) : 0;
        profile_.previewIntervalUs = lastPreviewDoneUs_ ? uint32_t(done - lastPreviewDoneUs_) : 0;
        lastPreviewDoneUs_ = done;)
  } else if (!status_.previewInterrupted)
    snprintf(status_.message, sizeof(status_.message), "Unexpected sensor frame format");
  return ok;
}
#if ORIGAMI_CINE_PROFILING
void Session::previewDrawn() {
  const uint64_t now = esp_timer_get_time();
  if (cachedFrameUs_ && now >= cachedFrameUs_)
    profile_.previewDrawAgeUs = uint32_t(now - cachedFrameUs_);
}
#endif
void Session::adjustShutter(int delta) {
  if (!status_.open || !shutter_.valid || !delta)
    return;
  // Sample the live hardware result only when handing control to the wheel.
  // Continuous metering itself runs in the sensor; no per-preview I2C polling.
  const bool automatic = autoExposure();
  if (automatic) {
    if (!refreshExposure())
      return;
  }
  int step = int(core::cine::nearestShutter(automatic ? shutter_.actualUs : shutter_.requestedUs));
  int next = delta < -int(core::cine::shutterCount)  ? 0
             : delta > int(core::cine::shutterCount) ? int(core::cine::shutterCount) - 1
                                                     : step + delta;
  if (next < 0)
    next = 0;
  if (next >= int(core::cine::shutterCount))
    next = core::cine::shutterCount - 1;
  if (!automatic && next == step && shutter_.requestedUs == core::cine::shutterSpeedsUs[next])
    return;
  configureExposure(ExposureMode::Manual, core::cine::shutterSpeedsUs[next], shutter_.gainQ4);
}
bool Session::capture() {
  if (!status_.open)
    return false;
  const uint64_t started = esp_timer_get_time();
  if (!shutter_.valid || started < shutterReadyUs_) {
    snprintf(status_.message, sizeof(status_.message),
             shutter_.valid ? "Updating shutter; wait for preview"
                            : "Reopen camera for shutter control");
    return false;
  }
  status_.saved = false;
  CINE_PROFILE(profile_.frameUs = profile_.saveUs = profile_.totalUs = 0;)
  status_.photoPath[0] = status_.metadataPath[0] = 0;
  auto target = photos::prepare();
  if (!target.valid()) {
    snprintf(status_.message, sizeof(status_.message), "Could not create photo");
    return false;
  }
  status_.sequence = target.sequence;
  hal::camera::CameraFrame frame{};
  bool acquired = false;
  for (unsigned i = 0; i < 4; ++i) {
    core::watchdog::feed();
    if (!camera_.read(&frame))
      break;
    if (frame.dmaStartUs + 25000 >= started && frame.dmaStartUs >= shutterReadyUs_) {
      acquired = true;
      break;
    }
    camera_.returnFrame();
  }
  CINE_PROFILE(profile_.frameUs = uint32_t(esp_timer_get_time() - started);)
  if (!acquired) {
    snprintf(status_.message, sizeof(status_.message), "Fresh sensor frame unavailable");
    return false;
  }
  const bool sampleValid = autoExposure() ? refreshExposure() : shutter_.valid;
  const auto exposure = sampleValid ? shutter_ : hal::camera::ShutterInfo{};
  CINE_PROFILE(const uint64_t saveAt = esp_timer_get_time();)
  const bool ok =
      frame.format == PIXFORMAT_JPEG &&
      core::cine::validJpeg(frame.buf, frame.len, frame.width, frame.height, 2048, 1536) &&
      photos::write(target, frame.buf, frame.len);
  camera_.returnFrame();
  CINE_PROFILE(profile_.saveUs = uint32_t(esp_timer_get_time() - saveAt);
               profile_.totalUs = uint32_t(esp_timer_get_time() - started);)
  status_.saved = ok;
  snprintf(status_.photoPath, sizeof(status_.photoPath), "%s", target.path);
  snprintf(status_.metadataPath, sizeof(status_.metadataPath), "/cine/SHOT_%05lu.json",
           (unsigned long)status_.sequence);
  char metadata[1200];
  const int n = snprintf(metadata, sizeof(metadata),
                         "{\"version\":3,\"mode\":\"Cine ISP 3MP\",\"source\":\"sensor ISP "
                         "JPEG\",\"output_width\":2048,\"output_height\":1536,"
                         "\"jpeg_quantization_scale\":%u,"
                         "\"color_profile\":\"sensor-ISP\",\"color_version\":2,"
                         "\"color_mode\":%u,\"color_name\":\"%s\",\"gamma\":\"%s\",\"wb_preset\":1,"
                         "\"shutter\":{\"sample_valid\":%s,\"sample_source\":\"%s\","
                         "\"automatic\":%s,\"ae_level\":%s,\"manual\":%s,"
                         "\"requested_us\":%lu,\"actual_us\":%lu,\"integration_ticks_q4\":%lu,"
                         "\"gain_q4\":%u,\"hts\":%u,\"vts\":%u,\"clock_hz\":%lu},\"saved\":%s}",
                         unsigned(core::cine::jpegQuantizationScale), unsigned(colorMode_),
                         colorName(colorMode_), colorMode_ == ColorMode::Mono
                             ? "sRGB + classic silver" : "sRGB approximation",
                         sampleValid ? "true" : "false",
                         autoExposure() ? "live-after-frame"
                         : sampleValid  ? "locked"
                                        : "unavailable",
                         autoExposure() ? "true" : "false", autoExposure() ? "-1" : "null",
                         exposure.valid && exposure.manual ? "true" : "false",
                         (unsigned long)exposure.requestedUs, (unsigned long)exposure.actualUs,
                         (unsigned long)exposure.ticks, exposure.gainQ4, exposure.hts, exposure.vts,
                         (unsigned long)exposure.clockHz, ok ? "true" : "false");
  const bool metadataOk =
      n > 0 && size_t(n) < sizeof(metadata) &&
      hal::sd::writeFile(status_.metadataPath, reinterpret_cast<const uint8_t *>(metadata), n);
  snprintf(status_.message, sizeof(status_.message),
           ok ? (metadataOk ? "Photo saved" : "Photo saved; metadata failed")
              : "Save failed; partial files retained");
  return ok;
}
#undef CINE_PROFILE
} // namespace cine_camera
} // namespace services
