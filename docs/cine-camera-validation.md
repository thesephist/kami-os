# Cine review validation

Local artifact paths in this historical log were migrated into `kami-os/.local/lab/`. Every `.origami/...` reference below resolves as `.local/lab/.origami/...` relative to this repository. Firmware, RAW and image bytes were preserved; path-bearing files were relocated with originals retained in the private migration archive. SDK/retired build caches live separately in `~/.cache/kami-os`.


Validated on 2026-11-03 against upstream `fbc6556389082f85b1900da6002cc9527402bf81`.

- Production firmware build passed with Hello World source and menu entry removed. Photo Printer remains.
- Independent Cine-only review-source production build passed; this isolated binary was compiled, not installed.
- ASan/UBSan: actual capture service, allocation-failure cleanup, failed teardown/retry, scoped cleanup, null/malformed frames and extreme exposure deltas. Camera/codec mocked in service tests.
- ASan/UBSan: actual preview methods with PSRAM-only allocation checks, stable repeated decoder reuse, failed allocation/open/dimension/partial decode, atomic frame swaps and idempotent cleanup. Decoder mocked for fault injection.
- ASan/UBSan: actual ROM/JPEGDEC callback bounds/strides/channel mapping; actual sensor ISP transaction/readback/rollback, WB and read-only exposure telemetry; RAW tone/meter and physical shutter bounds/rollback; existing DMA adapter and Stock Camera/video composition.
- Actual pinned JPEGDEC decoded retained native 3MP and VGA sensor JPEGs correctly; no private image files are included in the review source.
- Architecture dependency check and its seven tests, tracked-secret/default checks, and diff whitespace checks passed.
- Scratch-device application flash verified, boot observed without panic, protected metadata unchanged.
- Hardware: eight Cine → Stock-reference → Cine cycles without a camera-init failure; minimum sampled contiguous DMA block **5,620 bytes**. This is a bounded stress check, not proof of indefinite leak freedom. Follow-up controls ran in a second USB session after correcting the test to await an asynchronous settings transition.
- Hardware: settings enter/return, all five WB selections, both shutter endpoints, four native 2048×1536 JPEG captures with verified JSON sidecars, exit/re-entry, and a separate built-in Stock-camera-to-Cine transition passed.

At the tested 100ms shutter, median preview interval was **301.2ms** and DMA-to-composed-display age **247.9ms**. This was a functional/memory check without a matched before/after performance control. The safety back buffer adds 75,264 bytes of PSRAM; it does not change saved image processing.

The main experimental firmware retains Photo Printer quiescence hooks. The isolated review source omits only that integration glue, because upstream has no printer task. Historical RAW/color/resize diagnostics remain available through USB; they are not selected in the normal Cine mode.

No whole-SDK/codec memory-safety claim is made. Persistent I2C teardown failure intentionally holds the camera reservation for retry. USB reconnection and long-duration battery/network soak remain outside this review's hardware check.


## Sunny/Cine-only revision — November 8, 2026

