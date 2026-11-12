# Stock Camera vs Cine camera

Scope: OV3660 scratch hardware, upstream `cf3d1ce8ecfd7d4e4c63c52c9e25358dcdc52637`, and the selected **Cine** mode. Stock means the separate built-in Camera app with factory Default settings unless stated otherwise. Stock's saved settings can change its behavior.

## Image pipeline and controls

| Area | Stock Camera | Cine mode |
| --- | --- | --- |
| Capture source | Sensor ISP → sensor JPEG; default VGA (640×480), selectable HD/Max, subject to sensor/memory limits. | Same ISP/JPEG hardware; always native 2048×1536, JPEG quality register **6**. No RAW reconstruction, software resizing, grading, sharpening, Filmic curve, or JPEG re-encoding on normal saves. |
| JPEG quality | HAL chooses by size: VGA 6, HD 14, high resolution 12. Lower register value means higher quality. | Fixed 6, including preview source. Saves the complete frame, not the cropped screen. |
| Exposure | Continuous sensor auto exposure/gain; Default AE target 0, gain ceiling index 0; night mode may extend frame intervals. Bright/Dark/Manual profiles available. | Starts with continuous sensor AE/AGC at the darker vendor target **−1**, real gain capped at 4×. Turning the SS wheel freezes the current gain and selects **1/1000–1/10 s** manually; holding the wheel restores Auto. Auto keeps native frame timing (about 75ms maximum exposure on the 3MP profile), with night extension disabled. |
| White balance | Default Cloudy; Bright uses Sunny, Dark uses Auto; Manual exposes all presets. | Starts Sunny; WB wheel mode cycles **Sunny → Cloudy → Office → Home → Sunny** in either direction. Auto is excluded from normal app controls. Presets are vendor settings, not measured Kelvin values. |
| Tone transfer | Vendor gamma; Default contrast −1, brightness 0. | Fifteen sensor gamma points sampled from sRGB, plus the final-segment slope; nominal EC 0. Bypasses SDE contrast/brightness. This is an approximation, not calibrated linear RAW → sRGB. |
| Color | Vendor saturation −1 CMX table (about 90% of neutral chroma coefficients); the driver implements saturation by rewriting the matrix. | Vendor **+1 saturation CMX** table (`1d60030d84918a76140198`), selected as **C+0 S+1 · B0**; SDE bypassed (no added contrast, brightness offset or black cutoff). No fitted matrix or software Sunny/display-domain transform. “Sunny” is the default vendor WB preset. |
| Detail | Vendor normal sharpness setting 0. | Manual sharpening offsets set to zero in sensor registers; no userspace sharpening. Normal sensor denoise remains. |
| Lens shading | Enabled. | Disabled throughout, including initialization. |
| Common corrections/orientation | Bad/white pixel correction and black-level processing, color interpolation; mirrored horizontally, no vertical flip. | Same basic sensor operations/orientation; gamma/color control registers differ as above. |
| Focus | Autofocus preference supported on AF-capable sensors; OV3660 is fixed focus. | AF disabled; this tuning is OV3660-specific and rejected on unsupported sensors. |
| Preview | VGA source, stock JPEG decoder/direct display path; high-resolution capture may stop/reinitialize the camera and return to VGA afterward. | Keeps sensor in 3MP JPEG mode. Stock JPEGDEC library at 1/8 IDCT (256×192), samples to 224×168 RGB565, then fills/crops to the same 280×240 viewfinder. Preview uses the same exposure/WB/ISP as the saved frame, with lower detail. |
| Memory/lifecycle | Shared camera service reserves peripherals; settings pause the driver while retaining the reservation. | Same reservation; retained across Cine settings; the driver and preview buffers remain initialized. Persistent PSRAM decoder and two 224×168 buffers (75,264 bytes extra PSRAM for the back buffer); complete-frame swaps, no partially decoded display. No per-frame decoder allocation. |
| Viewfinder UI | Shared brackets, capture effects, photographer overlay. A returns; B photo/hold for video; C settings. Wheel does not change live exposure. | Stock framing/effects, with a complete native20px eight-tooth gear at A. B photo; C toggles shutter/WB wheel with the standard Select haptic. Large values and small SS/WB labels appear only after adjustment/mode changes (2s hold, immediate show/hide; no fade); active row uses theme kUiEdit blue, no arrow. Compact rounded50%-opaque backing on wheel rows and photo/save caption; clear image after hiding. No bottom metrics row. Sunny/shutter default, inverted shutter direction, A+B exit; no video. |
| Settings | Persistent camera profiles, manual controls, capture resolution and supported video/focus options. | Standard ListView rows for shutter, WB and Back to camera; C toggles editing, wheel adjusts, A exits editing/backs out. All rows use the normal menu font. Session-local WB/shutter/wheel mode; settings preserve them; reopen defaults to Cine, shutter wheel and Sunny WB with fresh exposure settling. No writes to Stock preferences or sensor OTP. |
| Saved files | Shared `/photos/IMG_XXXXX.jpg` numbering; photo review and cloud-upload integration. | Same photo allocator/path; best-effort `/cine/SHOT_XXXXX.json` sidecar with tuning/timings. Capture feedback returns to preview; no cloud enqueue. JSON failure does not undo an already saved JPEG. |
| Availability/tools | Main Camera entry and its existing diagnostics. | Custom Apps → Cine camera; additional USB diagnostics for exposure, timings and historical RAW/resize/color experiments. Those experiments are outside normal Cine mode. |

