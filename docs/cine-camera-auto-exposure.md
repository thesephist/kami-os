# Cine continuous auto exposure

Cine starts with the OV3660's continuous auto exposure and gain enabled. Sunny
white balance and the existing sRGB/vendor-matrix treatment remain the baseline.

- Turn the wheel in SS mode to take manual control, starting from a fresh sensor
  exposure reading. The wheel keeps the existing direction and 1/1000–1/10s range;
  the current gain is frozen when manual control starts.
- Click the wheel to switch SS/WB. Changing WB preserves Auto or Manual exposure.
- Hold the wheel for one second to restore Auto, select SS and reveal its readout.
  Cine consumes this gesture in all its screens, so it cannot trigger standby.
  After exiting Cine with A+B, the usual hold-to-sleep gesture works again.
- The SS value says `Auto` while automatic; manual mode shows the shutter speed.
  The existing two-second readout lifetime is unchanged.

Auto uses the vendor driver's next darker metering target (`ae_level=-1`): target
45 instead of neutral 55, with the vendor's corresponding stable/fast thresholds.
This biases exposure downward; it is not calibrated EV compensation or a new
highlight-weighted metering algorithm, and cannot guarantee that every highlight
will be retained. RAW/JPEG tone and color processing do not change.

AEC/AGC runs in the sensor, without per-preview I2C polling or userspace image
analysis. Auto restores the session's original VTS after a slow manual exposure,
keeps night-mode frame extension off, caps exposure at the shorter of native frame
capacity and 100ms, and caps real gain at 4x. The native 3MP profile is therefore
limited to approximately 75ms in Auto; manual can still reach 100ms. The gain
ceiling uses OV3660's Q4 real-gain register encoding (`0x40`), rather than the
generic driver's gain-ceiling enum value.

`cine shutter` reads fresh live exposure telemetry in Auto. Capture sidecars
record the automatic flag, AE target, sample validity and source. Auto's exposure
sample is read near frame receipt; it is not embedded per-frame sensor metadata.
A failed sample is recorded as unavailable and does not disable future captures.

USB research command `cine bench auto LEVEL` accepts -2..2 for comparisons using
the same hardware pipeline. Reopening Cine restores -1. It changes no preferences.
Register changes are bounded, checked and rolled back on failure.

Sources: the [Espressif OV3660 driver](https://github.com/espressif/esp32-camera/blob/master/sensors/ov3660.c)
and [OV3660 datasheet](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/unit/OV3660_CSP3_DS_1.3_sida.pdf),
sections 3.4 and 7.9. The implementation uses the locally pinned driver and sensor
register definitions; these links are supporting reference material.
