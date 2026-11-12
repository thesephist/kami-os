// Gated, session-local ISP comparison. No user preference or stock setting writes.
#include "services/cine_camera.h"
#include "core/cine_isp_seed.h"
#include <esp_timer.h>
#include <string.h>
namespace services {
namespace cine_camera {
bool Session::configureBaseline(uint32_t us, unsigned gain, bool settled, bool automatic) {
  IspConfig config;
  config.autoExposure = automatic;
  config.shutterUs = us < 1000 ? 1000 : us > 100000 ? 100000 : us;
  config.gainQ4 = gain < 16 ? 16 : gain > 64 ? 64 : gain;
  config.useSettledExposure = settled;
  return configureIsp(config);
}
bool Session::configureIsp(const IspConfig &config) {
  const auto curve = config.gamma;
  const auto matrix = config.matrix;
  const int thirds = config.exposureThirds;
  const uint32_t us = config.shutterUs;
  const unsigned gain = config.gainQ4;
  const bool fitted = matrix == ColorMatrix::FittedDiagnostic;
  const unsigned vendorSaturation = matrix == ColorMatrix::VendorSaturated ? 1 : 0;
  if (!status_.open || !isIsp(mode_) || unsigned(curve) > unsigned(GammaCurve::Vendor) ||
      unsigned(matrix) > unsigned(ColorMatrix::FittedDiagnostic) ||
      (curve == GammaCurve::Vendor && (!vendorGammaValid_ || thirds != 0)) || thirds < -6 ||
      thirds > 6 || config.aeLevel < -2 || config.aeLevel > 2 || us < 1000 || us > 100000 ||
      gain < 16 || gain > 64)
    return false;
  camera_.returnFrame();
  frameHeld_ = false;
  hal::camera::IspExperiment request;
  request.shutterUs = us;
  request.gainQ4 = gain;
  request.wbMode = wbPreset_;
  request.useSettledExposure = config.useSettledExposure;
  request.autoExposure = config.autoExposure;
  request.aeLevel = config.aeLevel;
  request.autoVts = autoFrameVts_;
  if (curve == GammaCurve::Vendor)
    memcpy(request.gamma, vendorGamma_, 16);
  else if (!core::cine::makeIspGamma(curve == GammaCurve::Filmic, thirds, request.gamma))
    return false;
  // Exact pinned OV3660 sensor_saturation_levels rows 0 and +1.
  // C0/B0 bypass SDE; saturation is implemented by the vendor CMX table.
  static const uint8_t vendor[2][11] = {{29, 96, 3, 12, 120, 132, 125, 107, 18, 1, 152},
                                        {29, 96, 3, 13, 132, 145, 138, 118, 20, 1, 152}};
  memcpy(request.cmx, fitted ? core::cine::ispFittedCmx : vendor[vendorSaturation], 11);
  if (!camera_.configureIspExperiment(request, &ispReadback_)) {
    close();
    snprintf(status_.message, sizeof(status_.message), "ISP experiment rejected; camera closed");
    return false;
  }
  ispConfig_ = config;
  ispConfigured_ = true;
  shutter_ = ispReadback_.shutter;
  if (!autoFrameVts_)
    autoFrameVts_ = shutter_.vts;
  params_.exposureThirds = thirds;
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
  if (!status_.open || pattern_ || (mode_ != Mode::Isp3Mp && mode_ != Mode::IspVga))
    return false;
  if (autoExposure())
    return true;
  return configureBaseline(shutter_.actualUs, shutter_.gainQ4, false, true);
}
const char *Session::whiteBalanceName(unsigned preset) {
  static const char *names[] = {"Auto", "Sunny", "Cloudy", "Office", "Home"};
  return names[preset <= 4 ? preset : 0];
}
void Session::adjustWhiteBalance(int delta) {
  if (!status_.open || (mode_ != Mode::Isp3Mp && mode_ != Mode::IspVga) || !delta)
    return;
  const auto previous = wbPreset_;
  const int64_t step = int64_t(wbPreset_ >= 1 && wbPreset_ <= 4 ? wbPreset_ - 1 : 0) + delta;
  wbPreset_ = 1 + unsigned((step % 4 + 4) % 4);
  if (wbPreset_ == previous)
    return;
  if (!configureBaseline(autoExposure() ? shutter_.actualUs : shutter_.requestedUs, shutter_.gainQ4,
                         false, autoExposure()))
    wbPreset_ = previous;
}
} // namespace cine_camera
} // namespace services
