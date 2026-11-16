#pragma once
// Upstream encoder tracing is opt-in, independent of USB maintenance access.
#ifndef ORIGAMI_INPUT_TRACE
#define ORIGAMI_INPUT_TRACE 0
#endif
#if ORIGAMI_INPUT_TRACE != 0 && ORIGAMI_INPUT_TRACE != 1
#error ORIGAMI_INPUT_TRACE must be 0 or 1
#endif
#if defined(OTA_BUILD_OFFICIAL) && OTA_BUILD_OFFICIAL && ORIGAMI_INPUT_TRACE
#error Official release builds must exclude input instrumentation
#endif
