# Cine Color modes

The Color wheel cycles and wraps in both directions:

**Sunny → Gold → Portra → Mono → Cool → Vivid → Sunny**.

Cine starts on Sunny with the wheel controlling Color, displayed above SS. Turn
to cycle Color; click the wheel to switch to shutter. Click again to return to Color. The blue active row and
both readouts appear for two seconds, then snap away. A opens Settings, which also
has a Color row. Hold the wheel to restore Auto shutter/gain. Reopening starts on
Sunny again; color selection is session-local.

Every mode uses the same fixed **Sunny sensor WB gains (1504 / 1040 / 1344)**.
There is no Auto/Office/Home WB cycling. Only the selected sensor CMX and, for
Mono, gamma change. Preview and saved 2048×1536 JPEG share those sensor settings.

| Mode | Selected reference | ISP implementation |
|---|---|---|
| Sunny | Existing baseline | Original sRGB curve and vendor +1 matrix with the shared greenward tint correction |
| Gold | G5, Gold color only | Quantized Gold color fit plus shared greenward tint correction; original Y/gamma |
| Portra | P5, Portra 400 color only | Quantized Portra 400 color fit plus shared greenward tint correction; original Y/gamma |
| Mono | M5, classic silver | Original Y coefficients, zero chroma, classic contrast composed into gamma knots |
| Cool | C5, balanced green | Exact selected matrix bytes; original gamma |
| Vivid | V5, neutral vivid | Selected V5 matrix plus shared greenward tint correction; original gamma |

## Approximation and runtime

Gold and Portra approximate nonlinear references; they cannot reproduce the full
LUT with one matrix. We fit the UV rows without an affine offset, preserving the
Sunny Y row, to 96,000 natural-image colors from eight retained images, 15,000
moderate synthetic colors, and 15,000 grays. A fixed RNG seed makes sampling
repeatable. Coefficients are quantized to signed Q7 and encoded into the OV3660's
nine magnitudes plus two sign bytes. Neutral casts are model-derived, not changes
to sensor white balance. The comparison scenes below were not the eight images
used to sample training colors.

Original fit errors before the shared tint correction, against G5/P5 reference
renders (absolute RGB 8-bit code differences; host model, not device calibration):

| Scene | Gold mean / p95 | Portra mean / p95 |
|---|---|---|
| daylight-raw | 3.9 / 10.9 | 3.6 / 10.2 |
| night-raw | 7.3 / 22.5 | 7.0 / 22.0 |
| production-jpeg | 5.6 / 15.0 | 4.9 / 12.9 |

Mono's desired curve is sampled at the ISP's existing gamma knots. Its appearance
also depends on the sensor's processing order and highlight tail; it is not a
pixel-exact implementation of a final-RGB luminance curve. There is no grain,
sharpening or extra full-resolution userspace processing.

The selected tables occupy a few hundred read-only bytes. Mode changes use the
existing bounded ISP transaction with register readback and rollback. There is
no new task, heap allocation, image buffer, per-frame CPU color operation, LUT
lookup, software encoder or camera resolution switch. Continuous AE/AGC,
manual shutter/gain handoff, 8× gain ceiling, QS8 encoding and decoder ownership
remain the existing production path. Captures wait for fresh frames after a
profile change. Every photo's version-3 metadata records `color_mode`,
`color_name`, `color_version: 2`, and `wb_preset: 1` alongside actual exposure.

## Reproducible profile data

The canonical table lives in `include/core/cine_isp.h`; Gold/Portra fitted data is
isolated in `include/core/cine_film_matrices.h` under its own data license.
Sunny/Cool/Vivid use the normal gamma; Mono's gamma and all selected matrices:

- **Sunny** CMX bytes: `29,96,3,14,134,145,137,122,20,1,152`.
- **Gold** CMX bytes: `29,96,3,30,120,146,139,84,59,1,152`.
- **Portra** CMX bytes: `29,96,3,17,145,158,123,53,75,1,152`.
- **Mono** CMX bytes: `29,96,3,0,0,0,0,0,0,0,0`.
- **Cool** CMX bytes: `29,96,3,10,115,137,127,121,19,1,152`.
- **Vivid** CMX bytes: `29,96,3,16,169,188,177,161,26,1,152`.