- Production build passed; verified application-only install on scratch `44:B1:76:C6:14:40`, with verified recovery image, unchanged protected metadata, normal boot and no panic observed.
- Actual capture service and ISP register transaction passed ASan/UBSan host fixtures, including allocation/teardown failures, borrowed frames, bounds, failure rollback and the read-only gamma snapshot (output unchanged on read failure). Actual ISP service tests cover static WB cycling, WB failure rollback and exact vendor-gamma copying/guards. HAL/JPEG are mocked in the capture-service fixture; the register transaction uses a failure-injected register bus.
- Dependency architecture check passed across 285 core/service/SFX files.
- Real A events cycle Sunny → Cloudy → Office → Home → Sunny twice; no Auto. Reopening restores Sunny. Positive wheel steps shorten exposure: 1/10 s → 1/12.5 s; endpoints remain 1/1000–1/10 s. C opens a controls summary with no Stock choice and returns without another camera initialization.
- Seven completed opens: median camera-service startup **1,951.8 ms**. Typical stages: init 686 ms, observed AE convergence 598 ms, ISP programming/readback 110 ms, first fresh decoded preview 420 ms, plus reservation/storage preparation. All seven reached the convergence criterion. These are one-scene measurements, not a worst-case bound; slower convergence retains the previous 1.1 s settling budget. The framebuffer read itself can exceed a budget boundary.
- Earlier firmware's three USB-observed opens were approximately 3.0 s. New normal USB observations were around 2.25–2.45 s, but two USB-delivery outliers were 9.7 and 13.0 s while device-side work remained ~1.95 s. Use device phase timers to assess camera work, not those transport stalls. The device-side measurement excludes app splash drawing and subsequent panel flush.
- Two repeated matched 3MP comparisons (vendor/sRGB/sRGB/vendor), Sunny, 40 ms requested shutter, gain 1×, same matrix/flags/JPEG quality. Four JPEGs and sidecars downloaded with CRC verification; all native pixels retained in the two labeled PNGs. Only gamma registers differ. Maximum difference at the 15 gamma knots is 4/255; highest-segment slope register is 15 vendor vs 31 Cine. The photos show a subtle difference in this scene; no larger dynamic-range or sharpness advantage is established. These are sequential captures, not identical RAW exposures. Diagnostic metadata `isp_experiment=2` identifies vendor gamma; `0` identifies Cine sRGB (the older generic `look` label is not diagnostic).
- Captures completed in 397–420 ms including frame acquisition and SD save; no software tone pass or JPEG re-encoding. The app was left in normal Sunny/sRGB viewfinder mode.

Artifacts: `.origami/artifacts/cine-sunny-only-20261108/index.html`, `device-results.json`, `gamma-registers.json` and the two full-resolution PNGs, under the scratch workspace. The earlier Cine-only handoff bundle remains a snapshot of its previous revision; this validation describes the current firmware workspace.


## Modal wheel and same-RAW tone lab — November 9, 2026

- Production build passed and the scratch application was backed up/verified, flashed/verified, and booted with protected metadata unchanged and no panic observed. Image: `cine-modal-wheel-20261109.bin`.
- Wheel defaults to shutter; C toggles shutter/WB. A opens Settings; A/C returns with WB, shutter and wheel mode preserved. No camera restart across Settings. Persistent bottom readout includes WB/shutter/EC/gain; transient value occupies the former C gear location for a 1,200 ms idle interval. A small SS/WB mode mark remains after expiry.
- Actual hardware verified: Sunny initial state; inverted shutter direction; active popup node and subsequent expiry while persistent values remain; C switches to WB; WB moves forward/backward and wraps without changing shutter; A Settings preserves values/mode; shutter endpoints 1/1000–1/10 s; physical B event saves native 2048×1536 `/photos/IMG_00239.jpg`; reopen restores Sunny and shutter mode. Actual display snapshots retained. Final DMA largest block: 8,180 bytes.
- Actual ISP service tests under ASan/UBSan cover forward/backward WB wrap, no-change deltas, INT_MIN/INT_MAX, failed transaction rollback, and vendor gamma guards. Actual view rendering under ASan/UBSan covers idle/popup shutter/WB and save feedback with production Instrument font; popup state tested at 1,199/1,200 ms and timer rollover. Existing Stock camera/video composition tests passed; architecture check passed across 286 files.
- New native RAW8 retained from scratch hardware, 3,145,728 bytes, CRC-checked download and SHA-256 `505aa1fe1267d0f163d52de46e06d3dd9bfeeae8615a3c288af787b3f934766b`. The first USB download timed out; its retry used the same retained frame, not a new exposure.
- Offline tone lab: one fixed RAW reconstruction/color treatment; reference plus four curves (Crisp, Deep toe, Soft shoulder, Matte), each with native 2048×1536 PNG, curve plot, shared 18% anchor, source metadata and hash. Curves are applied to a floating-point linear luminance proxy before final sRGB encoding/quantization. The RAW lab is camera-native RGB, not an exact emulation of sensor demosaic/color/JPEG. Matte adds its floor neutrally rather than magnifying chroma noise. No creative curve installed as a new default.
- All five curves checked finite/monotonic with common gray; all five native PNG sizes and local HTML links checked; JS syntax, all four comparison selections and divider endpoints exercised.
- Stock explanation corrected: the pinned OV3660 driver's saturation −1 rewrites the CMX coefficients to roughly 90% neutral chroma. Stock contrast −1 writes Y gain 0x18, while neutral is 0x20. The report's 0.75 contrast line is explicitly a gain-only illustration, not a measured full Stock transfer; actual gamma knots and highest-segment registers come from the prior hardware readback. A future selected luminance curve cannot be assumed identical to an independent per-channel sensor-gamma approximation.

