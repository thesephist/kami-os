#pragma once
#include "app.h"

class Stream;

namespace apps {
namespace photos {
extern App app;
void dumpStatus(Stream& output);
bool handleSerialCommand(const char* line,Stream& output);
}  // namespace photos
}  // namespace apps
