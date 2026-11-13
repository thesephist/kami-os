# Cine camera design sheet

The focused sheet shows seven key screens: Home, viewfinder, saved feedback,
shutter adjustment, Color adjustment, settings, and editing. It documents current
Color above SS, Color as the initial wheel mode, Auto exposure, wheel adjustment
to manual shutter, hold-wheel to restore Auto and select SS,
and the Sunny → Gold → Portra → Mono → Cool → Vivid cycle. Readouts snap away
2 seconds after adjustment. Startup and recovery states are described in text.

Export white annotations on black or black annotations on transparent using
`focused-dark` or `focused-transparent`. Screens remain native 280×240 pixels,
enlarged with nearest-neighbor sampling; text and arrows render at export resolution.
`--screen-scale 3` produces a 3240×3660 PNG; the default 2 produces 2160×2440.
Transparent exports retain fully opaque screenshots with clear space around them.

```sh
python3 tools/render_cine_flow.py \
  --layout focused-dark \
  --screen-scale 3 \
  --image /path/to/viewfinder-photo.jpg \
  --home /path/to/native-home-capture.png \
  --captured-screens /path/to/native-device-captures \
  --shutter-us 40001 \
  --output /path/to/cine-camera-flows.png
```

Use `--layout focused-transparent` for the second theme. Personal photos and
generated artifacts remain outside Git. Native PNGs named `settings-ss.png` and
`settings-ss-edit.png` replace source renders without pixel changes; the same
mechanism accepts other named states. Captures must be native 280×240. The JSON
manifest records each state's source; all individual native screens (including the six Color modes and recovery states)
accompany the PNG, listed under `exported_screens` in the manifest.

## Pixel fidelity

Viewfinder states call production `cine_view::image` and `chrome`, including the
Instrument font, RGB565 compositing, gear, photographer and feedback rendering.
Only the camera image is replaced by the supplied photo, reduced to the preview
buffer's actual 224×168 pixels before rendering. Auto/manual indicators use the
corresponding session state.

Menus use the current app Scene builder, production list layout, row presentation,
safe-area painter, and functions extracted from the pinned TFT library for glyph
coverage blending and rounded rectangles. System chrome uses production Wi-Fi,
battery and clock drawing. Source-rendered status values are representative;
device captures preserve actual values.

The November 12, 2026 export uses fresh complete device captures for Home and both
settings states, and current production renders with the original IMG_6997.jpg for
four viewfinder states. The supplied photograph is illustrative, not a new Kami
capture. An earlier November 9 menu raster comparison matched its then-current
application area exactly; those older framebuffer digests do not describe today's
Color/Auto menus.

Run `python3 tools/test_cine_flow.py` or `python3 tools/run_fork_checks.py --sdk`.
The focused check covers native menu geometry, system chrome, all six Color states,
exact 2×/3× screenshot placement, transparency, and unchanged capture overrides.
These tools do not change firmware behavior.

## Color-mode emulation in review screens

The supplied photograph is the Sunny reference and stays unchanged in Sunny.
Other Color screens transform only that photograph before firmware chrome is
drawn. The host model reads the current signed-Q7 CMX tables directly from
`cine_isp.h` and `cine_film_matrices.h`, reconstructs a Sunny CMX input, applies
the selected CMX, and converts YUV to RGB. Mono removes chroma and additionally
remaps the Sunny gamma output knots onto the Mono output knots. The endpoint
segment approximates the sensor's separate final gamma slope.

This is a relative display-domain illustration, not calibrated sensor simulation:
it cannot reproduce the original sensor WB, lost highlights, ISP processing
order, or JPEG artifacts. Firmware text, icons, selection colors and menus are
not graded. The same emulated backgrounds feed native screens and either diagram
theme, and the manifest describes the approximation.
