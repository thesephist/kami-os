"""Check canonical Cine, removed experiments, and opt-in upstream encoder tracing."""

from pathlib import Path
import argparse, os, subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--elf", type=Path, required=True)
p.add_argument("--diagnostics", action="store_true")
a = p.parse_args()
cache = Path(
    os.environ.get(
        "PLATFORMIO_CORE_DIR", str(Path.home() / ".cache/kami-os/platformio")
    )
)
nm = cache / "packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-nm"
symbols = subprocess.check_output([str(nm), "-S", "-C", str(a.elf)], text=True)
legacy = [
    "apps::hello_world::",
    "apps::photo_printer::",
    "services::photo_print::",
    "apps::cine_camera::handleSerialCommand",
    "Session::previewDrawn",
    "hal::input::startButtonCadence",
    "ToneMap::",
    "meterBayer",
    "benchmarkIspResize",
    "benchmarkResolution",
    "rawShutterRead",
    "rawShutterApply",
    "setRawShutter",
    "lockRawShutter",
    "cine_bench_jpge",
    "readJpegGamma",
    "__wrap_xTaskCreatePinnedToCore",
]
for name in legacy:
    assert name not in symbols, "Legacy implementation remains linked: " + name
for name in [
    "hal::input::encoderTraceStart",
]:
    assert (name in symbols) == a.diagnostics, (
        "Capability mismatch",
        name,
        a.diagnostics,
    )
for name in ["hal::input::s_trace", "hal::input::s_traceArchive"]:
    assert (name in symbols) == a.diagnostics, (
        "Trace storage mismatch",
        name,
        a.diagnostics,
    )
assert "hal::camera::configureCineIsp" in symbols
assert "hal::camera::getFrame" in symbols
print(
    "PASS removed example/print apps absent; canonical Cine linked once; "
    + (
        "optional upstream encoder trace present"
        if a.diagnostics
        else "legacy Cine inspection/profiling and input trace/cadence absent"
    )
)
