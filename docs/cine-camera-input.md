# Cine and stock Camera shutter input

On the current rev-4 device, A/B/C are sampled from the I2C button expander.
Both camera apps use the same HAL edge/chord detector, and both take a photo on
B release (`ButtonB_Tap`). The rotary encoder has separate GPIO interrupts; there
is no Cine-specific shutter interrupt priority.

Stock Camera's preview decoder checks/samples input roughly every 8ms and can
stop decoding when a button is held. Cine previously waited until its native
3MP preview read/decode/draw cycle completed. A short down/up pulse entirely
between successful samples cannot enter the event queue at all.

Cine now samples/enqueues input at 20ms intervals during JPEG decoding and RGB565 drawing, checks
for held buttons before and after frame acquisition, and stops preview decoding
when a button is held. Navigation/capture dispatch still happens in the normal
main loop after the frame is returned. An interrupted decode never swaps the
back buffer into view: the pinned JPEGDEC reports callback cancellation as a
successful decode, so interruption is checked separately. Uninterrupted image
pixels, capture settings and release-to-shoot behavior are unchanged.

This improves the preview sampling gaps, not every blocking operation. Camera
frame acquisition, capture/SD writes and ISP-control transactions can still
block the UI; no minimum physical pulse width is guaranteed.

## Same-device comparison

Developer USB commands `input cadence start` and `input cadence stop` collect
successful physical button snapshot times without allocation or per-sample
logging. Failed I2C reads are counted separately and do not shorten the measured
gaps. Collection is disabled by default. The report contains sample count,
maximum gap, total sampled span and uncovered time for hypothetical 20/50/100ms
pulses. The latter is a uniform-arrival-phase model, not a measured click failure
rate.

`cine bench input 0` temporarily disables the new Cine callbacks for a comparison
with its original polling path. `cine bench input 1` restores them; reopening Cine
also restores the normal enabled state. Neither command changes preferences.
Stock Camera is measured through its normal viewfinder with its current settings.
