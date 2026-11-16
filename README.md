# thesephist/kami-os

Personal fork of [kamiOS](https://github.com/Origami-Computing/origami-firmware-public), based on upstream v1.0.9 (`de0f204`) that adds some early prototypes and customizations. You can still find the original README in [UPSTREAM_README.md](UPSTREAM_README.md).

In this fork:

1. A new camera app, called **Cine camera**, that adds better postprocessing, color treatment, auto-exposure, and optimizations behind a new camera UI with more advanced controls. Cine camera also tags photographs with EXIF metadata of the capture device and settings.
2. Optimizations to the **Photo gallery** to make paging through photos much more responsive.
3. **Development tooling.** Adds `build.sh`, focused camera/gallery sanitizer tests, firmware-symbol checks, a UI-flow renderer, a JPEG compression study and design/validation notes. Upstream encoder tracing is opt-in and excluded from production; completed Cine inspection/profiling experiments are removed. GitHub Actions, release workflows and repository webhooks are disabled for this fork.

Build with `./build.sh`. Run checks with `.venv/bin/python tools/run_fork_checks.py --sdk`. See [CONTRIBUTING.md](CONTRIBUTING.md) for setup and [the camera review](docs/cine-camera-review.md) for implementation details. Upstream and third-party licenses remain applicable; see [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
