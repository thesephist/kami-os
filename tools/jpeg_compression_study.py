"""Host-only JPEG sweep; never linked into firmware.

Inputs should be fixed RGB/sRGB reference images. JPEG inputs measure additional
compression loss. RAW reconstruction belongs upstream of this compression study.
Requires Pillow and NumPy. See docs/cine-jpeg-compression-study.md.
"""

from pathlib import Path
import argparse
import hashlib
import io
import json
import re

import numpy as np
from PIL import Image, JpegImagePlugin


def digest(data):
    return hashlib.sha256(data).hexdigest()


def quantization_tables(scale):
    # OV3660 ROM tables match the standard JPEG tables at QS16. Verified against
    # native hardware JPEGs at QS4/6/8/10/12/16/24/32 in the November 2026 study.
    buffer = io.BytesIO()
    Image.new("RGB", (16, 16)).save(buffer, format="JPEG", quality=50)
    standard = Image.open(io.BytesIO(buffer.getvalue())).quantization
    return [
        [max(1, min(255, (value * scale + 8) // 16)) for value in standard[k]]
        for k in (0, 1)
    ]


def psnr(mse):
    return float(10 * np.log10(255**2 / max(float(mse), 1e-12)))


def ssim_quarter_luma(a, b):
    """Uniform 11x11 local SSIM at quarter resolution, not full-resolution SSIM."""
    if min(a.shape) < 11:
        return None

    def mean(x):
        integral = np.pad(x, ((1, 0), (1, 0))).cumsum(0).cumsum(1)
        return (
            integral[11:, 11:]
            - integral[:-11, 11:]
            - integral[11:, :-11]
            + integral[:-11, :-11]
        ) / 121

    ma, mb = mean(a), mean(b)
    va, vb = np.maximum(0, mean(a * a) - ma * ma), np.maximum(0, mean(b * b) - mb * mb)
    covariance = mean(a * b) - ma * mb
    return float(
        np.mean(
            (2 * ma * mb + 6.5025)
            * (2 * covariance + 58.5225)
            / ((ma * ma + mb * mb + 6.5025) * (va + vb + 58.5225))
        )
    )


def sweep(reference, scene, output, scales, sampling):
    with Image.open(reference) as source:
        source_format = source.format
        image = source.convert("RGB")
    rgb = np.asarray(image, dtype=np.float32)
    quarter = (max(1, image.width // 4), max(1, image.height // 4))
    gray = np.asarray(
        image.resize(quarter, Image.Resampling.BOX).convert("L"), dtype=np.float64
    )
    preview = np.asarray(
        image.resize((224, 168), Image.Resampling.BOX), dtype=np.float32
    )
    rows = []
    for subsampling in sampling:
        for scale in scales:
            tables = quantization_tables(scale)
            baseline = None
            for optimized in (False, True):
                variant = f"qs{scale:02d}-s{subsampling}-o{int(optimized)}"
                destination = output / "encoded" / f"{scene}-{variant}.jpg"
                image.save(
                    destination,
                    format="JPEG",
                    qtables=tables,
                    subsampling=subsampling,
                    optimize=optimized,
                    progressive=False,
                )
                with Image.open(destination) as decoded:
                    assert [decoded.quantization[k] for k in (0, 1)] == tables
                    assert JpegImagePlugin.get_sampling(decoded) == subsampling
                    pixels = np.asarray(decoded.convert("RGB"), dtype=np.float32)
                    decoded_hash = digest(pixels.astype(np.uint8).tobytes())
                    if optimized:
                        assert decoded_hash == baseline["decoded_sha256"]
                        row = dict(
                            baseline,
                            variant=variant,
                            optimized=True,
                            bytes=destination.stat().st_size,
                        )
                    else:
                        difference = pixels - rgb
                        y = (
                            0.299 * difference[:, :, 0]
                            + 0.587 * difference[:, :, 1]
                            + 0.114 * difference[:, :, 2]
                        )
                        cb = (
                            -0.168736 * difference[:, :, 0]
                            - 0.331264 * difference[:, :, 1]
                            + 0.5 * difference[:, :, 2]
                        )
                        cr = (
                            0.5 * difference[:, :, 0]
                            - 0.418688 * difference[:, :, 1]
                            - 0.081312 * difference[:, :, 2]
                        )
                        small = np.asarray(
                            decoded.resize((224, 168), Image.Resampling.BOX).convert(
                                "RGB"
                            ),
                            dtype=np.float32,
                        )
                        decoded_gray = np.asarray(
                            decoded.resize(quarter, Image.Resampling.BOX).convert("L"),
                            dtype=np.float64,
                        )
                        row = dict(
                            scene=scene,
                            variant=variant,
                            qs=scale,
                            subsampling=subsampling,
                            optimized=False,
                            bytes=destination.stat().st_size,
                            rgb_psnr=psnr(np.mean(difference * difference)),
                            y_psnr=psnr(np.mean(y * y)),
                            chroma_rmse=float(
                                np.sqrt((np.mean(cb * cb) + np.mean(cr * cr)) / 2)
                            ),
                            max_abs_error=int(np.max(abs(difference))),
                            preview_psnr=psnr(np.mean((small - preview) ** 2)),
                            ssim_quarter_luma_11x11=ssim_quarter_luma(
                                gray, decoded_gray
                            ),
                            decoded_sha256=decoded_hash,
                        )
                        baseline = row
                rows.append(row)
    return rows, dict(
        id=scene,
        path=str(reference.resolve()),
        source_sha256=digest(reference.read_bytes()),
        source_format=source_format,
        width=image.width,
        height=image.height,
        kind=(
            "Incremental JPEG loss" if source_format == "JPEG" else "Rendered reference"
        ),
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--reference", action="append", required=True, metavar="NAME=IMAGE"
    )
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--qs", default="3,4,5,6,8,10,12,16,20,24,32")
    parser.add_argument("--sampling", default="422,420,444")
    args = parser.parse_args()
    scales = [int(x) for x in args.qs.split(",")]
    modes = {"444": 0, "422": 1, "420": 2}
    if not scales or any(not 1 <= x <= 32 for x in scales):
        parser.error("This calibrated study model covers quantization scales 1..32")
    try:
        sampling = [modes[x] for x in args.sampling.split(",")]
    except KeyError:
        parser.error("Sampling must use 444, 422, or 420")
    references = []
    for value in args.reference:
        name, separator, path = value.partition("=")
        if (
            not separator
            or not re.fullmatch(r"[a-z0-9-]+", name)
            or not Path(path).is_file()
        ):
            parser.error("References require a unique lowercase NAME=existing-image")
        references.append((name, Path(path)))
    if len({name for name, _ in references}) != len(references):
        parser.error("Reference names must be unique")
    if args.output.exists() and any(args.output.iterdir()):
        parser.error(
            "Output must be absent or empty; existing experiment files are preserved"
        )
    (args.output / "encoded").mkdir(parents=True, exist_ok=True)
    rows, manifest = [], []
    for name, path in references:
        samples, source = sweep(path, name, args.output, scales, sampling)
        rows.extend(samples)
        manifest.append(source)
        (args.output / "metrics.json").write_text(json.dumps(rows, indent=2) + "\n")
        (args.output / "manifest.json").write_text(
            json.dumps(manifest, indent=2) + "\n"
        )
    print(f"Wrote {len(rows)} baseline JPEG encodes with full-resolution metrics")


if __name__ == "__main__":
    main()
