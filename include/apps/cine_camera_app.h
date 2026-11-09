#pragma once
#include "app.h"
class Print;
namespace apps { namespace cine_camera {
extern App app;
bool handleSerialCommand(const char* line,Print& output);
} }
