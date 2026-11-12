#include "services/cine_camera.h"
#include <esp_timer.h>
#include <stdio.h>
namespace services {
namespace cine_camera {
bool Session::configureExposure(ExposureMode mode, uint32_t us, unsigned gain) {
  if (!status_.open)
    return false;
  // Auto may legitimately be below the manual ladder. These request fields are
  // bounded placeholders in Auto; the HAL preserves the live integration ticks.
  us = us < 1000 ? 1000 : us > 100000 ? 100000 : us;
  gain = gain < 16 ? 16 : gain > 64 ? 64 : gain;
  camera_.returnFrame();
  hal::camera::CineIspRequest request;
  request.shutterUs = us;
  request.gainQ4 = gain;
  request.wbMode = uint8_t(wbPreset_);
  request.autoExposure = mode == ExposureMode::Automatic;
  request.autoVts = autoFrameVts_;
  if (!camera_.configureCineIsp(request, &ispReadback_)) {
    close();
    snprintf(status_.message, sizeof(status_.message), "Camera ISP update failed; camera closed");
    return false;
  }
  exposureMode_ = mode;
  configured_ = true;
  shutter_ = ispReadback_.shutter;
  if (!autoFrameVts_)
    autoFrameVts_ = shutter_.vts;
  shutterReadyUs_ = esp_timer_get_time() + uint64_t(shutter_.frameUs) * 2;
  status_.saved = false;
  return true;
}
bool Session::refreshExposure() {
  if (!status_.open || !autoExposure())
    return false;
  hal::camera::ShutterInfo live{};
  if (!hal::camera::readJpegShutter(&live) || !live.valid || live.manual)
    return false;
  shutter_ = live;
  return true;
}
bool Session::resumeAutoExposure() {
  if (!status_.open)
    return false;
  return autoExposure() ||
         configureExposure(ExposureMode::Automatic, shutter_.actualUs, shutter_.gainQ4);
}
const char *Session::whiteBalanceName(WhiteBalance preset) {
  static const char *names[] = {"Sunny", "Cloudy", "Office", "Home"};
  const unsigned value = unsigned(preset);
  return names[value >= 1 && value <= 4 ? value - 1 : 0];
}
void Session::adjustWhiteBalance(int delta) {
  if (!status_.open || !delta)
    return;
  const auto previous = wbPreset_;
  const int64_t step = int64_t(unsigned(wbPreset_) - 1) + delta;
  wbPreset_ = WhiteBalance(1 + unsigned((step % 4 + 4) % 4));
  if (wbPreset_ == previous)
    return;
  if (!configureExposure(exposureMode_, autoExposure() ? shutter_.actualUs : shutter_.requestedUs,
                         shutter_.gainQ4))
    wbPreset_ = previous;
}
} // namespace cine_camera
} // namespace services
