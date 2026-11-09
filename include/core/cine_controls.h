#pragma once
#include <stdint.h>
namespace core {
namespace cine {
// UI-task-owned modal wheel; all deadlines are wrap-safe millisecond ages.
class WheelControl {
  bool whiteBalance_ = false, visible_ = false;
  uint32_t changedAt_ = 0;

public:
  void reset() {
    whiteBalance_ = visible_ = false;
    changedAt_ = 0;
  }
  bool whiteBalance() const { return whiteBalance_; }
  bool visible() const { return visible_; }
  // Hold1s, then fade over200ms. The mode survives after its overlay disappears.
  unsigned opacity(uint32_t now) const {
    if (!visible_)
      return 0;
    const uint32_t age = uint32_t(now - changedAt_);
    return age >= 1200 ? 0 : age <= 1000 ? 255 : unsigned((1200 - age) * 255 / 200);
  }
  void show(uint32_t now) {
    visible_ = true;
    changedAt_ = now;
  }
  void toggle(uint32_t now) {
    whiteBalance_ = !whiteBalance_;
    show(now);
  }
  bool expire(uint32_t now) {
    if (visible_ && uint32_t(now - changedAt_) >= 1200u) {
      visible_ = false;
      return true;
    }
    return false;
  }
};
} // namespace cine
} // namespace core