Report: `.origami/artifacts/cine-modal-tone-lab-20261109/index.html`; hardware results: `modal-device-results.json`; actual screen images: `ui/device-controls.png`, all under the scratch workspace. Source remains separate from the clean earlier handoff bundle, which has not been regenerated.


## Readable direct controls and fixed-gamma B/C/S study — November 9, 2026

- Installed and verified `cine-readable-controls-20261109.bin` on the selected scratch device, with verified recovery image, unchanged protected metadata, normal boot and no panic observed.
- A uses the Stock gear mask. Its source ink occupies 12×16 pixels in a16×16 cell; rendering the cell at24×18 produces an18×18 ink box. Both shutter and WB values are persistent beside the wheel in the ordinary Instrument size2 font, with small SS/WB labels and a > marker. The old bottom metrics row is absent. Settings now uses the shared ListView and normal row metrics, with inline shutter/WB editing and Back to camera.
- ASan/UBSan view rendering passed all shutter/WB/save states with the production font; exact square gear ink bounds and an untouched bottom image strip are asserted. Dependency architecture check passed across286 core/services/SFX files.
- Hardware passed: gear bitmap, no old Settings text/footer metrics, both values remain after idle, regular size2 value nodes, C changes active row, WB wheel preserves shutter, standard3-row menu with all label sizes2, WB and shutter edits work in menu, Back returns without driver initialization, shutter endpoints still1/1000–1/10 s, and physical B saves native2048×1536. Reopen restores Sunny/shutter mode. Metadata confirms ISP experiment0 and no software Filmic; the installed gamma/color pipeline remains unchanged.
- B/C/S study uses two retained Sunny native RAW8 captures, each independently reconstructed once. Within each scene, all27 variants use the same input frame, black subtraction, orientation, WB convention and fixed sRGB transfer. No creative curve. Brightness adds−16/0/+16 display luma codes; contrast uses gain0.75/1/1.25; saturation uses the actual pinned vendor−1/0/+1 CMX tables. The gain/offset/CMX-to-RGB model is explicitly approximate and not a measured full ISP emulation. Brightness/contrast coupling and output clipping are part of the exploration; no midgray pivot is invented.
- Verified all54 native2048×1536 PNG hashes/sizes, six labeled3×3 grids, RAW hashes, neutral-axis preservation of the three vendor tables, every scene/saturation/cell selection, and native/grid download links. The grid sheets use640×480 previews; native PNGs are linked for detail inspection. No candidate B/C/S setting was installed.

Hardware report: `.origami/artifacts/cine-readable-controls-20261109/device-results.json`; screens: `ui/device-controls.png`. B/C/S report: `.origami/artifacts/cine-bcs-grids-20261109/index.html`. The prior Cine-only handoff bundle remains its earlier snapshot, not a regenerated bundle of this revision.


## Transient translucent wheel overlay and complete gear — November 9, 2026

