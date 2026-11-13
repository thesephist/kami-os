#include "apps/cine_camera_app.h"
#include "services/cine_camera.h"
#include "core/cine_controls.h"
#include "services/photo_print.h"
#include "services/vibration.h"
#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "nav_handler.h"
#include "scene/screen.h"
#include "ui/scene.h"
#include "ui/edit_field.h"
#include "ui/cine_camera_view.h"
#include "ui/draw.h"
#include "ui/nav_controls.h"
#include "ui/list_view.h"
#include "config/ui_theme.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
namespace apps {
namespace cine_camera {
namespace {
namespace cine = services::cine_camera;
cine::Session session;
core::cine::WheelControl wheel;
ui::ListView settingsList;
bool menuEditing = false;
enum class View { Menu, Opening, Preview, Error };
View current = View::Menu;
unsigned feedbackStage = 0;
uint32_t feedbackAt = 0;
#if ORIGAMI_CINE_DIAGNOSTICS
bool active = false;
#endif
bool dirty = true;
uint32_t lastInputSample = 0;
// Keep the gap short between reduced-IDCT previews and input handling.
uint32_t requestedAt = 0, lastPreview = 0;
constexpr uint16_t bg = 0xFFFF, fg = 0x0000, muted = 0x632C;
#if ORIGAMI_CINE_DIAGNOSTICS
const char *viewName() {
  return current == View::Preview   ? "preview"
         : current == View::Opening ? "opening"
         : current == View::Error   ? "error"
                                    : "menu";
}
#endif
void invalidate() {
  dirty = true;
  hal::display::markAllDirty();
}
bool close(bool keepReservation = false) {
  const bool ok = session.close(!keepReservation);
  current = ok ? View::Menu : View::Error;
  feedbackStage = 0;
  invalidate();
  return ok;
}
void begin();
void draw(TFT_eSprite &);
void activate() {
#if ORIGAMI_CINE_DIAGNOSTICS
  active = true;
#endif
  feedbackStage = 0;
  begin();
}
void exit() {
  close();
#if ORIGAMI_CINE_DIAGNOSTICS
  active = false;
#endif
}
void cover() { close(); }
void begin() {
  wheel.reset();
  services::photo_print::cancelEndpointPoll();
  current = View::Opening;
  requestedAt = millis();
  invalidate();
  ui::showAppOpeningSplash("Cine camera");
}
void snap() {
  if (current != View::Preview)
    return;
  feedbackStage = 1;
  feedbackAt = millis();
  invalidate();
  draw(hal::display::canvas());
  hal::display::flush();
  const bool saved = session.capture();
  feedbackStage = saved ? 5 : 6;
  feedbackAt = millis();
  invalidate();
  draw(hal::display::canvas());
  hal::display::flush();
}
bool previewShouldAbort(void *) {
  // Match stock Camera: sample/enqueue only, never dispatch with a frame loan.
  const uint32_t now = millis();
  // Expander reads are synchronous I2C: keep taps responsive without delaying a frame.
  if (uint32_t(now - lastInputSample) >= 20u) {
    lastInputSample = now;
    hal::input::service(now);
  }
  return hal::input::buttonADown() || hal::input::buttonBDown() || hal::input::buttonCDown();
}
void tick(uint32_t) {
  const uint32_t now = millis();
  if (wheel.expire(now))
    invalidate();
  if (feedbackStage && feedbackStage != 1 && uint32_t(now - feedbackAt) >= 1800u) {
    feedbackStage = 0;
    invalidate();
  }
  if (current == View::Opening) {
    if (services::photo_print::readyForCapture()) {
      current = session.open() ? View::Preview : View::Error;
      if (current == View::Error)
        session.close();
      lastPreview = millis();
      invalidate();
    } else if (uint32_t(now - requestedAt) > 15000) {
      current = View::Error;
      invalidate();
    }
  } else if (current == View::Preview && uint32_t(now - lastPreview) >= 1u) {
    if (session.preview(previewShouldAbort))
      invalidate();
    lastPreview = millis();
  }
}
void shutterValue(char *text, size_t bytes) {
  if (session.autoExposure()) {
    snprintf(text, bytes, "Auto");
    return;
  }
  if (session.shutter().valid && session.shutter().actualUs)
    snprintf(
        text, bytes, "1/%lu",
        (unsigned long)((1000000 + session.shutter().actualUs / 2) / session.shutter().actualUs));
  else
    snprintf(text, bytes, "Auto");
}
ui::ListItem settingsItem(int index, void *) {
  static char label[64];
  char shutter[20];
  shutterValue(shutter, sizeof(shutter));
  if (index == 0)
    snprintf(label, sizeof(label), "Shutter  %s", shutter);
  else if (index == 1)
    snprintf(label, sizeof(label), "Color  %s",
             cine::Session::colorName(session.colorMode()));
  else
    snprintf(label, sizeof(label), "Back to camera");
  ui::ListItem item;
  item.label = label;
  item.fg = fg;
  item.opensScreen = index == 2;
  item.editing = menuEditing && index == settingsList.selected;
  return item;
}
void build(ui::scene::Scene &view, int16_t w, int16_t h) {
  view.clear();
  ui::scene::setCurrentScene(&view);
  if (current == View::Preview) {
    view.addRect("cine.preview", {0, 0, w, h}, 0);
    auto *gear = view.addBitmap("cine.settings_button", {18, 14, 20, 20},
                                ui::cine_view::settingsMask, 20, 20, 0xffff, 0);
    if (gear)
      gear->bitmapTransparent = true;
    view.addLabel("cine.controls", {52, 196, 120, 20}, "B: PHOTO", 0xffff, 0, 1);
    if (wheel.shown(millis())) {
      const uint16_t ss = wheel.color() ? 0xffff : kUiEdit,
                     color = wheel.color() ? kUiEdit : 0xffff;
      char shutter[20];
      shutterValue(shutter, sizeof(shutter));
      view.addLabel("cine.wheel_shutter_label", {142, 52, 26, 12}, "SS", ss, 0, 1);
      view.addLabel("cine.wheel_color_label", {130, 82, 42, 12}, "Color", color, 0, 1);
      view.addLabel("cine.wheel_shutter_value", {178, 46, 90, 26}, shutter, ss, 0, 2);
      view.addLabel("cine.wheel_color_value", {180, 76, 90, 26},
                    cine::Session::colorName(session.colorMode()), color, 0, 2);
    }
    if (feedbackStage)
      view.addLabel("cine.feedback", {52, 196, 180, 20},
                    feedbackStage == 1   ? "CAPTURE"
                    : feedbackStage == 5 ? "SAVED"
                                         : "SAVE FAILED",
                    0xffff, 0, 1);
    return;
  }
  view.addRect("cine.bg", {0, 38, w, (int16_t)(h - 38)}, bg);
  if (current == View::Menu) {
    settingsList.totalItems = 3;
    settingsList.buildScene(view, 0, 38, w, h - 38, settingsItem, nullptr, kUiSelBg, kUiSelFg, bg,
                            muted, fg);
    return;
  }
  view.addLabel("cine.title", {0, 55, w, 26}, "Cine camera", fg, bg, 2,
                ui::scene::TextAlign::MiddleCenter);
  view.addLabel("cine.status", {8, 96, (int16_t)(w - 16), 50},
                current == View::Opening ? "Preparing camera..." : session.status().message, fg, bg,
                1, ui::scene::TextAlign::MiddleCenter);
  view.addLabel("cine.controls", {0, 212, w, 18}, "A: back", muted, bg, 1,
                ui::scene::TextAlign::MiddleCenter);
}
struct PreviewCanvas {
  TFT_eSprite &surface;
  uint16_t at(int x, int y) const { return surface.readPixel(x, y); }
  uint16_t readPixel(int x, int y) const { return surface.readPixel(x, y); }
  void fillRect(int x, int y, int w, int h, uint16_t ink) { surface.fillRect(x, y, w, h, ink); }
  void pushImage(int x, int y, int w, int h, uint16_t *pixels) {
    const bool swap = surface.getSwapBytes();
    surface.setSwapBytes(true);
    surface.pushImage(x, y, w, h, pixels);
    surface.setSwapBytes(swap);
    (void)previewShouldAbort(nullptr);
  }
};
void draw(TFT_eSprite &canvas) {
  if (!dirty)
    return;
  auto &view = ui::scene::scratchScene();
  build(view, canvas.width(), canvas.height());
  if (current == View::Preview) {
    PreviewCanvas target{canvas};
    ui::cine_view::image(target, session.pixels(), cine::Session::previewWidth,
                         cine::Session::previewHeight, feedbackStage == 1);
    char shutter[20];
    shutterValue(shutter, sizeof(shutter));
    ui::cine_view::chrome(target, cine::Session::colorName(session.colorMode()), shutter,
                          feedbackStage * 1000 +
                              (millis() - feedbackAt < 999 ? millis() - feedbackAt : 999),
                          wheel.color(), wheel.shown(millis()));
#if ORIGAMI_CINE_PROFILING
    session.previewDrawn();
#endif
  } else
    view.render(canvas);
  dirty = false;
}
AppPresentation presentation() {
  return current == View::Preview ? AppPresentation{true, true, AppPresentation::StatusBar::Hidden}
                                  : AppPresentation{};
}

void input(hal::input::InputEvent e) {
  using ET = hal::input::EventType;
  if (e.type == ET::ButtonA_Tap && current == View::Preview) {
    current = View::Menu;
    menuEditing = false;
    settingsList.totalItems = 3;
    settingsList.selected = wheel.color() ? 1 : 0;
    invalidate();
    return;
  }
  if (current == View::Menu) {
    if (ui::nav::isBackTap(e)) {
      if (menuEditing)
        menuEditing = false;
      else if (session.status().open)
        current = View::Preview;
      else
        begin();
      invalidate();
      return;
    }
    if (e.type == ET::Scroll) {
      if (menuEditing) {
        if (settingsList.selected == 0)
          session.adjustShutter(-int(e.delta));
        else
          session.adjustColor(int(e.delta));
        if (!session.status().open)
          current = View::Error;
      } else
        settingsList.scroll(e.delta);
      invalidate();
    } else if (ui::nav::isEnterTap(e)) {
      if (settingsList.selected == 2) {
        menuEditing = false;
        if (session.status().open)
          current = View::Preview;
        else
          begin();
      } else
        menuEditing = !menuEditing;
      invalidate();
    }
    return;
  }
  if (ui::nav::isBackTap(e)) {
    NavHandler::instance().pop();
    return;
  }
  if (current != View::Preview)
    return;
  if (e.type == ET::ButtonB_Tap)
    snap();
  else if (ui::nav::isEnterTap(e)) {
    services::vibration::navigationFeedback(services::vibration::Cue::Select);
    wheel.toggle(millis());
    invalidate();
  } else if (e.type == ET::Scroll) {
    if (!e.delta)
      return;
    if (wheel.color())
      session.adjustColor(int(e.delta));
    else
      session.adjustShutter(-int(e.delta));
    wheel.show(millis());
    if (!session.status().open)
      current = View::Error;
    invalidate();
  }
}
bool holdWheel() {
  // In every Cine view this gesture is consumed instead of putting Kami to sleep.
  if (session.status().open && session.resumeAutoExposure()) {
    wheel.reset();
    wheel.show(millis());
    settingsList.selected = 0;
    menuEditing = false;
    services::vibration::navigationFeedback(services::vibration::Cue::Select);
    invalidate();
  }
  return true;
}
core::health::RuntimePolicy policy() {
  bool busy = current == View::Preview || current == View::Opening || session.holdsResources();
  auto p = core::health::whileBusy(busy);
  p.allowBackgroundUploads = !busy;
  p.allowAutomaticUpdate = !busy;
  return p;
}
const ::scene::Screen screen = {"Cine camera", invalidate, nullptr, tick,  input,
                                nullptr,       draw,       "main",  build, &bg};
const ::scene::Screen *const screens[] = {&screen};
} // namespace
App app = [] {
  App a = SCENE_APP_CATALOG_LIFECYCLE_HOOKS("Cine camera", "cine-camera", &screen, activate,
                                            invalidate, exit, "src/apps/cine_camera_app.cpp",
                                            screens, presentation, policy);
  a.onCover = cover;
  a.onPowerHold = holdWheel;
  return a;
}();
#if ORIGAMI_CINE_DIAGNOSTICS
bool handleSerialCommand(const char *line, Print &output) {
  if (strncmp(line, "cine ", 5))
    return false;
  if (!strcmp(line, "cine open")) {
    NavHandler::instance().push(&app);
    return true;
  }
  if (!strcmp(line, "cine status")) {
    const auto &s = session.status();
    output.printf("[cine] status active=%u state=%s sequence=%lu saved=%u photo=%s meta=%s\n",
                  active, viewName(), (unsigned long)s.sequence, s.saved, s.photoPath,
                  s.metadataPath);
    return true;
  }
  if (!active) {
    output.println("[cine] rejected: open Cine camera first");
    return true;
  }
  if (!strcmp(line, "cine controls")) {
    output.printf("[cine-controls] mode=%s popup=%u\n", wheel.color() ? "color" : "shutter",
                  wheel.visible());
    return true;
  }
  if (!strcmp(line, "cine shutter")) {
    if (session.autoExposure())
      session.refreshExposure();
    const auto &s = session.shutter();
    output.printf("[cine] shutter valid=%u manual=%u requested_us=%lu actual_us=%lu gain_q4=%u "
                  "frame_us=%lu\n",
                  s.valid, s.manual, (unsigned long)s.requestedUs, (unsigned long)s.actualUs,
                  s.gainQ4, (unsigned long)s.frameUs);
    return true;
  }
  if (!strcmp(line, "cine color")) {
    output.printf("[cine] color=%u name=%s\n", unsigned(session.colorMode()),
                  cine::Session::colorName(session.colorMode()));
    return true;
  }
#if ORIGAMI_CINE_PROFILING
  if (!strcmp(line, "cine startup")) {
    const auto &p = session.profile();
    output.printf("[cine-startup] total_us=%lu prepare_us=%lu init_us=%lu ae_us=%lu isp_us=%lu "
                  "first_preview_us=%lu ae_stable=%u\n",
                  (unsigned long)p.startupUs, (unsigned long)p.startupPrepareUs,
                  (unsigned long)p.startupInitUs, (unsigned long)p.startupAeUs,
                  (unsigned long)p.startupIspUs, (unsigned long)p.startupPreviewUs,
                  p.startupAeStable);
    return true;
  }
  if (!strcmp(line, "cine capture-status")) {
    const auto &p = session.profile();
    output.printf("[cine-capture] frame_us=%lu save_us=%lu total_us=%lu\n",
                  (unsigned long)p.frameUs, (unsigned long)p.saveUs, (unsigned long)p.totalUs);
    return true;
  }
  if (!strcmp(line, "cine latency")) {
    const auto &p = session.profile();
    output.printf("[cine] latency frames=%lu read_us=%lu render_us=%lu dma_age_us=%lu "
                  "drawn_age_us=%lu interval_us=%lu\n",
                  (unsigned long)p.frames, (unsigned long)p.previewReadUs,
                  (unsigned long)p.previewUs, (unsigned long)p.previewAgeUs,
                  (unsigned long)p.previewDrawAgeUs, (unsigned long)p.previewIntervalUs);
    return true;
  }
#endif
  output.println("[cine] use open/status/controls/shutter/wb/startup/latency/capture-status");
  return true;
}
#endif
} // namespace cine_camera
} // namespace apps
