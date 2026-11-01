"""Launcher drawing helpers built on plain Tk photo images, so the launcher needs only
the standard library. `python -m tools.launcher_art` regenerates tools/assets/ and is
the only part that needs Pillow.
"""

from functools import lru_cache
from math import cos, hypot, radians, sin
from pathlib import Path
import sys
import tkinter as tk

ASSETS = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent.parent)) / "tools/assets"
ICON = ASSETS / "launcher.ico"
LOGO_SIZES = (44, 55, 66, 88)
ICON_SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
APP_ID = "Portal2VR.Launcher"
PORTAL_BLUE, PORTAL_ORANGE = (74, 168, 238), (255, 154, 60)
SAMPLES = tuple((i + .5) / 4 for i in range(4))


def _rgb(color):
    return tuple(int(color[i:i + 2], 16) for i in (1, 3, 5))


def _hex(rgb):
    return "#%02x%02x%02x" % tuple(round(value) for value in rgb)


@lru_cache(maxsize=256)
def _corner(radius, stroke, fill, outline, background):
    """Top-left corner as rows of pixels, anti-aliased by 4x supersampling."""
    fill, outline, background = _rgb(fill), _rgb(outline), _rgb(background)
    outer, inner = radius * radius, max(0, radius - stroke) ** 2
    rows = []
    for y in range(radius):
        row = []
        for x in range(radius):
            inside = body = 0
            for sample_y in SAMPLES:
                for sample_x in SAMPLES:
                    distance = (radius - x - sample_x) ** 2 + (radius - y - sample_y) ** 2
                    inside += distance <= outer
                    body += distance <= inner
            row.append(_hex(((16 - inside) * b + (inside - body) * o + body * f) / 16
                            for b, o, f in zip(background, outline, fill)))
        rows.append(row)
    return rows


def _pixels(rows):
    return " ".join("{" + " ".join(row) + "}" for row in rows)


def rounded_box(master, size, radius, fill, outline, background, stroke=1):
    """Opaque rounded rectangle; its corners blend into the flat background behind it."""
    width, height = size
    radius = min(radius, width // 2, height // 2)
    image = tk.PhotoImage(master=master, width=width, height=height)
    image.put(outline, to=(0, 0, width, height))
    image.put(fill, to=(stroke, stroke, width - stroke, height - stroke))
    if radius:
        rows = _corner(radius, stroke, fill, outline, background)
        image.put(_pixels(rows), to=(0, 0))
        image.put(_pixels(row[::-1] for row in rows), to=(width - radius, 0))
        image.put(_pixels(rows[::-1]), to=(0, height - radius))
        image.put(_pixels(row[::-1] for row in rows[::-1]), to=(width - radius, height - radius))
    return image


def accent_strip(master, width, height, background):
    """Blue light from the left and orange from the right, fading out before they meet."""
    base, row = _rgb(background), []
    for x in range(width):
        position = x / max(1, width - 1)
        color, strength = (PORTAL_BLUE, 1 - position / .46) if position < .5 else (PORTAL_ORANGE, (position - .54) / .46)
        strength = max(0, strength)
        row.append(_hex(b + (c - b) * strength for b, c in zip(base, color)))
    image = tk.PhotoImage(master=master, width=width, height=height)
    image.put(_pixels([row]), to=(0, 0, width, height))
    return image


def _mark(size):
    """White iris in a dark disc with a split blue and orange portal rim."""
    from PIL import Image, ImageDraw

    scale = size * 8
    radius = scale / 2
    image = Image.new("RGBA", (scale, scale), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    # Taskbar sizes get a heavier rim and wider blade gaps so the mark stays crisp.
    small = size <= 32
    rim = round(scale * (.13 if small else .085))
    draw.ellipse((rim // 2, rim // 2, scale - rim // 2 - 1, scale - rim // 2 - 1), fill=(12, 17, 24, 255))
    draw.arc((0, 0, scale - 1, scale - 1), 98, 262, fill=PORTAL_BLUE + (255,), width=rim)
    draw.arc((0, 0, scale - 1, scale - 1), 278, 82, fill=PORTAL_ORANGE + (255,), width=rim)
    blades = radius * (.62 if small else .66)
    mask = Image.new("L", (scale, scale), 0)
    cut = ImageDraw.Draw(mask)
    cut.ellipse((radius - blades, radius - blades, radius + blades, radius + blades), fill=255)
    opening = .46 if size <= 20 else .36
    hole = [(radius + opening * blades * cos(radians(22.5 + 45 * i)),
             radius + opening * blades * sin(radians(22.5 + 45 * i))) for i in range(8)]
    cut.polygon(hole, fill=0)
    # The blade gaps turn to mush below 24 pixels, so the smallest icons are a plain ring.
    for i in range(8 if size > 20 else 0):
        (x0, y0), (x1, y1) = hole[i], hole[(i + 1) % 8]
        length = hypot(x1 - x0, y1 - y0)
        cut.line((x0, y0, x0 + (x1 - x0) / length * scale, y0 + (y1 - y0) / length * scale),
                 fill=0, width=max(1, round((.15 if small else .085) * blades)))
    image.paste((238, 243, 248, 255), (0, 0), mask)
    return image.resize((size, size), Image.LANCZOS)


def write_assets(folder=ASSETS):
    """Regenerate the header logos and the window/executable icon."""
    folder.mkdir(parents=True, exist_ok=True)
    for size in LOGO_SIZES:
        _mark(size).save(folder / f"launcher_logo_{size}.png", optimize=True)
    # Every size is drawn on its own: Windows picks an exact match for each DPI.
    images = [_mark(size) for size in ICON_SIZES]
    images[-1].save(folder / ICON.name, sizes=[(size, size) for size in ICON_SIZES], append_images=images[:-1])


if __name__ == "__main__":
    write_assets()