- Installed/verified `cine-subtle-overlay-final-20261109.bin` on the selected scratch device with verified recovery image, unchanged protected metadata, normal boot and no panic observed.
- Wheel labels/values are absent at idle. Rotation or C-mode click shows both rows; selection is the firmware kUiEdit light-blue text on both label and value. No > marker. Opacity holds1s, fades over200ms, and is zero1.2s after the last interaction. Mode is retained after hiding. Both per-row backings and the smaller photo/save capsule are rounded and50% black at full opacity.
- Backdrops use bounded RGB565 stack spans, reading the original composed camera frame. Sprite image uploads explicitly preserve/restore the correct byte-order setting. Every draw reconstructs the photo first, preventing cumulative dimming. No per-frame heap allocation added.
- Gear is a native20x20 eight-tooth mask with matching horizontal/vertical teeth and a rounder opening. Pixel inspection uses exact integer nearest-neighbor enlargement. During implementation, a new tightly packed mask conflicted with the standard Scene bitmap reader's byte-aligned rows. It was repacked to60 bytes with a compile-time size check. The final hardware scene dump is byte-for-byte equal to that exact array.
- ASan/UBSan host tests cover the actual production view and actual Scene bitmap reader (native and scaled paths), native/Scene gear equivalence, quarter-turn symmetry, all cardinal teeth and center opening, padding bits, 50% white-to-RGB5650x7bef dimming, untouched rounded corners, invalid/overflowing box rejection, unchanged idle wheel-region pixels, correct selected-row blue, fade endpoints and timer rollover. Existing view states/save feedback and untouched bottom photo strip also passed. Architecture check passed across286 files.
- Final hardware passed: idle3-node scene (photo, gear, photo hint), visible selected blue SS/WB rows after interaction, expiry restores idle while retaining mode, WB forward/backward changes, normal three-row Settings navigation/return, successful native2048x1536 B capture with Sunny, ISP curve0 and no software Filmic, and final hidden Sunny/shutter viewfinder. Screen captures and metadata are retained. Previous prototype screenshots/results are archived separately and do not certify the final bitmap layout.

Report: `.origami/artifacts/cine-subtle-overlay-20261109/index.html`; hardware: `device-results.json`; screens: `ui/device-controls.png`. Existing tone/B/C/S choices remain unchanged. Earlier Cine-only handoff bundle has not been regenerated for this UI revision.


## C0/S+1/B0 baseline — November 9, 2026

- Selected the main-grid **C+0 S+1 · B0** look, superseding the earlier A3/negative-offset exploration. Production uses the exact pinned OV3660 +1 saturation CMX (`1d60030d84918a76140198`). sRGB gamma remains unchanged; SDE contrast/brightness and lens shading remain bypassed. No additional black cutoff, Filmic or software grading.
- ASan/UBSan actual ISP-service tests verify the exact selected and neutral vendor tables, unchanged gamma, invalid-value guards, WB cycling/rollback and diagnostic fitted-matrix isolation. Actual capture-service and failure-injected HAL transaction tests passed. Architecture check: 286 files; whitespace check passed.
- Production build and verified application-only installation on scratch `44:B1:76:C6:14:40` passed. Prior image verified for recovery; protected metadata unchanged; new boot observed without panic.
- Hardware: default and post-shutter/post-WB captures retain saturation +1 in JSON sidecars, native 2048×1536 output, no Filmic/fitted matrix, EC0. Explicit neutral/+1 transactions read back their exact CMX bytes with identical gamma/control flags. Gamma/color enabled; SDE and lens shading off. Reopen restores Sunny, shutter mode and hidden transient wheel controls.
- These checks validate configuration and functionality. The archived RAW grid approximates the sensor pipeline; it is not a bit-exact guarantee of final photographic appearance. This run does not retune exposure or judge quality from the current scene.

Evidence: `.origami/artifacts/cine-saturation-plus1-20261109/device-results.json`; binary `.origami/artifacts/cine-saturation-plus1-20261109.bin`. Earlier handoff bundles remain historical snapshots.


## Managed-fork cleanup — November 9, 2026

