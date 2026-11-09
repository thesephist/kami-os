#include "apps/photo_printer_app.h"
#include "apps/camera_menu_app.h"
#include "services/photo_print.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "hal/hal_display.h"
#include "nav_handler.h"
#include "scene/screen.h"
#include "ui/nav_controls.h"
#include "ui/scene.h"
#include "ui/edit_field.h"

namespace apps {
namespace photo_printer {
namespace {
namespace printing = services::photo_print;
bool active = false, dirty = true, capturing = false, editing = false, invalidDestination = false;
int selected = 0;
size_t cursor = 0;
uint32_t lastRevision = 0, lastEndpointRevision = 0, lastEndpointPoll = 0;
bool pollImmediately = true, capturePending = false;
uint32_t captureRequestedMs = 0;
char destination[printing::kDestinationCapacity] = {}, draft[printing::kDestinationCapacity] = {};
char detail[64] = "Press the wheel to take a photo";
services::photos::Target photo;
constexpr uint16_t bg = 0xFFFF, fg = 0x0000, muted = 0x632C;
constexpr char characters[] =
    " 0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.:/-_[]%~";
void invalidate() {
  dirty = true;
  hal::display::markAllDirty();
}
void activate() {
  active = true;
  pollImmediately = true;
  capturePending = false;
  capturing = editing = false;
  selected = 0;
  photo = {};
  printing::destination(destination, sizeof(destination));
  snprintf(detail, sizeof(detail), "Press the wheel to take a photo");
  invalidate();
}
void resume() {
  pollImmediately = true;
  if (capturing) {
    capturing = false;
    photo = apps::camera_menu::takeTutorialPhoto();
    if (photo.valid()) {
      bool started = printing::start(photo.path, photo.sequence);
      snprintf(detail, sizeof(detail), "%s",
               started ? "Connecting over Wi-Fi..." : "Could not start. Photo kept on SD.");
      Serial.printf("[photo-printer] captured sequence=%lu path=%s transport=wifi\n",
                    (unsigned long)photo.sequence, photo.path);
    } else
      snprintf(detail, sizeof(detail), "Photo cancelled. Press to try again.");
  }
  invalidate();
}
void cover() {
  capturePending = false;
  printing::cancelEndpointPoll();
}
void exitApp() {
  active = false;
  capturePending = false;
  printing::cancel();
}
void tick(uint32_t) {
  const uint32_t now = millis(); // Input may be newer than the frame timestamp supplied to tick.
  // The camera stops Wi-Fi. Cancellation is cooperative: never navigate into
  // camera setup while a status worker can still be inside DNS/TCP/TLS.
  if (capturePending) {
    if (printing::readyForCapture()) {
      capturePending = false;
      capturing = true;
      snprintf(detail, sizeof(detail), "B: take photo   LB: cancel");
      apps::camera_menu::beginTutorialCapture();
      return;
    }
    if (uint32_t(now - captureRequestedMs) >= 15000) {
      capturePending = false;
      snprintf(detail, sizeof(detail), "Printer check busy. Press to try again.");
      invalidate();
    } else
      return;
  }
  auto status = printing::status();
  if (active && !capturing && !editing && !status.running &&
      (pollImmediately || uint32_t(now - lastEndpointPoll) >= 3000)) {
    printing::pollEndpoint();
    lastEndpointPoll = now;
    pollImmediately = false;
  }
  auto endpoint = printing::endpointStatus();
  if (endpoint.revision != lastEndpointRevision) {
    lastEndpointRevision = endpoint.revision;
    invalidate();
  }
  if (status.revision != lastRevision) {
    lastRevision = status.revision;
    if (status.sequence && !capturing)
      snprintf(detail, sizeof(detail), "%s", status.message);
    invalidate();
  }
}
void build(ui::scene::Scene &view, int16_t width, int16_t height) {
  view.clear();
  view.addRect("printer.bg", {0, 38, width, (int16_t)(height - 38)}, bg);
  view.addLabel("printer.title", {0, 48, width, 26},
                editing ? "Printer destination" : "Photo Printer", fg, bg, 2,
                ui::scene::TextAlign::MiddleCenter);
  if (editing) {
    size_t start = cursor > 20 ? cursor - 20 : 0;
    char excerpt[30] = {};
    snprintf(excerpt, sizeof(excerpt), "%.28s", draft + start);
    char position[48];
    snprintf(position, sizeof(position), "Character %u: %c", unsigned(cursor + 1),
             draft[cursor] ? draft[cursor] : '+');
    view.addLabel("printer.url.edit", {5, 90, (int16_t)(width - 10), 25}, excerpt, fg, bg, 1,
                  ui::scene::TextAlign::MiddleCenter);
    view.addLabel("printer.url.cursor", {0, 126, width, 22}, position, fg, bg, 1,
                  ui::scene::TextAlign::MiddleCenter);
    view.addLabel("printer.edit.hint", {0, 158, width, 16}, "Turn: character  Press: next", muted,
                  bg, 1, ui::scene::TextAlign::TopCenter);
    view.addLabel("printer.edit.delete", {0, 179, width, 16},
                  invalidDestination ? "Use http(s)://; no ? or @ allowed"
                                     : "Choose space to remove a character",
                  muted, bg, 1, ui::scene::TextAlign::TopCenter);
    view.addLabel("printer.controls", {0, 208, width, 18}, "RB: save   LB: cancel", muted, bg, 1,
                  ui::scene::TextAlign::TopCenter);
    return;
  }
  auto status = printing::status();
  const char *title = capturePending                               ? "Preparing camera..."
                      : status.running                             ? "Sending over Wi-Fi..."
                      : status.state == printing::State::Queued    ? "Sent to printer!"
                      : status.state == printing::State::Uncertain ? "Check printer"
                      : status.state == printing::State::Failed    ? "Printer unavailable"
                                                                   : "Snap. Dither. Print.";
  view.addLabel("printer.state", {0, 79, width, 23}, title, fg, bg, 1,
                ui::scene::TextAlign::MiddleCenter);
  view.addLabel("printer.detail", {8, 103, (int16_t)(width - 16), 24}, detail, muted, bg, 1,
                ui::scene::TextAlign::MiddleCenter);
  auto endpoint = printing::endpointStatus();
  const char *live =
      endpoint.state == printing::EndpointState::Online         ? "Printer online"
      : endpoint.state == printing::EndpointState::AuthRequired ? "Printer: password needed"
      : endpoint.state == printing::EndpointState::NoWifi       ? "Printer: Wi-Fi disconnected"
      : endpoint.state == printing::EndpointState::Offline      ? "Printer unavailable"
      : endpoint.checking                                       ? "Printer: checking..."
                                                                : "Printer: waiting to check";
  const uint16_t liveColor = endpoint.state == printing::EndpointState::Online ? 0x03E0
                             : endpoint.state == printing::EndpointState::Offline ||
                                     endpoint.state == printing::EndpointState::AuthRequired
                                 ? 0xC800
                                 : muted;
  view.addRect("printer.live.dot", {16, 122, 6, 6}, liveColor);
  view.addLabel("printer.live", {28, 117, (int16_t)(width - 42), 16}, live, liveColor, bg, 1,
                ui::scene::TextAlign::MiddleCenter);
  ui::editField(view, false, "printer.take", {14, 134, (int16_t)(width - 28), 28}, "Take & print",
                selected == 0, false, true, 2);
  ui::editField(view, false, "printer.destination", {14, 168, (int16_t)(width - 28), 26},
                "Printer destination", selected == 1, false, true, 1);
  char shortUrl[37];
  snprintf(shortUrl, sizeof(shortUrl), "%.35s", destination);
  view.addLabel("printer.url", {0, 199, width, 15}, shortUrl, muted, bg, 1,
                ui::scene::TextAlign::TopCenter);
  view.addLabel("printer.controls", {0, 220, width, 15}, "Turn: choose  Press: open  LB: back",
                muted, bg, 1, ui::scene::TextAlign::TopCenter);
}
void draw(TFT_eSprite &canvas) {
  if (!dirty)
    return;
  auto &view = ui::scene::scratchScene();
  build(view, canvas.width(), canvas.height());
  view.render(canvas);
  dirty = false;
}
void editInput(hal::input::InputEvent event) {
  using ET = hal::input::EventType;
  if (ui::nav::isBackTap(event)) {
    editing = false;
    invalidate();
    return;
  }
  if (event.type == ET::ButtonB_Tap) {
    if (printing::setDestination(draft)) {
      printing::destination(destination, sizeof(destination));
      editing = false;
      snprintf(detail, sizeof(detail), "Printer destination saved");
    } else {
      invalidDestination = true;
      snprintf(detail, sizeof(detail), "Use a valid http:// or https:// URL");
    }
    invalidate();
    return;
  }
  if (ui::nav::isEnterTap(event)) {
    cursor = (cursor + 1) % (strlen(draft) + 1);
    invalidate();
    return;
  }
  size_t length = strlen(draft);
  if (event.type == ET::KeyBackspace && cursor > 0) {
    memmove(draft + cursor - 1, draft + cursor, length - cursor + 1);
    --cursor;
    invalidate();
    return;
  }
  if (event.type == ET::TextInput && event.key >= 33 && event.key < 127 &&
      cursor < sizeof(draft) - 2) {
    if (cursor == length)
      draft[cursor + 1] = 0;
    draft[cursor++] = char(event.key);
    invalidate();
    return;
  }
  if (event.type != ET::Scroll || !event.delta)
    return;
  if (cursor >= sizeof(draft) - 2)
    return;
  const char *current = draft[cursor] ? strchr(characters, draft[cursor]) : characters;
  int index = current ? int(current - characters) : 0;
  index = ((index + event.delta) % int(sizeof(characters) - 1) + int(sizeof(characters) - 1)) %
          int(sizeof(characters) - 1);
  if (characters[index] == ' ') {
    if (cursor < length)
      memmove(draft + cursor, draft + cursor + 1, length - cursor);
  } else {
    if (cursor == length)
      draft[cursor + 1] = 0;
    draft[cursor] = characters[index];
  }
  invalidate();
}
void input(hal::input::InputEvent event) {
  if (editing) {
    editInput(event);
    return;
  }
  if (ui::nav::isBackTap(event)) {
    NavHandler::instance().pop();
    return;
  }
  if (capturePending || printing::status().running)
    return;
  if (event.type == hal::input::EventType::Scroll) {
    selected = event.delta > 0 ? 1 : 0;
    invalidate();
    return;
  }
  if (!ui::nav::isEnterTap(event))
    return;
  printing::cancelEndpointPoll();
  if (selected == 1) {
    snprintf(draft, sizeof(draft), "%s", destination);
    cursor = 0;
    editing = true;
    invalidDestination = false;
    invalidate();
    return;
  }
  capturePending = true;
  captureRequestedMs = millis();
  snprintf(detail, sizeof(detail), "Finishing printer check...");
  invalidate();
}
core::health::RuntimePolicy policy() {
  bool busy = capturePending || capturing || printing::status().running;
  auto result = core::health::whileBusy(busy);
  result.allowBackgroundUploads = !busy;
  result.allowAutomaticUpdate = !busy;
  return result;
}
const ::scene::Screen screen = {"Photo Printer", invalidate, nullptr, tick,  input,
                                nullptr,         draw,       "main",  build, &bg};
const ::scene::Screen *const screens[] = {&screen};
} // namespace
App app = [] {
  App result = SCENE_APP_CATALOG_LIFECYCLE_HOOKS(
      "Photo Printer", "photo-printer", &screen, activate, resume, exitApp,
      "src/apps/photo_printer_app.cpp", screens, nullptr, policy);
  result.onCover = cover;
  return result;
}();
bool handleSerialCommand(const char *line, Print &output) {
  if (strncmp(line, "printer ", 8))
    return false;
  if (!strcmp(line, "printer password")) {
    output.printf("[photo-printer] password configured=%u\n",
                  printing::passwordConfigured() ? 1u : 0u);
    return true;
  }
  if (!strcmp(line, "printer password-clear")) {
    output.println(printing::setPassword("") ? "[photo-printer] password saved"
                                             : "[photo-printer] rejected reason=busy");
    return true;
  }
  if (!strncmp(line, "printer password ", 17)) {
    bool ok = printing::setPassword(line + 17);
    output.println(ok ? "[photo-printer] password saved"
                      : "[photo-printer] rejected reason=invalid_password_or_busy");
    return true;
  }
  if (!strcmp(line, "printer destination")) {
    char url[printing::kDestinationCapacity];
    printing::destination(url, sizeof(url));
    output.printf("[photo-printer] destination=%s\n", url);
    return true;
  }
  if (!strncmp(line, "printer destination ", 20)) {
    bool ok = printing::setDestination(line + 20);
    if (ok) {
      printing::destination(destination, sizeof(destination));
      invalidate();
    }
    output.println(ok ? "[photo-printer] destination saved"
                      : "[photo-printer] rejected reason=invalid_destination_or_busy");
    return true;
  }
  if (!strcmp(line, "printer endpoint")) {
    auto endpoint = printing::endpointStatus();
    output.printf("[photo-printer] endpoint state=%s checking=%u code=%d revision=%lu\n",
                  printing::endpointStateName(endpoint.state), endpoint.checking ? 1 : 0,
                  endpoint.httpCode, (unsigned long)endpoint.revision);
    return true;
  }
  if (!strcmp(line, "printer status")) {
    auto status = printing::status();
    output.printf("[photo-printer] status active=%u state=%s sequence=%lu path=%s transport=wifi "
                  "bytes=%lu total=%lu code=%d job=%s\n",
                  active ? 1u : 0u,
                  capturePending ? "preparing"
                  : capturing    ? "capturing"
                                 : printing::stateName(status.state),
                  (unsigned long)status.sequence, photo.valid() ? photo.path : "-",
                  (unsigned long)status.bytes, (unsigned long)status.total, status.httpCode,
                  status.job);
    return true;
  }
  output.println("[photo-printer] rejected reason=use_status_destination_or_password");
  return true;
}
} // namespace photo_printer
} // namespace apps
