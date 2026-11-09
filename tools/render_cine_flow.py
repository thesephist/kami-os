"""Render the current Cine UI into one shareable flow PNG; never embeds the input photo in Git."""

from pathlib import Path
import argparse, json, math, struct, subprocess, tempfile, textwrap
from PIL import Image, ImageDraw, ImageFont, ImageOps

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    i = source.index("{", start)
    depth = 1
    end = i + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def native_frames(image, output):
    with tempfile.TemporaryDirectory(prefix="cine-flow-render-") as td:
        p = Path(td)
        (p / "Arduino.h").write_text(
            "#pragma once\n#include <stdint.h>\n#include <stddef.h>\n#include <pgmspace.h>\n"
        )
        (p / "pgmspace.h").write_text(
            "#pragma once\n#define PROGMEM\n#define pgm_read_byte(a) (*(const unsigned char*)(a))\n"
        )
        (p / "Fonts/GFXFF").mkdir(parents=True)
        (p / "Fonts/GFXFF/gfxfont.h").write_text(
            "#pragma once\n#include <stdint.h>\nstruct GFXglyph{uint32_t bitmapOffset;uint8_t width,height,xAdvance;int8_t xOffset,yOffset;};struct GFXfont{uint8_t* bitmap;GFXglyph* glyph;uint16_t first,last;uint8_t yAdvance;};\n"
        )
        photo = (
            ImageOps.exif_transpose(Image.open(image))
            .convert("RGB")
            .resize((224, 168), Image.Resampling.BOX)
        )
        pixels = photo.tobytes()
        (p / "photo.rgb565").write_bytes(
            b"".join(
                struct.pack(
                    "<H",
                    ((pixels[i] >> 3) << 11)
                    | ((pixels[i + 1] >> 2) << 5)
                    | (pixels[i + 2] >> 3),
                )
                for i in range(0, len(pixels), 3)
            )
        )
        source = (ROOT / "src/apps/cine_camera_app.cpp").read_text()
        code = (
            (ROOT / "tools/fixtures/cine_flow_renderer.cpp")
            .read_text()
            .replace(
                "// @APP_FUNCTIONS@",
                "\n".join(
                    function(source, s)
                    for s in [
                        "void shutterValue(",
                        "ui::ListItem settingsItem(",
                        "void build(",
                    ]
                ),
            )
        )
        (p / "render.cpp").write_text(code)
        subprocess.run(
            [
                "c++",
                "-std=c++17",
                "-O1",
                "-DORIGAMI_INSTRUMENT_FONT",
                "-I" + str(p),
                "-I" + str(ROOT / "include"),
                "-I" + str(ROOT / "lib/TB_TFT_eSPI"),
                str(p / "render.cpp"),
                str(ROOT / "src/ui/list_row.cpp"),
                "-o",
                str(p / "render"),
            ],
            check=True,
        )
        subprocess.run(
            [str(p / "render"), str(p / "photo.rgb565"), str(output)], check=True
        )
    return {f.stem: Image.open(f).convert("RGB") for f in output.glob("*.ppm")}