- Pre-cleanup snapshot: `cine-baseline-c0-s1-b0-20261109` (`d312307`). Cleanup: `912036a`. The original upstream history is retained. Local checkout: `/home/thesephist/src/kami-os`; GitHub: `https://github.com/thesephist/kami-os`.
- All 18 focused checks passed, including portable ASan/UBSan Cine fixtures, actual HAL ISP/shutter transactions with injected failures, preview/gear/fade rendering, DMA isolation, shared camera reservation/frame-loan guards, stock still capture, Photos navigation/catalog and pinned real JPEGDEC gallery rendering. The new runner is `python3 tools/run_fork_checks.py --sdk`; its SDK fixture dependency is declared in `tools/requirements-test.txt`.
- The initial inherited session mocks omitted our HAL additions, and the gallery block test depended on whitespace around namespace braces. Those fixtures were repaired and the affected checks passed. Checks earlier in the run were not rerun after test-only changes.
- Thirteen files changed only for readability exactly equal the pre-cleanup source after the pinned formatter, including printer logic, RAW tone math and the gallery renderer. Whole-upstream formatting was excluded. All newly tracked source/tests passed the credential scan.
- Full production build passed. Application-only installation on scratch `44:B1:76:C6:14:40` verified the previous recovery image, new flash, protected metadata and boot; no panic observed. The daily driver was excluded.
- Hardware saves passed at native 2048×1536 after startup, shutter adjustment and WB adjustment. The selected sensor gamma, CMX and control flags read back exactly the same as before cleanup. JSON schema and stable image-control fields match the prior baseline; timings, exposure and paths naturally vary by capture. Reopen restores Sunny/shutter mode with the wheel overlay hidden. No photographic retuning or performance claim is made for this pass.

Local hardware evidence is retained in the scratch workspace at `.origami/artifacts/cine-quality-20261109/`, including `device-results.json` and `baseline-equivalence.json`; it contains no new public photo archive. Firmware artifact: `.origami/artifacts/cine-quality-20261109.bin`. CI is configured to build and run the fork checks; local verification does not assert a remote CI result.


## Final UI polish — November 9, 2026

- SS/WB readouts now appear immediately at full text opacity, stay for 2 seconds after wheel movement or a mode click, then disappear immediately. Fixed 50%-opaque rounded backdrops and blue active-row styling remain. The fade adapter and intermediate-alpha state were removed. Wrap-safe deadline, refresh and mode persistence checks pass under ASan/UBSan.
- The actual input handlers pass host checks for one explicit Select cue per mode switch, unchanged shutter/WB routing, settings editing and capture. Shared navigation lifecycle/cue tests pass; the usual haptic preference and cue deduplication remain respected.
- Home B now pushes Cine camera. The main application selector's original Camera entry is unchanged. The opening/error hint was corrected to “A: back” to match the existing return behavior.
- Final production build and reviewed SDK archive/linkage checks pass. The scratch application install verified recovery, flash, protected metadata and normal boot. On hardware: Home B → Cine; active/hidden overlays; WB mode retained after hiding; one Select effect start for one mode click; settings WB editing; native 2048×1536 save with the same saturation+1/sRGB pipeline; original Camera opened through the selector; Home B again opens Cine and restores Sunny/shutter defaults. The selector check resolves the Camera label because a dynamic Wi-Fi/update row may precede it.
- The 18-state design PNG was rendered from the current viewfinder raster, Instrument font, app Scene builder and list styling using the supplied photo; it includes a real Home capture. Source-rendered menus omit variable system chrome. The optional Preparing state, four WB presets, settings browse/edit, capture/saved/failure and camera-error recovery are diagrammed. Private photo and rendered PNGs stay outside Git; `tools/render_cine_flow.py` and its host fixture are reusable with `--image`, `--output` and optional `--home`.

Local evidence: `.origami/artifacts/cine-polish-design-20261109/device-results.json`, `device.log`, `cine-camera-flows.png` and its JSON manifest. Installed binary: `.origami/artifacts/cine-polish-final-20261109.bin`.


## Upstream v1.0.5 sync — November 9, 2026

