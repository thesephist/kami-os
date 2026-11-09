#pragma once
#include "app.h"
class Print;
namespace apps {
namespace photo_printer {
extern App app;
bool handleSerialCommand(const char *line, Print &output);
} // namespace photo_printer
} // namespace apps
