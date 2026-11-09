#include "services/photo_print.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <atomic>
#include <ctype.h>
#include <string.h>
#include "core/store.h"
#include "hal/hal_sd.h"
#include "hal/hal_tasks.h"
#include "hal/hal_wifi.h"
#include "services/background_network.h"
#include "services/agent_coordination.h"
#include "services/tls_trust.h"

namespace services {
namespace photo_print {
namespace {
constexpr char kDefault[] = "https://tinkertreat.party";
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
Status current;
EndpointStatus endpoint;
std::atomic<bool> probing{false};
background_network::PeriodicWork probeNetwork; // UI admission transfers to the worker.
std::atomic<bool> cancelled{false}, workerActive{false};
char savedDestination[kDestinationCapacity] = {};
struct Job {
  char path[64], url[kDestinationCapacity + 80];
  uint32_t sequence;
} job;
struct Authentication {
  char origin[kDestinationCapacity] = {}, password[96] = {};
};
void originOf(const char *url, char *out, size_t capacity) {
  const char *host = strstr(url, "://");
  if (!host) {
    out[0] = 0;
    return;
  }
  host += 3;
  const char *end = strchr(host, '/');
  size_t length = end ? size_t(end - url) : strlen(url);
  if (length >= capacity) {
    out[0] = 0;
    return;
  }
  memcpy(out, url, length);
  out[length] = 0;
}
bool loadPassword(const char *url, char *output, size_t capacity) {
  output[0] = 0;
  Authentication auth;
  char origin[kDestinationCapacity];
  originOf(url, origin, sizeof(origin));
  bool ok = core::store::getBlob("photo_print", "auth", &auth, sizeof(auth)) &&
            memchr(auth.origin, 0, sizeof(auth.origin)) &&
            memchr(auth.password, 0, sizeof(auth.password)) && origin[0] &&
            !strcasecmp(origin, auth.origin) && auth.password[0];
  if (ok)
    snprintf(output, capacity, "%s", auth.password);
  memset(&auth, 0, sizeof(auth));
  return ok;
}
void publish(State state, const char *message, bool running, int code = 0,
             const char *receipt = "") {
  portENTER_CRITICAL(&mux);
  current.state = state;
  current.running = running;
  current.httpCode = code;
  ++current.revision;
  snprintf(current.message, sizeof(current.message), "%s", message);
  snprintf(current.job, sizeof(current.job), "%s", receipt);
  portEXIT_CRITICAL(&mux);
}
// Match the existing device_fs TLS adapter: keep writes below a TCP segment
// and pace them so TLS/lwIP buffers drain before the next allocation. Device
// traces showed 1 KB bursts leave <1.5 KB contiguous RAM and time out.
template <class Transport> class PrintClient final : public Transport {
public:
  size_t write(const uint8_t *data, size_t length) override {
    size_t sent = 0;
    while (sent < length && !cancelled.load()) {
      const size_t chunk = length - sent < 512 ? length - sent : 512;
      const uint32_t began = millis();
      const size_t written = Transport::write(data + sent, chunk);
#ifdef ORIGAMI_DEV_SERIAL_INPUT
      Serial.printf("[photo-print-io] write requested=%u sent=%u elapsed=%lu heap=%u largest=%u\n",
                    unsigned(chunk), unsigned(written), (unsigned long)(millis() - began),
                    ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
      if (!written || written > chunk)
        break;
      sent += written;
      delay(10);
    }
    return sent;
  }
};
class PhotoStream final : public Stream {
public:
  hal::sd::StreamReader file;
  uint32_t started = millis();
  size_t consumed = 0;
  ~PhotoStream() { hal::sd::closeStream(&file); }
  bool stopped() const { return cancelled.load() || uint32_t(millis() - started) > 45000; }
  int available() override { return stopped() ? -1 : int(file.size - file.position); }
  int peek() override { return -1; }
  int read() override {
    uint8_t b;
    return readBytes(&b, 1) == 1 ? b : -1;
  }
  void flush() override {}
  size_t write(uint8_t) override { return 0; }
  size_t readBytes(char *buffer, size_t length) override {
    if (stopped())
      return 0;
    const uint32_t began = millis();
    size_t n = hal::sd::readStream(&file, reinterpret_cast<uint8_t *>(buffer), length);
#ifdef ORIGAMI_DEV_SERIAL_INPUT
    Serial.printf("[photo-print-io] read requested=%u got=%u elapsed=%lu age=%lu\n",
                  unsigned(length), unsigned(n), (unsigned long)(millis() - began),
                  (unsigned long)(millis() - started));
#endif
    consumed += n;
    portENTER_CRITICAL(&mux);
    current.bytes = consumed;
    portEXIT_CRITICAL(&mux);
    delay(1);
    return n;
  }
  size_t readBytes(uint8_t *buffer, size_t length) override {
    return readBytes(reinterpret_cast<char *>(buffer), length);
  }
};
class ReplyStream final : public Stream {
public:
  char bytes[2049] = {};
  size_t used = 0;
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *data, size_t length) override {
    if (length > sizeof(bytes) - 1 - used)
      return 0;
    memcpy(bytes + used, data, length);
    used += length;
    bytes[used] = 0;
    return length;
  }
};
void publishEndpoint(EndpointState state, int code = 0) {
  portENTER_CRITICAL(&mux);
  if (!cancelled.load()) {
    endpoint.state = state;
    endpoint.httpCode = code;
  }
  endpoint.checking = false;
  ++endpoint.revision;
  portEXIT_CRITICAL(&mux);
}
void endpointWorker() {
  agent_coordination::TransportPause agent;
  if (cancelled.load()) {
    publishEndpoint(EndpointState::Unknown);
    return;
  }
  if (!hal::wifi::connected()) {
    publishEndpoint(EndpointState::NoWifi);
    return;
  }
  if (!agent.acquire(1000)) {
    publishEndpoint(EndpointState::Busy);
    return;
  }
  if (cancelled.load()) {
    publishEndpoint(EndpointState::Unknown);
    return;
  }
  HTTPClient http;
  PrintClient<WiFiClient> plain;
  PrintClient<WiFiClientSecure> secure;
  const bool tls = !strncmp(job.url, "https://", 8);
  if (tls)
    services::tls::useRootBundle(secure);
  http.setConnectTimeout(2000);
  http.setTimeout(2000);
  http.setReuse(false);
  const bool ready = tls ? http.begin(secure, job.url) : http.begin(plain, job.url);
  int code = 0;
  if (ready) {
    char password[96] = {};
    if (loadPassword(job.url, password, sizeof(password)))
      http.setAuthorization("", password);
    memset(password, 0, sizeof(password));
    code = http.GET();
  }
  http.end();
  publishEndpoint(code >= 200 && code < 300    ? EndpointState::Online
                  : code == 401 || code == 403 ? EndpointState::AuthRequired
                                               : EndpointState::Offline,
                  code);
#ifdef ORIGAMI_DEV_SERIAL_INPUT
  Serial.printf("[photo-printer] endpoint code=%d state=%s\n", code,
                endpointStateName(endpointStatus().state));
#endif
}
void worker(void *) {
  if (probing.load()) {
    endpointWorker();
  } else {
    // Camera cleanup may restore Wi-Fi asynchronously. Let the manager rejoin
    // without blocking the UI or starting a duplicate submission.
    const uint32_t wifiWait = millis();
    while (!hal::wifi::connected() && !cancelled.load() && uint32_t(millis() - wifiWait) < 15000)
      delay(50);
    background_network::PeriodicWork network;
    agent_coordination::TransportPause agent;
    const uint32_t wait = millis();
    while (!network.tryEnter() && !cancelled.load() && uint32_t(millis() - wait) < 5000)
      delay(20);
    // tryEnter is idempotent for this owner.
    if (cancelled.load())
      publish(State::Cancelled, "Print cancelled", false);
    else if (!network.tryEnter())
      publish(State::Failed, "Network busy. Photo kept on SD.", false);
    else if (!hal::wifi::connected())
      publish(State::Failed, "Wi-Fi disconnected. Photo kept on SD.", false);
    else if (!agent.acquire(8000))
      publish(State::Failed, "Network busy. Photo kept on SD.", false);
    else if (cancelled.load())
      publish(State::Cancelled, "Print cancelled", false);
    else {
      PhotoStream image;
      if (!hal::sd::openStream(job.path, &image.file) || image.file.size < 4 ||
          image.file.size > 4 * 1024 * 1024) {
        publish(State::Failed, "Could not read saved photo", false);
      } else {
        portENTER_CRITICAL(&mux);
        current.total = image.file.size;
        portEXIT_CRITICAL(&mux);
        HTTPClient http;
        PrintClient<WiFiClient> plain;
        PrintClient<WiFiClientSecure> secure;
        const bool tls = !strncmp(job.url, "https://", 8);
        if (tls)
          services::tls::useRootBundle(secure);
        http.setConnectTimeout(5000);
        http.setTimeout(8000);
        http.setReuse(false);
        const bool ready = tls ? http.begin(secure, job.url) : http.begin(plain, job.url);
        if (!ready)
          publish(State::Failed, "Invalid printer destination", false);
        else {
          char password[96] = {};
          if (loadPassword(job.url, password, sizeof(password)))
            http.setAuthorization("", password);
          memset(password, 0, sizeof(password));
          http.addHeader("Content-Type", "image/jpeg");
          publish(State::Uploading, "Sending photo over Wi-Fi...", true);
          int code = http.sendRequest("POST", &image, image.file.size);
#ifdef ORIGAMI_DEV_SERIAL_INPUT
          char tlsError[128] = {};
          int tlsCode = tls ? secure.lastError(tlsError, sizeof(tlsError)) : 0;
          Serial.printf("[photo-print-io] result code=%d tls=%d detail=%s consumed=%u age=%lu\n",
                        code, tlsCode, tlsError, unsigned(image.consumed),
                        (unsigned long)(millis() - image.started));
#endif
          bool accepted = false;
          char receipt[33] = {};
          if (code >= 200 && code < 300) {
            ReplyStream reply;
            if (http.writeToStream(&reply) >= 0) {
              JsonDocument doc;
              if (!deserializeJson(doc, reply.bytes)) {
                const char *id = doc["job"] | "";
                size_t n = strlen(id);
                accepted = (doc["ok"] == true && n > 0 && n < sizeof(receipt));
                for (size_t i = 0; accepted && i < n; ++i)
                  if (!isalnum((unsigned char)id[i]) && id[i] != '-' && id[i] != '_')
                    accepted = false;
                if (accepted)
                  snprintf(receipt, sizeof(receipt), "%s", id);
              }
            }
          }
          http.end();
          if (accepted)
            publish(State::Queued, "Printer accepted the photo", false, code, receipt);
          else if (code == 401 || code == 403)
            publish(State::Failed, "Set or update the printer password.", false, code);
          else if (code >= 400 && code < 500)
            publish(State::Failed, "Printer rejected photo. Kept on SD.", false, code);
          else if (image.consumed > 0)
            publish(State::Uncertain, "Check printer; request was not retried.", false, code);
          else if (cancelled.load())
            publish(State::Cancelled, "Print cancelled", false, code);
          else
            publish(State::Failed, "Cannot reach printer. Photo kept on SD.", false, code);
        }
      }
    }
    // Release HTTP, SD, and admission before making this worker reusable.
  }
  probeNetwork.release();
  probing.store(false);
  workerActive.store(false);
  hal::tasks::recordCurrentStack(hal::tasks::Id::PhotoPrint);
  hal::tasks::deleteCurrent(hal::tasks::Id::PhotoPrint);
}
} // namespace
const char *endpointStateName(EndpointState state) {
  switch (state) {
  case EndpointState::Unknown:
    return "unknown";
  case EndpointState::Online:
    return "online";
  case EndpointState::Offline:
    return "offline";
  case EndpointState::AuthRequired:
    return "auth-required";
  case EndpointState::NoWifi:
    return "no-wifi";
  case EndpointState::Busy:
    return "waiting";
  }
  return "unknown";
}
EndpointStatus endpointStatus() {
  portENTER_CRITICAL(&mux);
  auto copy = endpoint;
  portEXIT_CRITICAL(&mux);
  return copy;
}
void cancelEndpointPoll() {
  if (probing.load())
    cancelled.store(true);
}
bool readyForCapture() { return !workerActive.load(); }
bool pollEndpoint() {
  if (workerActive.load())
    return false;
  cancelled.store(false);
  if (!hal::wifi::connected()) {
    publishEndpoint(EndpointState::NoWifi);
    return true;
  }
  // Admission must precede stack allocation: an in-flight OTA/polling TLS
  // request may already use all remaining internal/DMA memory.
  if (!probeNetwork.tryEnter()) {
    publishEndpoint(EndpointState::Busy);
    return false;
  }
  char url[kDestinationCapacity];
  destination(url, sizeof(url));
  const size_t n = strlen(url);
  if (n >= 6 && !strcmp(url + n - 6, "/print"))
    url[n - 6] = 0;
  snprintf(job.url, sizeof(job.url), "%s/status", url);
  cancelled.store(false);
  probing.store(true);
  workerActive.store(true);
  portENTER_CRITICAL(&mux);
  endpoint.checking = true;
  ++endpoint.revision;
  portEXIT_CRITICAL(&mux);
  if (!hal::tasks::spawn(hal::tasks::Id::PhotoPrint, worker)) {
    publishEndpoint(EndpointState::Busy);
    probeNetwork.release();
    probing.store(false);
    workerActive.store(false);
    return false;
  }
  return true;
}
const char *stateName(State state) {
  switch (state) {
  case State::Idle:
    return "ready";
  case State::Connecting:
    return "connecting";
  case State::Uploading:
    return "uploading";
  case State::Queued:
    return "queued";
  case State::Failed:
    return "error";
  case State::Uncertain:
    return "uncertain";
  case State::Cancelled:
    return "cancelled";
  }
  return "error";
}
bool validDestination(const char *url) {
  if (!url)
    return false;
  size_t n = strlen(url);
  const char *host = nullptr;
  if (!strncmp(url, "http://", 7))
    host = url + 7;
  else if (!strncmp(url, "https://", 8))
    host = url + 8;
  else
    return false;
  if (!*host || *host == '/' || n >= kDestinationCapacity)
    return false;
  for (const char *p = host; *p; ++p)
    if ((unsigned char)*p <= 32 || (unsigned char)*p >= 127 || strchr("@?#\\", *p))
      return false;
  return true;
}
void destination(char *output, size_t capacity) {
  if (!savedDestination[0]) {
    if (!core::store::getBlob("photo_print", "url", savedDestination, sizeof(savedDestination)) ||
        !memchr(savedDestination, 0, sizeof(savedDestination)) ||
        !validDestination(savedDestination))
      snprintf(savedDestination, sizeof(savedDestination), "%s", kDefault);
  }
  snprintf(output, capacity, "%s", savedDestination);
}
bool setDestination(const char *url) {
  if (workerActive.load() || !validDestination(url))
    return false;
  char value[kDestinationCapacity] = {};
  snprintf(value, sizeof(value), "%s", url);
  size_t n = strlen(value);
  while (n > 0 && value[n - 1] == '/')
    value[--n] = 0;
  if (!validDestination(value) || !core::store::putBlob("photo_print", "url", value, sizeof(value)))
    return false;
  memcpy(savedDestination, value, sizeof(value));
  portENTER_CRITICAL(&mux);
  endpoint = EndpointStatus{};
  portEXIT_CRITICAL(&mux);
  return true;
}
bool setPassword(const char *password) {
  if (workerActive.load() || !password || strlen(password) >= sizeof(Authentication{}.password))
    return false;
  for (const char *c = password; *c; ++c)
    if ((unsigned char)*c < 32 || (unsigned char)*c >= 127)
      return false;
  char url[kDestinationCapacity];
  destination(url, sizeof(url));
  Authentication auth;
  originOf(url, auth.origin, sizeof(auth.origin));
  snprintf(auth.password, sizeof(auth.password), "%s", password);
  bool ok = core::store::putBlob("photo_print", "auth", &auth, sizeof(auth));
  memset(&auth, 0, sizeof(auth));
  return ok;
}
bool passwordConfigured() {
  char url[kDestinationCapacity], password[96];
  destination(url, sizeof(url));
  bool ok = loadPassword(url, password, sizeof(password));
  memset(password, 0, sizeof(password));
  return ok;
}
Status status() {
  portENTER_CRITICAL(&mux);
  Status copy = current;
  portEXIT_CRITICAL(&mux);
  return copy;
}
void cancel() { cancelled.store(true); }
bool start(const char *path, uint32_t sequence) {
  if (workerActive.load() || !path || strncmp(path, "/photos/", 8) || strstr(path, "..") ||
      strlen(path) >= sizeof(job.path) || !sequence)
    return false;
  char url[kDestinationCapacity];
  destination(url, sizeof(url));
  snprintf(job.path, sizeof(job.path), "%s", path);
  job.sequence = sequence;
  size_t n = strlen(url);
  bool endpoint = n >= 6 && !strcmp(url + n - 6, "/print");
  snprintf(job.url, sizeof(job.url), "%s%s?dither=fs&width=576&gamma=1.35&from=Kami", url,
           endpoint ? "" : "/print");
  workerActive.store(true);
  cancelled.store(false);
  portENTER_CRITICAL(&mux);
  current = Status{};
  current.sequence = sequence;
  portEXIT_CRITICAL(&mux);
  publish(State::Connecting, "Connecting to printer...", true);
  if (!hal::tasks::spawn(hal::tasks::Id::PhotoPrint, worker)) {
    workerActive.store(false);
    publish(State::Failed, "Not enough memory to start print", false);
    return false;
  }
  return true;
}
} // namespace photo_print
} // namespace services