- Rebased the four published fork commits onto upstream `cf3d1ce8ecfd7d4e4c63c52c9e25358dcdc52637`, which contains v1.0.5 (`c71e3d3`) and the legacy Staging recovery fix. Rebase completed without conflicts; `git range-diff` reports all four patches identical. Checkpoint `pre-upstream-sync-20261109` retains the previous published head (`d240277`).
- Upstream changes Public OTA access so signed updates need no enrollment key; Private/Staging still require developer authorization. It also corrects legacy recovery manifest versions and their preparation workflow. No camera/gallery or SDK-pin changes were introduced upstream.
- All 39 fork runtime source files under `src/`, `include/` and `scripts/` match the checkpoint byte-for-byte. The only runtime differences from that checkpoint are upstream's OTA client/header and the v1.0.5 build version. Current README/review provenance and the fork formatter baseline now identify the new upstream.
- All 20 fork checks passed on the rebased tree. Upstream Public access, authorization policy, resumed downloads, channel/version selection and actual Arduino boot-health checks passed; all 24 publishing/channel/recovery unit tests passed, including the new legacy bridge cases.
- Full production build, ESP32-S3 image parsing, application-slot size bound, and reviewed SDK archive/TLS linkage checks passed. The custom v1.0.5 application image and evidence remain in the private scratch artifacts, not in Git.
- This pass updates and validates source only. The scratch device continues running the previously installed v1.0.4-based polish build; no USB flash or OTA publication was performed. Previous hardware measurements and the design PNG remain associated with their recorded builds.

Evidence: scratch `.origami/artifacts/cine-upstream-sync-20261109/source-equivalence.json`, `validation.json`, `image-info.log`, `sdk-review.log`; build artifact `.origami/artifacts/cine-upstream-v1.0.5-20261109.bin`.


## Local workspace retirement — November 9, 2026

- Retired the standalone scratch directory. Private captures, reports, experiment sources, device records and older handoff source now live in `.local/lab/`, excluded from Git. Toolchains/downloads and retired build caches moved to `~/.cache/kami-os/`; the Python environment lives in `.venv`.
- Inventoried and verified 3,171 preserved files before location edits. Updated 205 path-bearing files and retained their exact original bytes in a verified private archive. Firmware, RAW and image bytes were unchanged. All 95 candidate/backup image references across 48 device records still match SHA-256; private symlinks resolve and active helper/state files no longer reference the deleted directory.
- Recreated the virtualenv for its canonical path, retaining all 22 package versions and regenerating console entrypoints. Dependency checks, PlatformIO 6.2.0, serial/deploy help and SDK package discovery pass. The portable root `build.sh` successfully generates the production compilation database: all 592 commands use relocated SDK paths, and the compiler binaries run. The generated database is stored with the caches. This was a tooling/layout check, not another full firmware rebuild or device flash.

Private migration inventory, original text archive and verification results are under `.local/lab/.origami/migration/`.

## Final design review and v1.0.5 installation — November 9, 2026

- The focused seven-screen sheet uses white annotations on black, actual Home/settings framebuffer captures, and production viewfinder renders with the supplied reference photo. Menu layout, glyph blending, safe-area placement, rounded rectangles and system chrome now use production routines. Both settings states match the installed device's entire 280×202 application area with zero differing pixels; dynamic status pixels are preserved in the captures. See [the design review notes](cine-camera-design-review.md) for reproduction and fidelity limits.
- All 20 existing fork checks plus the new design-sheet regression passed. The production build and reviewed SDK/TLS linkage check passed (84 linked archives). The diagram work changes no firmware runtime source.
- Installed the v1.0.5 production image built from `ce862cc` on the selected scratch device. Its previous v1.0.4 application was backed up and verified before writing; the new flash digest matched, and protected partition/NVS/OTA metadata and shipping data remained unchanged. Normal v1.0.5 boot was observed with no panic reported.
- On the installed build, three Home B → Cine opens restored Sunny/shutter defaults; settings editing, wheel WB changes, readout hiding, native 2048×1536 capture and metadata, A+B → Home, and the original Camera through the application selector passed. The device was left at Home. This smoke test does not establish a fix for the separately reported intermittent camera-availability issue.

