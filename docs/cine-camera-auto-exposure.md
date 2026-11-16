# Cine continuous auto exposure

Cine starts with the OV3660's continuous auto exposure and gain enabled. Sunny
white balance and the existing sRGB/vendor-matrix treatment remain the baseline.

- The wheel starts in Color, with its readout above SS.
- Turn the wheel in SS mode to take manual control, starting from a fresh sensor
  exposure reading. The wheel keeps the existing direction and 1/1000–1/10s range;
  the current gain is frozen when manual control starts.
- Click the wheel to switch Color/SS. Changing Color preserves Auto or Manual exposure.
- Hold the wheel for one second to restore Auto, select SS and reveal its readout.
  Cine consumes this gesture in all its screens, so it cannot trigger standby.
  After exiting Cine with A+B, the usual hold-to-sleep gesture works again.
- The SS value says `Auto` while automatic; manual mode shows the shutter speed.
  The existing two-second readout lifetime is unchanged.

Auto uses the vendor driver's next darker metering target (`ae_level=-1`): target
45 instead of neutral 55, with the vendor's corresponding stable/fast thresholds.
This biases exposure downward; it is not calibrated EV compensation or a new
highlight-weighted metering algorithm, and cannot guarantee that every highlight
will be retained. The canonical JPEG tone and color treatment is fixed.

AEC/AGC runs in the sensor, without per-preview I2C polling or userspace image
analysis. Auto restores the session's original VTS after a slow manual exposure,
keeps night-mode frame extension off, caps exposure at the shorter of native frame
capacity and 100ms, and caps real gain at 8x. The native 3MP profile is therefore
bounded by approximately 75ms in Auto; the sensor's anti-flicker band limits can
impose a lower effective maximum. Manual can still reach 100ms. The gain
ceiling uses OV3660's Q4 real-gain register encoding (`0x80`), rather than the
generic driver's gain-ceiling enum value.

The sensor chooses shutter and gain together from its metered image level.
Raising the ceiling from 4x to 8x permits one additional stop of amplification;
it does not change the target or impose shutter-first or gain-first priority.
Anti-flicker constraints can quantize shutter choices. The exact allocation
policy is not specified by our code. Gain through 8x is in the sensor's analog
range; no additional digital gain is enabled. Manual shutter freezes the latest
live gain, including values above 4x, until Auto is restored.

Capture sidecars record the automatic flag, AE target, sample validity and source. Auto's exposure
sample is read near frame receipt; it is not embedded per-frame sensor metadata.
A failed sample is recorded as unavailable and does not disable future captures.

The AE target is fixed at -1; the historical target-comparison command has been
removed. Register changes are bounded, checked and rolled back on failure.

Sources: the [Espressif OV3660 driver](https://github.com/espressif/esp32-camera/blob/master/sensors/ov3660.c)
and [OV3660 datasheet](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/unit/OV3660_CSP3_DS_1.3_sida.pdf),
sections 3.4 and 7.9. The implementation uses the locally pinned driver and sensor
register definitions; these links are supporting reference material.