**One normal mode:** the Stock/Cine picker has been removed. The separate built-in Camera app remains available. Historical reference/RAW paths are gated USB diagnostics, not user-selectable modes.

**Startup:** an immediate stock-style app-opening splash precedes camera initialization. Cine observes AE over separate frames (at least 450 ms and three transitions within 3% exposure×gain), with the previous 1.1 s settling budget as fallback. After ISP writes, transitional frames are discarded until a preview with a DMA timestamp beyond the two-frame freshness guard is decoded. The controls screen no longer closes/reopens the driver. Device-side phase timings are available through `cine startup`.

**Tone comparison:** `cine bench isp vendor vendor 0 SHUTTER_US GAIN_Q4` restores the 16 gamma bytes read from the sensor immediately after normal initialization. `srgb` writes the current approximation. Both paths otherwise use identical Cine settings. This isolates gamma, not every difference from the complete Stock app. Vendor gamma is unavailable if the initial read fails; nonzero gamma EV is rejected for the exact vendor copy. Neither path can recover sensor-clipped highlights.

## Memory/crash review

Reviewed app events, sensor/frame ownership, preview callbacks, capture buffers, ISP transactions and hidden research paths. Fixes in this revision:

- **High:** Mode/settings transitions released the peripheral reservation before camera reinitialization, allowing radios/background work to compete for scarce DMA memory. Keep the reservation until app exit; fail closed and retry if driver teardown fails.
- **Medium:** Missing null/length/marker checks and unsafe callback coordinate arithmetic could let a malformed frame reach copying/decoding or exceed callback bounds. Validate frames and use subtraction-based bounds/stride checks; validate diagnostic numeric inputs before narrowing.
- **Medium:** Decoder failure could modify the displayed frame. Decode into a back buffer and swap only on success. Keep the decoder in PSRAM rather than allocating internal-first every frame.
- **Medium:** Initial-preview failure relied on the app to release resources. Make service failures clean up themselves; add scoped-session cleanup and prevent copying an owning Session.
- **Low:** Extreme diagnostic EV adjustments could overflow; off-ladder shutter values could miss a wheel endpoint. Saturate safely and apply the exact endpoint.

[Validation and hardware results](cine-camera-validation.md) are recorded separately. These checks exercise Cine's integration; they do not establish memory safety of the entire ESP32 SDK or third-party codec. A persistent hardware/I2C teardown failure retains driver resources rather than restarting radios over a still-live camera.

## Review map

- `src/apps/cine_camera_app.cpp`, `include/ui/cine_camera_view.h`: controls, rendering and serial diagnostics.
- `src/services/cine_camera*.cpp`: lifecycle, borrowed-frame ownership, direct JPEG capture, ISP configuration and preview.
- `include/core/cine_frame.h`: sensor JPEG validation. `include/core/cine_shutter.h`: bounded shutter math.
- `src/hal/hal_camera.cpp`: sensor register transaction/readback/rollback and physical exposure controls.
- `src/core/cine_tone.cpp`: selected ISP gamma seed plus historical RAW experiments; historical software looks are not the selected path.

## Fork maintenance pass — November 9, 2026

The managed source is now `thesephist/kami-os`, with the original Git history retained. Tag `cine-baseline-c0-s1-b0-20261109` preserves the device-tested state before this pass; use `git diff -w cine-baseline-c0-s1-b0-20261109..main` to review the cleanup with whitespace ignored.

- Removed the unreachable paired/neutral comparison capture branches, unused WB override, look/color cycling methods without callers, unused preview accessors/state, a duplicate wheel-value formatter, and an ignored UI argument. Reachable photo paths, JSON field names/values, UI, and USB commands retain their behavior.
- Replaced positional ISP numeric/boolean options with `IspConfig`, `GammaCurve`, and `ColorMatrix`. The matrix type makes the old fitted-matrix/vendor-saturation conflict unrepresentable. Named internal modes distinguish native ISP JPEG, stock diagnostics, and binned/native RAW.
- Kept the RAW/ISP/resize benchmark tools because they remain reachable USB experiments. Filmic and fitted-matrix diagnostics remain explicitly separate from the selected sRGB/vendor+1 baseline. Removing those tools would change the experiment interface.
- Formatted only fork-added C++ files, excluding the copied JPEG codec. Upstream source outside our additions was not reformatted. Portable sanitizer fixtures and a focused CI runner now live in `tools/`; they need no retained personal photos or scratch paths.

The selected image pipeline remains **C+0 S+1 · B0**, Sunny by default, native 2048×1536 JPEG, with the same shutter/WB controls and transient blue overlay.

Home B opens Cine camera; the original Camera remains in the main application selector. Opening/error screens use the accurate “A: back” hint.

Upstream sync: rebased onto v1.0.5 plus the legacy Staging recovery fix (`cf3d1ce`). Upstream changed OTA access/recovery and release tooling; all camera, gallery, printer and Home-shortcut source files match the pre-rebase fork exactly. Earlier hardware measurements in the validation log describe their named builds.

Continuous-AE control, target and telemetry details: [Cine auto exposure](cine-camera-auto-exposure.md). Hold-to-sleep is consumed by Cine; outside Cine it retains the firmware's usual behavior.