def render(image, output, home=None):
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="cine-flow-frames-") as td:
        frames = native_frames(image, Path(td))
        if home:
            frames["home"] = (
                Image.open(home)
                .convert("RGB")
                .resize((280, 240), Image.Resampling.NEAREST)
            )
        else:
            # Context fallback is explicitly schematic; the app frames use source renders.
            h = Image.new("RGB", (280, 240), "white")
            d = ImageDraw.Draw(h)
            d.rounded_rectangle((64, 81, 115, 145), radius=18, fill="#173256")
            d.rounded_rectangle((159, 81, 210, 145), radius=18, fill="#173256")
            frames["home"] = h
        W, H = 3480, 4220
        sheet = Image.new("RGB", (W, H), "#f3f1ed")
        draw = ImageDraw.Draw(sheet)
        ink = "#182431"
        muted = "#586775"
        line = "#b7c3cd"
        accent = "#205eaa"
        teal = "#17665f"
        fontroot = Path("/usr/share/fonts/truetype/dejavu")

        def font(size, bold=False):
            return ImageFont.truetype(
                str(fontroot / ("DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf")),
                size,
            )

        def text(x, y, s, size=24, color=ink, bold=False):
            draw.text((x, y), s, font=font(size, bold), fill=color)

        def wrap(x, y, s, width, size=23, color=muted, lineheight=32):
            words = s.split()
            row = ""
            for word in words:
                trial = (row + " " + word).strip()
                if draw.textlength(trial, font=font(size)) > width and row:
                    text(x, y, row, size, color)
                    y += lineheight
                    row = word
                else:
                    row = trial
            if row:
                text(x, y, row, size, color)
                y += lineheight
            return y

        def pill(x, y, s, color=accent):
            w = int(draw.textlength(s, font=font(21, True))) + 28
            draw.rounded_rectangle((x, y, x + w, y + 39), radius=18, fill="#e3eaf3")
            text(x + 14, y + 5, s, 21, color, True)
            return w

        def arrow(points, label=None, where=None, color=accent):
            draw.line(points, fill=color, width=4, joint="curve")
            a, b = points[-2:]
            angle = math.atan2(b[1] - a[1], b[0] - a[0])
            n = 14
            draw.polygon(
                [
                    b,
                    (
                        b[0] - n * math.cos(angle - 0.48),
                        b[1] - n * math.sin(angle - 0.48),
                    ),
                    (
                        b[0] - n * math.cos(angle + 0.48),
                        b[1] - n * math.sin(angle + 0.48),
                    ),
                ],
                fill=color,
            )
            if label:
                x, y = where if where else ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2 - 22)
                chunks = label.split("\n")
                tw = max(draw.textlength(s, font=font(18, True)) for s in chunks)
                draw.rounded_rectangle(
                    (x - tw / 2 - 7, y - 3, x + tw / 2 + 7, y + len(chunks) * 25),
                    radius=7,
                    fill="#f3f1ed",
                )
                for i, s in enumerate(chunks):
                    text(
                        x - draw.textlength(s, font=font(18, True)) / 2,
                        y + i * 25,
                        s,
                        18,
                        color,
                        True,
                    )

        cards = {}

        def card(col, y, key, title, number, body, badge=None):
            x = 88 + col * 676
            draw.rounded_rectangle(
                (x, y, x + 600, y + 700),
                radius=22,
                fill="white",
                outline="#d8dfe4",
                width=2,
            )
            text(x + 20, y + 15, f"{number:02d}", 23, accent, True)
            text(x + 71, y + 15, title, 25, ink, True)
            frame = frames[key].resize((560, 480), Image.Resampling.NEAREST)
            sheet.paste(frame, (x + 20, y + 58))
            draw.rounded_rectangle(
                (x + 17, y + 55, x + 583, y + 541), radius=8, outline="#253442", width=3
            )
            wrap(x + 20, y + 555, body, 560, 23)
            if badge:
                pill(x + 20, y + 652, badge)
            cards[key] = (x, y)
            return x, y

        def context(col, y, title, lines):
            x = 88 + col * 676
            draw.rounded_rectangle((x, y, x + 600, y + 700), radius=22, fill="#e7ecee")
            text(x + 32, y + 30, title, 32, ink, True)
            yy = y + 96
            for heading, body in lines:
                text(x + 32, yy, heading, 25, teal, True)
                yy = wrap(x + 32, yy + 40, body, 530, 24) + 38

        def lane(y, n, title, description):
            text(88, y, n, 28, accent, True)
            text(146, y - 4, title, 38, ink, True)
            text(146, y + 47, description, 24, muted)

        text(88, 48, "Cine camera", 76, ink, True)
        text(92, 146, "Interaction map  /  Firmware design review", 31, muted)
        text(2450, 66, "18 screen / interaction states", 26, accent, True)
        text(2450, 108, "280 × 240 pixels · enlarged 2×", 24, muted)
        x = 92
        for value in [
            "A  settings / back",
            "B  take photo",
            "C  click wheel: switch SS / WB",
            "Turn wheel: adjust value",
        ]:
            x += pill(x, 210, value) + 18
        lane(
            284,
            "01",
            "Enter Cine",
            "Home B is the quick launch. Stock Camera remains in the main application selector.",
        )
        y = 378
        card(
            0,
            y,
            "home",
            "Home",
            1,
            "B opens Cine. Turning the wheel opens the application selector.",
            "B → Cine",
        )
        card(
            1,
            y,
            "opening",
            "Opening splash",
            2,
            "Immediate feedback while the camera starts.",
            "Transient",
        )
        card(
            2,
            y,
            "preparing",
            "Preparing camera",
            3,
            "Shown while resources are finishing; normally this state may be skipped.",
            "Transient",
        )
        card(
            3,
            y,
            "live",
            "Live viewfinder",
            4,
            "Clean view at rest. The current wheel mode is remembered when its readout hides.",
            "Default: Sunny · shutter",
        )
        context(
            4,
            y,
            "Controls at a glance",
            [
                ("A / gear", "Open the conventional settings list."),
                (
                    "Wheel / C",
                    "Turn to adjust; click to switch mode with a Select haptic.",
                ),
                (
                    "B / camera",
                    "Capture a native 3MP JPEG. A+B returns to the previous app.",
                ),
            ],
        )
        for col, label in [(0, "B"), (1, "wait"), (2, "ready")]:
            arrow(
                [(88 + col * 676 + 602, y + 298), (88 + (col + 1) * 676 - 4, y + 298)],
                label,
            )
        arrow(
            [(1064, 1080), (1064, 1120), (2416, 1120), (2416, 1080)],
            "Can proceed straight to live",
            where=(1740, 1106),
            color=teal,
        )
        lane(
            1170,
            "02",
            "Direct manipulation",
            "Both values appear together. The active mode is blue; labels and values snap off after 2 seconds of inactivity.",
        )
        y = 1264
        card(
            0,
            y,
            "shutter",
            "Adjust shutter",
            5,
            "Turn changes shutter speed. Every movement restarts the 2-second hold.",
            "SS · 1/1000–1/10 s",
        )
        card(
            1,
            y,
            "wb-sunny",
            "WB · Sunny",
            6,
            "Click C switches from shutter to WB and plays the standard Select haptic.",
            "Default white balance",
        )
        card(
            2,
            y,
            "wb-cloudy",
            "WB · Cloudy",
            7,
            "Turning the wheel cycles through the four static presets.",
            "Turn to cycle",
        )
        card(
            3,
            y,
            "wb-office",
            "WB · Office",
            8,
            "No Auto preset. Previous shutter value remains visible.",
            "Turn to cycle",
        )
        card(
            4,
            y,
            "wb-home",
            "WB · Home",
            9,
            "Presets wrap in both directions. Click C returns to shutter adjustment.",
            "Turn to cycle",
        )
        for col, label in [(0, "C +\nhaptic"), (1, "turn"), (2, "turn"), (3, "turn")]:
            arrow(
                [(88 + col * 676 + 602, y + 298), (88 + (col + 1) * 676 - 4, y + 298)],
                label,
            )
        arrow([(760, y + 340), (690, y + 340)])
        arrow(
            [(3092, y + 702), (3092, y + 740), (1064, y + 740), (1064, y + 703)],
            "WB wraps both ways",
            where=(2070, y + 726),
            color=teal,
        )
        text(
            102,
            2042,
            "Any adjustment state → LIVE after 2s. Instant appearance / disappearance; 50% black pills stay fixed; selected mode and values persist.",
            24,
            teal,
            True,
        )
        lane(
            2096,
            "03",
            "Settings",
            "A opens settings with the current wheel mode selected. Turn to browse; C toggles editing; A finishes editing before returning to the viewfinder.",
        )
        y = 2190
        card(
            0,
            y,
            "settings-ss",
            "Shutter selected",
            10,
            "Turn navigates the list. C begins editing the selected setting.",
            "Browse",
        )
        card(
            1,
            y,
            "settings-ss-edit",
            "Edit shutter",
            11,
            "Blue row text denotes editing. Turn changes shutter. C or A finishes editing.",
            "Edit",
        )
        card(
            2,
            y,
            "settings-wb",
            "WB selected",
            12,
            "After leaving edit mode, turn to the WB row. C begins editing.",
            "Browse",
        )
        card(
            3,
            y,
            "settings-wb-edit",
            "Edit white balance",
            13,
            "Turn cycles Sunny / Cloudy / Office / Home. C or A finishes editing.",
            "Edit",
        )
        card(
            4,
            y,
            "settings-back",
            "Back to camera",
            14,
            "C on Back to camera returns to the viewfinder. A from list browsing does too.",
            "→ LIVE",
        )
        for col, label in [
            (0, "C"),
            (1, "C, then\nturn"),
            (2, "C"),
            (3, "C, then\nturn"),
        ]:
            arrow(
                [(88 + col * 676 + 602, y + 298), (88 + (col + 1) * 676 - 4, y + 298)],
                label,
            )
        text(
            102,
            2922,
            "A from LIVE → settings.   A while editing → stop editing.   A while browsing / C on Back to camera → LIVE.   Camera stays initialized.",
            24,
            teal,
            True,
        )
        lane(
            2990,
            "04",
            "Capture, feedback and recovery",
            "B captures without leaving the viewfinder. Save failure is recoverable in place; a camera-opening / control error returns to the previous app.",
        )
        y = 3084
        card(
            0,
            y,
            "capture",
            "Capturing",
            15,
            "B briefly pulses the image and raises the photographer.",
            "B → capture",
        )
        card(
            1,
            y,
            "saved",
            "Saved",
            16,
            "Photo saved. Badge clears after 1.8 seconds; the viewfinder continues.",
            "Success → LIVE",
        )
        card(
            2,
            y,
            "save-failed",
            "Save failed",
            17,
            "Failure badge clears after 1.8 seconds. Press B to try again.",
            "Failure → retry",
        )
        context(
            3,
            y,
            "One capture flow",
            [
                ("Success", "Capture → Saved → Live."),
                ("Save failure", "Capture → Save failed → Live. B can retry."),
                ("Exit", "A+B returns to the previous app from any Cine screen."),
            ],
        )
        card(
            4,
            y,
            "error",
            "Camera unavailable",
            18,
            "Representative resource error. A goes back; reopen Cine to retry.",
            "A → previous app",
        )
        arrow([(690, y + 298), (760, y + 298)], "OK")
        arrow(
            [(388, y + 702), (388, y + 742), (1740, y + 742), (1740, y + 703)],
            "write failed",
            where=(1064, y + 728),
            color="#98533a",
        )
        text(88, 3910, "Review notes", 28, ink, True)
        wrap(
            88,
            3955,
            "Screens use the supplied IMG_6997.jpg as viewfinder content. Preview frames use the production raster and Instrument font; menus use the current Scene builder, list styling and safe-area geometry. Opening/error examples are representative. Variable system status is omitted from source-rendered screens.",
            3304,
            24,
        )
        text(
            88,
            4145,
            "Live image pipeline: C+0 S+1 · B0   /   Sunny default   /   Native 3MP JPEG   /   Static design sheet; timings noted above",
            24,
            muted,
        )
        sheet.save(output)
        frames_dir = output.parent / (output.stem + "-screens")
        frames_dir.mkdir(exist_ok=True)
        for key, im in frames.items():
            im.save(frames_dir / (key + ".png"))
        manifest = {
            "source_photo": str(Path(image).resolve()),
            "output": str(output.resolve()),
            "size": [W, H],
            "native_screen": [280, 240],
            "scale": 2,
            "states": list(cards),
            "ui_source": "production Cine raster / Scene builder / Instrument font",
            "home": "device capture" if home else "schematic context",
            "private_photo_committed": False,
        }
        output.with_suffix(".json").write_text(json.dumps(manifest, indent=2) + "\n")
        print(output)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--image", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--home", type=Path)
    a = p.parse_args()
    render(a.image, a.output, a.home)
