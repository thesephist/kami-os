"""Ensure instrumentation is opt-in and cannot enter an official release."""

from pathlib import Path
import subprocess, tempfile

R = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    source = p / "test.cpp"
    source.write_text(
        """#include "config/diagnostics.h"
#ifndef ENABLED
#define ENABLED 0
#endif
static_assert(ORIGAMI_CINE_DIAGNOSTICS==ENABLED,"Cine commands");
static_assert(ORIGAMI_CINE_PROFILING==ENABLED,"Cine timing");
static_assert(ORIGAMI_INPUT_TRACE==ENABLED,"encoder trace");
static_assert(ORIGAMI_INPUT_PROFILING==ENABLED,"button cadence");
int main(){}
"""
    )
    base = [
        "c++",
        "-std=c++11",
        "-I" + str(R / "include"),
        str(source),
        "-o",
        str(p / "test"),
    ]
    subprocess.run(base, check=True)
    flags = [
        "-D" + name + "=1"
        for name in [
            "ORIGAMI_CINE_DIAGNOSTICS",
            "ORIGAMI_CINE_PROFILING",
            "ORIGAMI_INPUT_TRACE",
            "ORIGAMI_INPUT_PROFILING",
        ]
    ]
    subprocess.run(base + ["-DENABLED=1"] + flags, check=True)
    assert (
        subprocess.run(
            base + ["-DOTA_BUILD_OFFICIAL=1", "-DENABLED=1"] + flags,
            capture_output=True,
        ).returncode
        != 0
    )
    # Test the header's own rejection, independently of the positive assertions.
    source.write_text('#include "config/diagnostics.h"\nint main(){}\n')
    for name in [flag[2:-2] for flag in flags]:
        assert (
            subprocess.run(base + ["-D" + name + "=2"], capture_output=True).returncode
            != 0
        )
        assert (
            subprocess.run(base + ["-D" + name + "=1"], capture_output=True).returncode
            == 0
        )
        assert (
            subprocess.run(
                base + ["-DOTA_BUILD_OFFICIAL=1", "-D" + name + "=1"],
                capture_output=True,
            ).returncode
            != 0
        )
print(
    "PASS production instrumentation defaults off, explicit diagnostic capabilities and official-release/invalid-flag rejection"
)
