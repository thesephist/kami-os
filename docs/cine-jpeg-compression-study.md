# Cine JPEG compression study · November 12, 2026

**Primary: QS8, baseline 4:2:2 JPEG at native 2048×1536.** This is the conservative quality/size compromise. **Secondary: QS10 with the same pipeline**, if storage savings become more important than retaining subtle texture. QS10 is documented, not another app mode.

QS is the OV3660 quantization-scale register: higher values mean stronger compression. It is not a percentage quality rating. The chosen change does not add software encoding or change resolution, frame clocks, exposure, WB, ISP gamma, vendor matrix, denoising or sharpening.

## Quality results

Twelve fixed 3MP references × eleven scales (3, 4, 5, 6, 8, 10, 12, 16, 20, 24, 32) × three sampling modes (4:4:4, 4:2:2, 4:2:0) × standard/optimized Huffman tables: **792 baseline JPEG encodes**, plus three progressive probes.

The corpus contains three independently rendered retained RAW8 frames, eight previously saved photos spanning street/foliage/skin/interiors/night/text/food, and one synthetic gradient/colored-edge stress image. Each comparison uses exactly the same RGB input. Full resolution is retained.

| Scale, 4:2:2 | RAW-derived median bytes saved vs QS6 | RAW-derived RGB PSNR | RAW-derived quarter-size luma SSIM | Already-JPEG median bytes saved vs QS6 re-encode |
|---|---:|---:|---:|---:|
| QS6 | 0.0% | 41.43 dB | 0.99765 | 0.0% |
| QS8 | 20.5% | 40.91 dB | 0.99717 | 6.2% |
| QS10 | 32.2% | 40.54 dB | 0.99651 | 11.8% |
| QS12 | 40.4% | 40.24 dB | 0.99600 | 22.8% |
| QS16 | 49.1% | 39.62 dB | 0.99488 | 36.3% |

QS8 costs about half a dB of median RAW-reference RGB PSNR versus QS6 and saves about a fifth of bytes on those renders. I found its native-pixel crops close to QS6; QS10 is a reasonable stronger alternative. More aggressive settings give diminishing runtime returns and increasingly discard texture/gradient detail. These are lossy settings, not a claim of perceptual identity.

The previously saved JPEGs are **incremental-loss tests**. Seven used sensor QS6 and one used QS12. Re-encoding an existing QS6 image at matching tables favors that baseline; their size and PSNR differences must not be mistaken for fresh-sensor results. RAW references avoid previous JPEG quantization, but their fixed host reconstruction is not a reproduction of the sensor's demosaic/denoising/color pipeline.

## On-device runtime

Fixed JPEG bytes were CRC-checked on upload and verified by exact SD readback. The actual pinned JPEGDEC preview path uses 1/8 IDCT → 256×192 → 224×168 RGB565. Decoder, input and output buffers stay at fixed PSRAM addresses within the photographic batch. Variant order is deterministic/randomized; each case has one warmup and normally eight measured repeats. Decode hashes must stay identical across repeats.

The sensor sweep and photographic file batch use separate temporary builds; ratios never mix those builds. The early synthetic QS6 probes use the preceding transport build and are pixel-correctness checks only.

The table reports the median of each scene's ratio to its QS6 baseline. This gives each photographic scene equal weight. Four photographic references cover all five main settings; extra sampling/Huffman cases and the synthetic stress probes are recorded separately.

| Scale | Paired scenes | JPEG bytes vs QS6 | SD read vs QS6 | Decode vs QS6 | SD write vs QS6 |
|---|---:|---:|---:|---:|---:|
| QS6 | 4 | 100.0% | 100.0% | 100.0% | 100.0% |
| QS8 | 4 | 93.5% | 94.1% | 98.8% | 96.0% |
| QS10 | 4 | 87.9% | 90.3% | 97.1% | 94.2% |
| QS12 | 4 | 76.2% | 80.1% | 90.1% | 83.6% |
| QS16 | 4 | 63.0% | 69.0% | 82.5% | 75.4% |

A separate native sensor sweep used the current dark stationary scene, fixed 10ms shutter/unity gain/Sunny WB, canonical ISP, unchanged 3MP framebuffer allocation, repeated candidate settings in different orders, and sensor color bars. The dark scene is useful for timings and table validation, not photographic tuning. First repeats are excluded below.

