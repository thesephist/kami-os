#pragma once
#include "config/diagnostics.h"
#include "services/camera_session.h"
#include "core/cine_isp.h"
class JPEGDEC;
namespace services {
namespace cine_camera {
using ColorMode = core::cine::ColorMode;
struct Status {
  bool open = false, saved = false, previewInterrupted = false;
  uint32_t sequence = 0;
  char message[80] = {}, photoPath[64] = {}, metadataPath[64] = {};
};
#if ORIGAMI_CINE_PROFILING
struct Profile {
  uint32_t startupUs = 0, startupPrepareUs = 0, startupInitUs = 0, startupAeUs = 0,
           startupIspUs = 0, startupPreviewUs = 0;
  bool startupAeStable = false;
  uint32_t frames = 0, previewUs = 0, previewReadUs = 0, previewAgeUs = 0, previewDrawAgeUs = 0,
           previewIntervalUs = 0, frameUs = 0, saveUs = 0, totalUs = 0;
};
#endif
// UI-owned, single 3MP ISP/JPEG pipeline. All frame loans end before input dispatch.
class Session {
public:
  Session() = default;
  ~Session() { close(); }
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;
  bool open();
  bool close(bool restoreUi = true);
  bool holdsResources() const { return camera_.backgroundHeld() || camera_.running(); }
  bool preview(bool (*shouldAbort)(void *) = nullptr, void *context = nullptr);
  bool capture();
  void adjustShutter(int delta);
  bool autoExposure() const { return configured_ && exposureMode_ == ExposureMode::Automatic; }
  bool resumeAutoExposure();
  bool refreshExposure();
  const hal::camera::ShutterInfo &shutter() const { return shutter_; }
  void adjustColor(int delta);
  ColorMode colorMode() const { return colorMode_; }
  static const char *colorName(ColorMode preset);
  const Status &status() const { return status_; }
  const uint16_t *pixels() const { return preview_; }
  static constexpr unsigned previewWidth = 224, previewHeight = 168;
#if ORIGAMI_CINE_PROFILING
  const Profile &profile() const { return profile_; }
  void previewDrawn();
#endif
private:
  enum class ExposureMode { Automatic, Manual };
  camera::Session camera_;
  uint16_t *preview_ = nullptr, *previewWork_ = nullptr;
  JPEGDEC *ispDecoder_ = nullptr;
  ColorMode colorMode_ = ColorMode::Sunny;
  ExposureMode exposureMode_ = ExposureMode::Automatic;
  hal::camera::ShutterInfo shutter_;
  uint64_t shutterReadyUs_ = 0;
  uint16_t autoFrameVts_ = 0;
  bool configured_ = false;
  Status status_;
  hal::camera::CineIspReadback ispReadback_;
#if ORIGAMI_CINE_PROFILING
  Profile profile_;
  uint64_t cachedFrameUs_ = 0, lastPreviewDoneUs_ = 0;
#endif
  bool configureExposure(ExposureMode mode, uint32_t shutterUs, unsigned gainQ4);
  void releaseIspPreview();
  bool renderIspPreview(const hal::camera::CameraFrame &frame, bool (*shouldAbort)(void *),
                        void *context);
};
} // namespace cine_camera
} // namespace services
