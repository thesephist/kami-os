# Contributing

Public contributions open after the migration review is complete. Keep changes focused and include the reason, affected hardware, and verification results.

Apps own workflows; screens own view lifecycles; services own operational sessions and cancellation; core owns reusable models and algorithms; HAL owns device access and bus policy. Preserve cleanup, memory budgets, rendering paths, and resource reservations. Do not put blocking network or device work in drawing callbacks.

Before submitting:

```sh
python3 tools/check_tracked_secrets.py
python3 tools/check_architecture_dependencies.py
python3 tools/test_architecture_dependencies.py
pio run -e esp32-s3-menu-n16r8-production
```

Hardware changes also require focused testing on the matching board. A successful build is not proof of device behavior. Do not upload private logs, credentials, recordings, screenshots of user data, or generated firmware into source control. Preserve third-party notices and document the provenance of new assets.


Cine development keeps one canonical camera pipeline. `./build.sh` builds the
production target; `./build.sh --diagnostics` builds the same pipeline with
explicit inspection/profiling/input-trace capabilities. Run
`python3 tools/run_fork_checks.py --sdk` after dependency installation, and check
`tools/check_cine_build.py --elf PATH` (add `--diagnostics` for that target).
Do not add experimental photographic modes to the production session.
