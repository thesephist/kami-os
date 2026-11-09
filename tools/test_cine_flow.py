"""Check native Cine design states and lossless placement in the focused sheet."""

from pathlib import Path
import hashlib
import importlib.util
import json
import tempfile
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "cine_flow", ROOT / "tools/render_cine_flow.py"
)
flow = importlib.util.module_from_spec(spec)
spec.loader.exec_module(flow)

with tempfile.TemporaryDirectory(prefix="cine-flow-check-") as td:
    directory = Path(td)
    photo = directory / "photo.png"
    Image.new("RGB", (640, 480), (80, 112, 144)).save(photo)
    home = directory / "home.png"
    Image.new("RGB", (280, 240), (24, 32, 48)).save(home)
    frames_dir = directory / "native"
    frames_dir.mkdir()
    frames = flow.native_frames(photo, frames_dir, 40001)
    assert len(frames) == 17 and all(im.size == (280, 240) for im in frames.values())
    menu = frames["settings-ss"]
    # Actual scratch-device framebuffer captures, 2026-11-09: Sunny, 1/25s.
    # Exclude only the live battery/clock/radio band; no personal image is stored.
    captured_digests = {
        "settings-ss": "ac2abb2a695e5581d459c9aac49b384aa654328b2b8157312d92f252aeac5ed2",
        "settings-ss-edit": "75db36ad4144941b92dcd18abd43476888cdee5d4f2eafdf48eb1ac7181f3a0c",
    }
    for key, expected in captured_digests.items():
        assert (
            hashlib.sha256(frames[key].crop((0, 38, 280, 240)).tobytes()).hexdigest()
            == expected
        ), key
    # Native list geometry and the TFT midpoint-circle raster, not a smooth circle.
    assert menu.getpixel((14, 40)) == (255, 255, 255)
    assert menu.getpixel((19, 40)) == (0, 0, 0)
    assert menu.getpixel((14, 45)) == (0, 0, 0)
    # The system band must contain the real disconnected Wi-Fi, clock and battery.
    for box in [(24, 8, 46, 30), (95, 8, 180, 35), (210, 8, 253, 35)]:
        assert ImageChops.difference(
            menu.crop(box),
            Image.new("RGB", (box[2] - box[0], box[3] - box[1]), "white"),
        ).getbbox()
    assert (
        ImageChops.difference(
            menu.crop((0, 0, 280, 38)), frames["settings-ss-edit"].crop((0, 0, 280, 38))
        ).getbbox()
        is None
    )
    # Editing changes only the selected row; the rest of the native screen is stable.
    difference = ImageChops.difference(menu, frames["settings-ss-edit"]).getbbox()
    assert difference and difference[1] >= 40 and difference[3] <= 75
    output = directory / "flows.png"
    flow.render(photo, output, home, "focused-dark", shutter_us=40001)
    manifest = json.loads(output.with_suffix(".json").read_text())
    assert len(manifest["states"]) == 7 and manifest["scale"] == 2
    sheet = Image.open(output)
    assert sheet.size == (2160, 2440) and sheet.getpixel((0, 0)) == (0, 0, 0)
    for col, y, key in [
        (0, 310, "home"),
        (1, 310, "live"),
        (2, 310, "saved"),
        (0, 1000, "shutter"),
        (1, 1000, "wb-sunny"),
        (0, 1690, "settings-ss"),
        (1, 1690, "settings-ss-edit"),
    ]:
        x = 80 + col * 700
        source = Image.open(home) if key == "home" else frames[key]
        assert (
            ImageChops.difference(
                sheet.crop((x, y + 52, x + 560, y + 532)),
                source.resize((560, 480), Image.Resampling.NEAREST),
            ).getbbox()
            is None
        ), key
    # Native captures override source renders without interpolation or recoloring.
    captures = directory / "captures"
    captures.mkdir()
    Image.new("RGB", (280, 240), (48, 80, 112)).save(captures / "settings-ss.png")
    flow.render(photo, output, home, "focused-dark", captures)
    assert Image.open(output).getpixel((80, 1742)) == (48, 80, 112)
print(
    "PASS Cine flow: production native states, menu raster/chrome, seven screens, exact 2x placement and native capture overrides"
)
