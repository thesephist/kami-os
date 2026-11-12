#pragma once
#include "core/input_cadence.h"
namespace hal {
namespace input {
// UI task only. Measures successful physical button snapshots, including gaps
// from frame waits, drawing and other work; failed I2C reads do not count as samples.
void startButtonCadence();
core::input::Cadence::Snapshot buttonCadence(bool stop = false);
} // namespace input
} // namespace hal
