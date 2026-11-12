#pragma once
#include <stdint.h>
namespace core {
namespace input {
// UI-owned optional diagnostic. No allocation, I/O, or event injection.
class Cadence {
public:
  struct Snapshot {
    uint32_t samples = 0, failures = 0, maxGapUs = 0;
    uint64_t spanUs = 0, blind20Us = 0, blind50Us = 0, blind100Us = 0;
  };
  void start() {
    stats_ = {};
    enabled_ = true;
    haveSample_ = false;
  }
  void stop() { enabled_ = false; }
  bool enabled() const { return enabled_; }
  Snapshot snapshot() const { return stats_; }
  void sample(uint32_t nowUs, bool valid) {
    if (!enabled_)
      return;
    if (!valid) {
      ++stats_.failures;
      return;
    }
    ++stats_.samples;
    if (haveSample_) {
      const uint32_t gap = nowUs - lastUs_;
      stats_.spanUs += gap;
      if (gap > stats_.maxGapUs)
        stats_.maxGapUs = gap;
      if (gap > 20000)
        stats_.blind20Us += gap - 20000;
      if (gap > 50000)
        stats_.blind50Us += gap - 50000;
      if (gap > 100000)
        stats_.blind100Us += gap - 100000;
    }
    lastUs_ = nowUs;
    haveSample_ = true;
  }

private:
  Snapshot stats_;
  bool enabled_ = false, haveSample_ = false;
  uint32_t lastUs_ = 0;
};
} // namespace input
} // namespace core
