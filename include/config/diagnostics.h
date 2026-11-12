#pragma once
// Explicit build capabilities. USB maintenance is independent of these flags.
#ifndef ORIGAMI_CINE_DIAGNOSTICS
#define ORIGAMI_CINE_DIAGNOSTICS 0
#endif
#ifndef ORIGAMI_CINE_PROFILING
#define ORIGAMI_CINE_PROFILING 0
#endif
#ifndef ORIGAMI_INPUT_TRACE
#define ORIGAMI_INPUT_TRACE 0
#endif
#ifndef ORIGAMI_INPUT_PROFILING
#define ORIGAMI_INPUT_PROFILING 0
#endif
#if (ORIGAMI_CINE_DIAGNOSTICS != 0 && ORIGAMI_CINE_DIAGNOSTICS != 1) ||                            \
    (ORIGAMI_CINE_PROFILING != 0 && ORIGAMI_CINE_PROFILING != 1) ||                                \
    (ORIGAMI_INPUT_TRACE != 0 && ORIGAMI_INPUT_TRACE != 1) ||                                      \
    (ORIGAMI_INPUT_PROFILING != 0 && ORIGAMI_INPUT_PROFILING != 1)
#error Diagnostic capabilities must be 0 or 1
#endif

#if defined(OTA_BUILD_OFFICIAL) && OTA_BUILD_OFFICIAL &&                                           \
    (ORIGAMI_CINE_DIAGNOSTICS || ORIGAMI_CINE_PROFILING || ORIGAMI_INPUT_TRACE ||                  \
     ORIGAMI_INPUT_PROFILING)
#error Official release builds must exclude Cine diagnostics and input instrumentation
#endif