Mono gamma bytes: `24,40,65,101,115,128,139,148,157,165,180,193,215,231,244,16`.

Gold/Portra data derives from **spektrafilm by Andrea Volpato**,
https://github.com/andreavolpato/spektrafilm, commit
`3bb2c2d2801ff68b92019cf1dbcbb133d60832bc`, **CC BY-SA 4.0**. The source profiles
were baked to sRGB-in/sRGB-out 33³ LUTs with Kodak Portra Endura paper,
`stops_above_midgray=log2(1/0.18)` (unity exposure gain), deterministic LUT mode,
and no spatial/stochastic effects. G5/P5 retain modeled chroma while restoring
original encoded luminance. We then fit/quantize these derived targets. No
simulator source or full LUT is compiled into firmware. These are creative
approximations, not Kodak-endorsed or calibrated film reproductions.

See `third_party/licenses/spektrafilm/SPEKTRAFILM_LICENSE.txt` and `CHANGELOG.txt`
for the unmodified data license and modification record. Source code outside the
identified matrix data retains its existing license. Private study artifacts
contain source hashes, exact recipes and visual comparisons; private photographs
and firmware images do not belong in Git.

## Checks

`tools/run_fork_checks.py --sdk` exercises the production input handlers, service
lifecycle, all six profile bytes, signed wheel wrap including integer limits,
fixed WB, manual/Auto exposure preservation, metadata, and every-register
rollback under ASan/UBSan. The native renderer covers all six Color names and
keeps the two-second popup timing and ordinary Instrument value font. Production
symbol checks ensure instrumentation remains excluded. Host tests do not replace
installation, real capture, appearance, or interactive device validation.

## Installed-device validation — 12 November 2026

The production firmware was installed on the selected rev-4 N16R8 scratch Kami.
Application bytes and normal boot were verified; protected partition, NVS and
OTA-selection bytes were unchanged. A verified previous application is retained
privately for recovery. Production symbol checks confirm diagnostics/profiling
and input tracing are absent. All 21 fork checks passed.

The device captured all six modes at **50,001 µs / 8× gain**, with unchanged
Sunny WB and QS8 native 2048×1536 4:2:2 JPEG. Mono decoded to exactly equal
RGB channels. Forward/reverse/multi-step wrap, settings editing, popup expiry,
hold-to-Auto with Color retained, and reopen-to-Sunny all passed. The shared
photo allocator continued its existing numbering. Subjects moved between
frames, so the photographic comparisons are not pixel-aligned accuracy tests.

Static RAM remains **223,364 bytes**; flash use is **5,284,825 bytes**, 112 bytes
above the previous production build. No sensor-to-screen timing benchmark was
performed for this parameter-only change. The device was left on Home, camera
closed, with USB service access closed.

## Shared tint correction

The accepted C3→C5 purple-cast adjustment is now shared by Sunny, Gold, Portra
and Vivid. Nominally it is `U -= 0.025 * Y; V -= 0.045 * Y`. Its exact
quantized C5-minus-C3 signed-Q7 deltas are `U [-1,-2,0]`, `V [-1,-4,0]`.
Cool already contains the correction and is unchanged; Mono removes chroma
and is unchanged. This applies the correction once per look rather than
re-applying it to Cool or tinting Mono. The sensor Sunny WB gains, all Y rows,
gamma curves, shutter/gain behavior and JPEG path are unchanged. This is a
shared aesthetic correction, not a measurement/calibration of sensor WB.
Shot metadata uses color revision 2 to distinguish the revised looks.

The shared-tint firmware was rebuilt and installed on the scratch Kami. All 21
fork checks and all six native on-device captures passed at color revision 2,
including unchanged WB/exposure, neutral Mono, controls/wrap and Auto restore.
Protected metadata was preserved. RAM/flash use match the preceding build,
and the device was left on Home with USB service closed.
