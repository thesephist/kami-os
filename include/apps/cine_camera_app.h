#pragma once
#include "app.h"
#include "config/diagnostics.h"
class Print;
namespace apps {
namespace cine_camera {
extern App app;
#if ORIGAMI_CINE_DIAGNOSTICS
bool handleSerialCommand(const char *line, Print &output);
#else
inline bool handleSerialCommand(const char *, Print &) { return false; }
#endif
} // namespace cine_camera
} // namespace apps
