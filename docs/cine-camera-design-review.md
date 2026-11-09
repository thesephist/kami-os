# Cine camera design sheet

The focused sheet shows seven key screens on black with white annotations: Home,
viewfinder, saved feedback, shutter adjustment, white-balance adjustment, settings,
and editing. Screens remain native 280×240 pixels, enlarged exactly 2× with nearest
neighbor sampling. Repeated WB presets, startup and error transitions are described
in text rather than repeated screenshots.

Generate the sheet with a local photo; personal photos and generated artifacts
remain outside Git:

```sh
python3 tools/render_cine_flow.py \
  --layout focused-dark \
  --image /path/to/viewfinder-photo.jpg \
  --home /path/to/native-home-capture.png \
  --captured-screens /path/to/native-device-captures \
  --shutter-us 40001 \
  --output /path/to/cine-camera-flows.png
```

`--captured-screens` is optional. Native PNGs named `settings-ss.png` and
`settings-ss-edit.png` replace those source renders without pixel changes; the same
mechanism accepts other named states. Home captures must also be native 280×240.
The JSON manifest records which screens came from the device. Individual native
screens are saved alongside the diagram.

## Pixel fidelity

Viewfinder states call the production `cine_view::image` and `chrome` routines,
including the actual Instrument font, RGB565 compositing, gear, photographer and
feedback rendering. Only the camera image is replaced by the supplied photo,
reduced to the preview buffer's actual 224×168 pixels before rendering.

Menus use the current app Scene builder, production list layout, row presentation,
safe-area painter, and functions extracted from the pinned TFT library for glyph
coverage blending and rounded rectangles. System chrome uses the production
Wi-Fi, battery and clock drawing functions. Source-rendered status values are a
representative snapshot; device captures preserve the actual values.

On the scratch Kami on 2026-11-09, Sunny / 1/25s settings browse and edit renders
matched the entire captured application area (280×202 below the status strip)
with **zero differing pixels**. Their RGB digests are regression fixtures in
`tools/test_cine_flow.py`; no device photo or screenshot is committed. The final
sheet uses complete device captures for Home and both settings states, and source
renders for the four photo-based states. This is not a claim that a photograph of
the physical LCD matches RGB values, nor that the supplied photo was shot on Kami.

Run `python3 tools/test_cine_flow.py` or the full
`python3 tools/run_fork_checks.py --sdk` suite. The diagram check covers captured
menu digests, system chrome, native dimensions, exact 2× placement and unaltered
capture overrides. These changes affect review tooling only, not device behavior.
