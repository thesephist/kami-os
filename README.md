> **thesephist/kami-os fork.** Based on upstream `de0f204` (v1.0.9). Adds Cine camera and a faster newest-first Photos gallery with wraparound. Home B opens Cine; the built-in Camera remains available in the application selector.
>
> Cine starts in continuous Auto exposure with Sunny Color. The wheel selects Color first, then shutter; hold it to restore Auto. Six sensor-ISP Color modes, an 8× gain ceiling, native 3MP QS8 JPEG saves, and camera/exposure EXIF form the current baseline. See [stock/Cine differences](docs/cine-camera-review.md) and [validation](docs/cine-camera-validation.md).
>
> GitHub Actions, automated releases and repository webhooks are disabled for this fork. Run checks and builds locally.

## Working on this fork

```sh
git clone https://github.com/thesephist/kami-os.git
cd kami-os
git remote add upstream https://github.com/Origami-Computing/origami-firmware-public.git
python3 -m venv .venv
.venv/bin/python -m pip install platformio==6.2.0 -r tools/requirements-test.txt
./build.sh
.venv/bin/python tools/run_fork_checks.py --sdk
```

Host checks require Python 3 and a C++ compiler with AddressSanitizer/UndefinedBehaviorSanitizer. They use generated fixtures, not private photos, a connected device, or scratch directories. The SDK check additionally uses the pinned JPEGDEC source downloaded by the build and Pillow (`python3 -m pip install -r tools/requirements-test.txt`, in your Python environment). CI builds the production target and runs these checks. To format only added C++ files, install `clang-format==18.1.8` and run `python3 tools/format_fork.py` (or `--check`).

`build.sh` uses `.venv` and keeps PlatformIO toolchains/downloads in `${XDG_CACHE_HOME:-$HOME/.cache}/kami-os/platformio`. `PLATFORMIO_CORE_DIR` and `KAMI_PYTHON` can override those defaults. The normal `.pio/libdeps` directory remains the project's library cache.

Private local work lives in `.local/lab/` (ignored by Git): RAW captures, photo comparisons, retained firmware/recovery records, experiment recipes, and historical handoff source. Historical `.origami/...` evidence paths in the validation notes now resolve under `.local/lab/.origami/...`. The old standalone scratch directory has been retired; `.local/lab/.origami/migration/` contains the checksum inventory and exact pre-relocation text originals. These private records are preserved data; toolchain and retired build caches are separate in `~/.cache/kami-os`.

`main` tracks this fork's `origin/main`; `upstream` is the original source repository. Tag `cine-baseline-c0-s1-b0-20261109` preserves the tested firmware before the cleanup pass. Tag `pre-upstream-sync-20261109` preserves the published v1.0.4-based fork before rebasing onto upstream v1.0.5. Diagnostic builds inspect the canonical pipeline; legacy RAW/ISP/resize experiments remain in Git history. The JPEG compression study is documented in [the experiment report](docs/cine-jpeg-compression-study.md). Wi-Fi settings are provisioned on-device, never in Git.

---

<p align="center">
  <img src="docs/assets/origami-computing.svg" alt="Origami Computing" width="88" height="98">
</p>
<h1 align="center">kamiOS 1.0</h1>
<p align="center"><strong>Origami Computing</strong></p>

kamiOS is the operating firmware for Origami’s ESP32-S3 handheld: a display, camera, audio, motion sensing, and Lua widgets in one device.

Official releases are published after source review, automated checks, and qualification of the signed candidate. See [release review](docs/release-review.md).

## Build your own app

Describe an idea to your coding assistant and give it the [Origami app builder skill](skills/origami-app-builder/SKILL.md). Start with Lua: small games, clocks, interactive tools and data screens can run from the SD card without rebuilding firmware. The skill includes a [ready-to-copy counter](skills/origami-app-builder/assets/counter/main.lua), the supported Lua API and C++ guidance for deeper hardware features.

With a skill-compatible assistant, copy the entire `skills/origami-app-builder` folder into its skills directory and invoke `$origami-app-builder`. In Codex, that directory is `~/.codex/skills/`. Other assistants can read `SKILL.md` and its linked references directly from this checkout.

Try: “Use the Origami app builder skill to make a breathing app with a gentle animation and rotary speed control.” Lua supports the device's exposed capabilities; arbitrary network, storage and hardware access need native integration.

## Hardware

The supported target is rev-4 ESP32-S3 N16R8: 16 MB flash, 8 MB octal PSRAM, a 240 × 280 ST7789 display, and a TCA6424A I/O expander. Firmware for other boards is not interchangeable.

## Build

Install PlatformIO Core 6.2.0 in a dedicated Python environment, then run:

```sh
pio run -e esp32-s3-menu-n16r8-production
```

This produces a **custom production-policy build**, not an official signed OTA release. The pinned Arduino framework, libraries, and compatibility wrappers are part of the build contract. See [building](docs/building.md).

## Firmware

This source targets **v1.0.6**. It adds a reworked onboarding flow and Wi-Fi setup by sound, an App Store that installs signed native apps by QR or six-digit code, IMU calibration, a clock that survives deep sleep, battery sleep with alarm wake, faster wake to Home, automatic upload of saved recordings, and memory and network recovery fixes. Native app packages load only with a valid publisher signature and matching hashes.

Earlier releases added on-device video recording with audio and local playback within Photos (v1.0.4) and Public updates without an OTA enrollment key (v1.0.5).

Run `python3 tools/test_mp3_metadata.py` and `python3 tools/test_playback_start_admission.py` for the metadata, decoder, seeking and failed-start regression checks.

`firmware.bin` is an application image, not a full-device installer. Do not write it at address zero. [Installation notes](docs/installation.md) explain the current review boundary. Credentials and OTA access must be provisioned separately; they are never distributed with this repository. The minimal owner-confirmed SD artwork is included under `sd_flash/`.

## Explore

- [Architecture and ownership](docs/architecture.md)
- [Build and verification](docs/building.md)
- [SDK license and security review](docs/sdk-review.md)
- [Private-to-public promotion runbook](docs/public-promotion.md)
- [Contribution guide](CONTRIBUTING.md)
- [Security reporting](SECURITY.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

## License

Origami’s original source is offered under [MIT](LICENSE), as selected for this migration. Existing third-party licenses continue to govern their respective components. Origami confirmed ownership of its artwork and generated font for this migration. Upstream components retain their own licenses; see [distribution requirements](docs/distribution.md).

Public updates since v1.0.5 require no OTA enrollment key. HTTPS, signed manifests, image hashes, hardware/layout checks and update confirmation remain mandatory. Private and Staging retain developer authorization. This change only takes effect after installation; legacy recovery is qualified separately for each supported updater and partition layout.
