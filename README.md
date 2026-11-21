# thesephist/kami-os

Personal fork of [kamiOS](https://github.com/Origami-Computing/origami-firmware-public), based on upstream v1.0.16 (`dfbd9a2`). The original README is in [UPSTREAM_README.md](UPSTREAM_README.md).

The camera and gallery work developed here is now included upstream. This branch adopts upstream's unified Camera, QVGA viewfinder, 3MP capture, auto-exposure, color modes, video recording, photo uploads, and gallery behavior.

Remaining differences from upstream:

1. **Development tooling.** Adds `build.sh`, a focused camera/gallery check runner, firmware-symbol checks, formatting helpers, and a JPEG compression study. Earlier fork design and validation notes remain in `docs/`; they describe the pre-merge implementation where behavior differs from the upstream README.
2. **Encoder diagnostics.** Encoder tracing is opt-in and excluded from production and official builds. `./build.sh --diagnostics` enables it; the camera pipeline remains the same. Upstream camera logging is retained.
3. **Repository configuration.** GitHub Actions, release workflows, and repository webhooks are disabled for this fork. Local build artifacts, captures, and device records are ignored.

Build with `./build.sh`. Run checks with `.venv/bin/python tools/run_fork_checks.py --sdk`. See [CONTRIBUTING.md](CONTRIBUTING.md) for setup. Upstream and third-party licenses remain applicable; see [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
