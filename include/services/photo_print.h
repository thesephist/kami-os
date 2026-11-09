#pragma once
#include <stddef.h>
#include <stdint.h>
namespace services {
namespace photo_print {
constexpr size_t kDestinationCapacity = 192;
enum class State : uint8_t { Idle, Connecting, Uploading, Queued, Failed, Uncertain, Cancelled };
struct Status {
  State state = State::Idle;
  bool running = false;
  uint32_t sequence = 0, bytes = 0, total = 0, revision = 0;
  int httpCode = 0;
  char job[33] = {}, message[64] = {};
};
enum class EndpointState : uint8_t { Unknown, Online, Offline, AuthRequired, NoWifi, Busy };
struct EndpointStatus {
  EndpointState state = EndpointState::Unknown;
  bool checking = false;
  int httpCode = 0;
  uint32_t revision = 0;
};
// UI-task request, background-task HTTP. Shares the print worker: never overlaps uploads.
bool pollEndpoint();
void cancelEndpointPoll();
// True only after the worker has destroyed its HTTP/TLS/SD resources and
// released network admission. Canceling a poll alone does not establish this.
bool readyForCapture();
EndpointStatus endpointStatus();
const char *endpointStateName(EndpointState state);
// UI-task settings and submission. URL is saved in NVS, never compiled credentials.
void destination(char *output, size_t capacity);
bool setDestination(const char *url);
// HTTP Basic password, bound to the current origin. Never returned over serial.
bool setPassword(const char *password);
bool passwordConfigured();
bool validDestination(const char *url);
bool start(const char *photoPath, uint32_t sequence);
void cancel(); // Cooperative; does not free worker-owned resources.
Status status();
const char *stateName(State state);
} // namespace photo_print
} // namespace services
