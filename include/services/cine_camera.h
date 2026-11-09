#pragma once
#include "services/camera_session.h"
#include "services/camera_settings.h"
#include "core/cine_tone.h"
class JPEGDEC;
namespace services {
namespace cine_camera {
// Isp3Mp is the app baseline; other modes remain explicit USB research tools.
enum class Mode : uint8_t { RawBinned, Stock, Isp3Mp, RawNative, IspVga };
enum class GammaCurve : uint8_t { Srgb, Filmic, Vendor };
enum class ColorMatrix : uint8_t { VendorNeutral, VendorSaturated, FittedDiagnostic };
struct IspConfig {
  GammaCurve gamma = GammaCurve::Srgb;
  ColorMatrix matrix = ColorMatrix::VendorSaturated;
  int exposureThirds = 0;
  uint32_t shutterUs = 16667;
  unsigned gainQ4 = 16;
  bool useSettledExposure = false;
};
struct Status {
  uint32_t startupUs = 0, startupPrepareUs = 0, startupInitUs = 0, startupAeUs = 0,
           startupIspUs = 0, startupPreviewUs = 0;
  bool startupAeStable = false;
  bool open = false, raw = false, saved = false, zeroShutterLag = false;
  unsigned frames = 0;
  uint32_t sequence = 0;
  uint32_t previewUs = 0, previewReadUs = 0, previewAgeUs = 0, previewDrawAgeUs = 0,
           previewIntervalUs = 0, meterUs = 0, frameAgeUs = 0, frameUs = 0, toneUs = 0,
           encodeUs = 0, saveUs = 0, totalUs = 0;
  char message[80] = {}, cinePath[64] = {}, neutralPath[64] = {}, rawPath[64] = {},
       metadataPath[64] = {};
};
// UI-owned session. Preallocates image buffers; no network or extra task.
class Session {
public:
  Session() = default;
  ~Session() { close(); }
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;
  bool open(Mode mode, bool testPattern = false);
  // Keep the peripheral reservation across settings/mode changes. Release on exit.
  bool close(bool restoreUi = true);
  bool holdsResources() const { return camera_.backgroundHeld() || camera_.running(); }
  bool preview();
  void previewDrawn(); // DMA-start age when the UI has composed the preview.
  void retainPreviewRaw(bool enabled) {
    retainPreviewRaw_ = enabled;
    cacheReady_ = false;
  }
  void referencePreview(bool enabled); // Gated same-settings A/B diagnostic.
  bool usesReferencePreview() const { return referencePreview_; }
  bool capture(bool saveRaw = false);
  bool benchmarkIspResize();
  bool benchmarkResolution();     // Gated same-held-RAW resolution experiment.
  void adjustExposure(int delta); // Diagnostic rendering gain; wheel uses shutter.
  void adjustShutter(int delta);
  const hal::camera::ShutterInfo &shutter() const { return shutter_; }
  void adjustWhiteBalance(int delta);
  unsigned whiteBalance() const { return wbPreset_; }
  static const char *whiteBalanceName(unsigned preset);
  void setFilmic(bool enabled);
  bool configureIsp(const IspConfig &config);
  const hal::camera::IspReadback &ispReadback() const { return ispReadback_; }
  void remeter();
  bool verifyPattern() const;
  const core::cine::Metering &metering() const { return metering_; }
  const Status &status() const { return status_; }
  const core::cine::Parameters &parameters() const { return params_; }
  const uint16_t *pixels() const { return preview_; }
  Mode mode() const { return mode_; }
  static constexpr unsigned previewWidth = 224, previewHeight = 168;

private:
  camera::Session camera_;
  camera::Settings vanilla_;
  core::cine::ToneMap *tone_ = nullptr;
  core::cine::Parameters params_;
  uint8_t *raw_ = nullptr, *rawOwned_ = nullptr, *bgr_ = nullptr, *jpeg_ = nullptr;
  uint16_t *preview_ = nullptr;
  uint16_t *previewWork_ = nullptr;
  JPEGDEC *ispDecoder_ = nullptr;
  size_t rawBytes_ = 0;
  unsigned width_ = 0, height_ = 0, outWidth_ = 512, outHeight_ = 384;
  Mode mode_ = Mode::RawBinned;
  uint8_t wbPreset_ = 1;
  hal::camera::ShutterInfo shutter_;
  uint64_t shutterReadyUs_ = 0;
  core::cine::Metering metering_;
  bool pattern_ = false, cacheReady_ = false, meterPending_ = false, frameHeld_ = false;
  uint64_t cachedFrameUs_ = 0, lastPreviewDoneUs_ = 0;
  bool referencePreview_ = false, retainPreviewRaw_ = false;
  Status status_;
  uint8_t vendorGamma_[16] = {};
  bool vendorGammaValid_ = false;
  IspConfig ispConfig_;
  bool ispConfigured_ = false;
  hal::camera::IspReadback ispReadback_;
  bool configureBaseline(uint32_t shutterUs, unsigned gainQ4, bool settled = false);
  void configure();
  void releaseIspPreview();
  bool renderIspPreview(const hal::camera::CameraFrame &frame);
  bool renderRaw();
  bool saveRender(const char *path, bool reuse = false);
};
bool isIsp(Mode mode);
const char *modeName(Mode mode);
} // namespace cine_camera
} // namespace services