| Scale | Dark samples | Median bytes | Preview decode | JPEG SD write | DMA-start age at receipt |
|---|---:|---:|---:|---:|---:|
| QS6 | 12 | 92.4 KiB | 145.4 ms | 157.9 ms | 75.5 ms |
| QS8 | 12 | 77.8 KiB | 137.4 ms | 145.5 ms | 75.4 ms |
| QS10 | 12 | 72.2 KiB | 134.5 ms | 150.4 ms | 75.6 ms |
| QS12 | 12 | 70.0 KiB | 133.1 ms | 139.5 ms | 75.6 ms |
| QS16 | 12 | 67.0 KiB | 131.8 ms | 137.4 ms | 75.4 ms |

These are **component measurements**, not optical sensor-to-screen latency or stock-camera FPS comparisons. File benchmarks exclude acquisition, UI/input work, panel transfer, gallery bookkeeping and metadata. Sensor read waits can change discontinuously with frame phase; they are not an encoding-speed measurement. The roughly 75ms DMA age stays in place, so a smaller JPEG does not accelerate sensor readout. SD timings include substantial fixed overhead and card variability.

## Other compression choices

- **4:2:0:** reduces chroma detail and bytes. Colored edges and text are the important failure cases. It is not our supported current sensor-output configuration; a desktop JPEG trial does not prove that an equivalent sensor register change is safe. Keep 4:2:2.
- **4:4:4:** larger files, with limited benefit for these photographic inputs. Recompressed 4:2:2 originals cannot establish the benefit of retaining chroma they already lost.
- **Optimized Huffman:** identical decoded pixels in all 396 host pairs. It gives additional byte savings, but our sensor/driver path does not expose per-frame Huffman optimization. Recompressing every 3MP capture in software would work against fast saves. It is useful as an off-device archival possibility, not this firmware default.
- **Progressive:** all three probes decode through JPEGDEC's first-DC-scan thumbnail path. The sensor currently emits baseline JPEGs; progressively re-encoding captures on the device is not an efficient route to that preview benefit.

The initial x86 SIMD decoder run showed synthetic-image anomalies. All **795** images passed the scalar 32-bit host run, including identical preview hashes for every Huffman pair. The selected synthetic QS6/QS8 standard/optimized cases also passed on ESP32-S3 with the scalar reference hashes. No decoder dependency or SIMD setting was changed in production.

## Implementation and validation

Cine initializes the existing sensor stream with `core::cine::jpegQuantizationScale = 8`. Saved JPEG bytes are written directly. Sidecar schema v2 gains the additive `jpeg_quantization_scale` field; stock Camera defaults remain unchanged. Static RAM stays **223,364 bytes**; application flash is **5,284,713 bytes**, 20 bytes above the previous build.

QS8 production firmware was installed on the identified scratch device. Flash bytes and protected metadata were verified; native JPEG capture, metadata, controls, Photos and return to Home passed. Production diagnostics/profiling/tracing and the temporary transport changes are absent.

The temporary benchmark's larger USB receive buffer only accelerated transfers. It is absent from the production diff. Benchmarks and private photographs are retained under the ignored local lab; no personal images or device backups are published in this repository.

## Reproduce the host sweep

Use Pillow 13.0.0 and NumPy 2.5.4 for the recorded run. Render RAW once to a fixed RGB/sRGB lossless reference, or decode an existing JPEG once to PNG and label its previous compression explicitly. Then:

```sh
python3 tools/jpeg_compression_study.py \
  --reference scene=reference.png --output /tmp/jpeg-study
```

The host-only tool preserves dimensions, writes explicit quantization tables, verifies sampling, checks Huffman pixel equality, and records full-resolution RGB/luma PSNR, chroma error, preview-size error and local uniform 11×11 SSIM at quarter resolution. The SSIM value is a supporting proxy, not standard full-resolution perceptual ground truth. The public reproduction check matched JPEG sizes/pixel hashes exactly and floating metrics within 0.00001 on QS6/QS8 fixtures.

Native sensor JPEGs at QS4/6/8/10/12/16/24/32 exactly matched all 128 entries of `max(1, (standard JPEG table × QS + 8) // 16)`. Matching tables does not make desktop and sensor JPEG encoding bit-identical. The host model is limited to QS1…32; the recommendation rests on measured candidates, not extrapolation to the register's full range.

See [aggregate results](cine-jpeg-compression-results.json), the [host sweep tool](../tools/jpeg_compression_study.py), [OV3660 compression registers](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/unit/OV3660_CSP3_DS_1.3_sida.pdf) (§7.16, `0x4407`), [Pillow JPEG encoding options](https://pillow.readthedocs.io/en/stable/handbook/image-file-formats.html#jpeg), and [JPEGDEC's DC-only progressive support](https://github.com/bitbank2/JPEGDEC/blob/master/README.md).