Private evidence: `.local/lab/.origami/artifacts/cine-session-final-20261109/` contains the build checksum, pixel comparison, complete device captures, installed-device results and diagram manifest. Personal photos, captures, firmware images and device backups remain outside Git.

## Continuous Auto exposure and wheel hold — November 11, 2026

- Cine now starts in continuous hardware AEC/AGC with the darker vendor AE target −1. SS wheel movement selects manual shutter and freezes the current gain; a one-second wheel hold restores Auto and selects SS. WB changes preserve the exposure mode. Cine consumes the hold in every app view; the shared gesture and normal standby behavior outside Cine are unchanged.
- The new register transaction preserves live integration on Auto WB/target updates, including sub-millisecond exposures. It restores native VTS after a slow manual shutter, disables night extension, bounds gain/exposure, and rolls back register failures. No image-analysis loop or per-preview exposure polling was added. Auto capture telemetry is explicitly a live sample near frame receipt, not embedded frame metadata; sample failures do not disable subsequent capture.
- All 21 fork checks passed. The final HAL transaction, capture service and input handlers also passed their targeted sanitizer checks after the final edits. The production build and reviewed SDK/TLS linkage check passed (84 archives). Scratch install verified the rollback image, new flash checksum, unchanged protected metadata and normal boot with no reported panic.
- Hardware: startup/reopen Auto; 1/1000s and 1/10s manual selection; wheel-hold Auto restoration without standby; VTS 2095 → 1564 after slow manual exposure; WB preservation; settings hold; two native 2048×1536 JPEG saves with valid Auto/AE-level metadata; A+B → Home. The app was left at Home with the camera deinitialized.
- The dim current scene produced 37,567µs at gain 4× for both neutral and −1 targets, consistent with an exposure limit. This confirms the control transitions but does **not** establish a visual brightness difference in that scene. The two captures took approximately 435ms and 440ms; these are scene-specific timings, not a performance bound.

Private evidence: `.local/lab/.origami/artifacts/cine-auto-exposure-20261111/` contains the exact image/checksum, deployment log, device results, capture sidecars and first test attempt (a short-lived overlay expired before a delayed USB query). See [continuous-AE notes](cine-camera-auto-exposure.md) for controls and sensor limits.

## Cine shutter sampling — November 11, 2026

- Stock and Cine share the polled I2C button handler and release-to-shoot event. Cine previously left short presses unsampled during its synchronous native-resolution preview; stock services input during JPEG decode. Cine now samples during preview decode/drawing and can cancel a preview while a button is held, returning the frame before dispatching events. Interrupted JPEGDEC callbacks cannot expose partially decoded buffers or refresh the timestamp of an old displayed frame.
- All 22 fork checks, the production build, and the reviewed 84-archive SDK/TLS linkage check passed. The scratch-device install verified its exact rollback image and new flash, preserved protected metadata, and booted normally without a reported panic.
- Two same-device windows per path measured stock mean button-read gaps around 30ms, original Cine around 225ms, and updated Cine around 23ms. Updated Cine preview intervals stayed around 227–228ms versus 225ms previously; display-frame age increased about 20ms. See [input comparison](cine-camera-input.md) for maxima, polling tradeoffs and measurement limits.
- A B press/release through the sampled input handler saved a native 2048×1536 photo with valid metadata. An acknowledged wheel hold restored Auto without standby or a trailing click, and sampled A+B returned to Home. The camera was deinitialized and the device left at Home. These checks do not establish a physical tap failure rate or a fix for the separate intermittent camera-availability report.

Private evidence: `.local/lab/.origami/artifacts/cine-input-responsiveness-20261111/` retains both firmware candidates, checksums, verified deployment records, the initial 8ms experiment and final 20ms device measurements. Photographs, device logs and firmware binaries remain outside Git.
