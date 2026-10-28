"""Launcher artwork drawn with Pillow when it is installed.

Source runs without Pillow fall back to the flat colours in launcher.py; the
packaged launcher bundles it. All geometry is in physical pixels.
`python -m tools.launcher_art` regenerates tools/assets/launcher.ico.
"""

from math import cos, hypot, radians, sin
import os
from pathlib import Path
import sys

try:
    from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageFont, ImageTk
except ImportError:
    Image = None

ASSETS = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent.parent)) / "tools/assets"
BACKGROUND = ASSETS / "launcher_background.jpg"
ICON = ASSETS / "launcher.ico"
WORDMARK_FONT = Path(os.environ.get("WINDIR", r"C:\Windows")) / "Fonts/bahnschrift.ttf"
PORTAL_BLUE = (56, 172, 255)
PANEL_TINT = (7, 15, 24)


def enabled() -> bool:
    return Image is not None and BACKGROUND.is_file()


def photo(image):
    return ImageTk.PhotoImage(image)


def _ramp(size, start, end, vertical=False):
    """Linear alpha mask from start to end, left to right or top to bottom."""
    ramp = Image.linear_gradient("L")
    if not vertical:
        ramp = ramp.transpose(Image.Transpose.ROTATE_90)
    return ramp.resize(size).point(lambda value: round(start + (end - start) * value / 255))


def _blur_alpha(layer, radius, strength):
    return layer.getchannel("A").filter(ImageFilter.GaussianBlur(radius)).point(
        lambda value: min(255, round(value * strength)))


def iris(diameter, color=(236, 243, 249)):
    """Eight-blade camera iris on a transparent background."""
    size = diameter * 4
    radius = size / 2
    mask = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(mask)
    draw.ellipse((0, 0, size - 1, size - 1), fill=255)
    hole = [(radius + .36 * radius * cos(radians(22.5 + 45 * i)),
             radius + .36 * radius * sin(radians(22.5 + 45 * i))) for i in range(8)]
    draw.polygon(hole, fill=0)
    for i in range(8):
        (x0, y0), (x1, y1) = hole[i], hole[(i + 1) % 8]
        length = hypot(x1 - x0, y1 - y0)
        draw.line((x0, y0, x0 + (x1 - x0) / length * size, y0 + (y1 - y0) / length * size),
                  fill=0, width=max(1, round(.075 * radius)))
    result = Image.new("RGBA", (diameter, diameter), color)
    result.putalpha(mask.resize((diameter, diameter), Image.LANCZOS))
    return result


def _tracked(draw, origin, text, font, tracking, fill):
    x, y = origin
    for char in text:
        draw.text((x, y), char, font=font, fill=fill, anchor="ls")
        x += font.getlength(char) + tracking
    return x - tracking


def _wordmark(image, origin, scale):
    """PORTAL 2 / iris | VR, with a portal ring after the 2 and a soft drop shadow."""
    s = scale
    bold = ImageFont.truetype(str(WORDMARK_FONT), round(60 * s))
    bold.set_variation_by_name("Bold")
    light = ImageFont.truetype(str(WORDMARK_FONT), round(44 * s))
    light.set_variation_by_name("Light")
    layer = Image.new("RGBA", (round(420 * s), round(156 * s)), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    baseline = round(72 * s)
    end = _tracked(draw, (0, baseline), "PORTAL 2", bold, 1.5 * s, (244, 248, 252, 255))
    # Taller than the capitals and filled with light, so it reads as a portal rather than a 0.
    ring = Image.new("RGBA", layer.size, (0, 0, 0, 0))
    ring_box = (end + 14 * s, baseline - 60 * s, end + 40 * s, baseline + 10 * s)
    ring_draw = ImageDraw.Draw(ring)
    ring_draw.ellipse(ring_box, fill=PORTAL_BLUE + (70,))
    ring_draw.ellipse(ring_box, outline=PORTAL_BLUE + (255,), width=max(2, round(3.5 * s)))
    inner = Image.new("RGBA", layer.size, (190, 232, 255, 0))
    inner_mask = Image.new("L", layer.size, 0)
    x0, y0, x1, y1 = ring_box
    ImageDraw.Draw(inner_mask).ellipse((x0 + 6 * s, y0 + 12 * s, x1 - 6 * s, y1 - 12 * s), fill=120)
    inner.putalpha(inner_mask.filter(ImageFilter.GaussianBlur(5 * s)))
    glow = Image.new("RGBA", layer.size, PORTAL_BLUE + (0,))
    glow.putalpha(_blur_alpha(ring, 7 * s, 1.5))
    layer = Image.alpha_composite(Image.alpha_composite(Image.alpha_composite(glow, layer), ring), inner)
    draw = ImageDraw.Draw(layer)
    mark = iris(round(40 * s))
    top = round(94 * s)
    layer.alpha_composite(mark, (round(2 * s), top))
    divider = round(56 * s)
    draw.line((divider, top - 2 * s, divider, top + 42 * s), fill=(214, 226, 238, 210), width=max(1, round(1.5 * s)))
    _tracked(draw, (divider + 13 * s, top + 37 * s), "VR", light, 3 * s, (214, 226, 238, 255))
    shadow = _blur_alpha(layer, 5 * s, .75)
    x, y = origin
    image.paste((0, 0, 0), (x, y + round(2 * s)), shadow)
    image.paste(layer, (x, y), layer)


class Backdrop:
    """Cover-scaled photo with legibility shading, frosted panel and footer band."""

    def __init__(self):
        self.photo = Image.open(BACKGROUND).convert("RGB")
        self.wordmark = WORDMARK_FONT.is_file()

    def render(self, size, panel, footer_top, separators, logo, scale):
        w, h = size
        ratio = max(w / self.photo.width, h / self.photo.height)
        crop_w, crop_h = min(self.photo.width, w / ratio), min(self.photo.height, h / ratio)
        left, top = (self.photo.width - crop_w) / 2, (self.photo.height - crop_h) * .45
        image = self.photo.resize(size, Image.LANCZOS, box=(left, top, left + crop_w, top + crop_h))
        image = ImageEnhance.Brightness(image).enhance(.84)
        shade = min(w, round(640 * scale))
        image.paste((2, 6, 11), (0, 0, shade, h), _ramp((shade, h), 205, 0))
        image.paste((2, 6, 11), (0, 0, w, round(190 * scale)), _ramp((w, round(190 * scale)), 120, 0, True))
        x0, y0, x1, y1 = panel
        frosted = image.crop(panel).filter(ImageFilter.GaussianBlur(9 * scale))
        image.paste(Image.blend(frosted, Image.new("RGB", frosted.size, PANEL_TINT), .74), (x0, y0))
        band = h - footer_top
        image.paste((3, 7, 12), (0, footer_top - round(28 * scale), w, footer_top),
                    _ramp((w, round(28 * scale)), 0, 150, True))
        image.paste((3, 7, 12), (0, footer_top, w, h), _ramp((w, band), 185, 215, True))
        draw = ImageDraw.Draw(image, "RGBA")
        draw.line((0, footer_top, w, footer_top), fill=(120, 170, 215, 46))
        draw.rectangle((x0, y0, x1 - 1, y1 - 1), outline=(104, 150, 194, 120))
        draw.line((x0 + 1, y0 + 1, x1 - 2, y0 + 1), fill=(255, 255, 255, 18))
        for sx0, sx1, sy in separators:
            image.paste((255, 255, 255), (sx0, sy, sx1, sy + 1), _ramp((sx1 - sx0, 1), 34, 0))
        if self.wordmark:
            _wordmark(image, logo, scale)
        return image


def highlight(crop, scale, selected):
    """Navigation highlight over a crop of the backdrop: a blue wash fading into the panel."""
    w, h = crop.size
    image = crop.copy()
    image.paste((28, 132, 222), (0, 0, w, h), _ramp((w, h), *((215, 70) if selected else (80, 0))))
    if selected:
        image.paste((110, 205, 255), (0, 0, w, 1), _ramp((w, 1), 150, 20))
        image.paste((110, 205, 255), (0, h - 1, w, h), _ramp((w, 1), 150, 20))
        ImageDraw.Draw(image).rectangle((0, 0, max(1, round(4 * scale)) - 1, h - 1), fill=(104, 210, 255))
    return image


def launch_button(size, scale, state):
    w, h = size
    tone = {"normal": 1, "hover": 1.16, "pressed": .84}[state]
    left, right = (tuple(min(255, round(c * tone)) for c in rgb) for rgb in ((17, 92, 172), (40, 150, 230)))
    image = Image.composite(Image.new("RGB", size, right), Image.new("RGB", size, left), _ramp(size, 0, 255))
    image.paste((255, 255, 255), (0, 0, w, h // 2), _ramp((w, h // 2), 34, 0, True))
    stripes = Image.new("L", size, 0)
    lines = ImageDraw.Draw(stripes)
    step = max(4, round(9 * scale))
    for x in range(round(w * .58), w + h, step):
        lines.line((x, 0, x - h, h), fill=255, width=max(1, round(scale)))
    image.paste((255, 255, 255), (0, 0), Image.composite(_ramp(size, 0, 22), Image.new("L", size, 0), stripes))
    draw = ImageDraw.Draw(image, "RGBA")
    draw.rectangle((0, 0, w - 1, h - 1), outline=(132, 216, 255, 255))
    draw.rectangle((1, 1, w - 2, h - 2), outline=(255, 255, 255, 36))
    inset = round(12 * scale)
    for x in (round(92 * scale), w - round(84 * scale)):
        draw.line((x, inset, x, h - inset), fill=(255, 255, 255, 84), width=1)
    return image


def write_icon(path=ICON):
    """Regenerate the window and executable icon: white iris in a dark disc with a blue rim."""
    size = 256
    icon = Image.new("RGBA", (size * 4, size * 4), (0, 0, 0, 0))
    draw = ImageDraw.Draw(icon)
    draw.ellipse((8, 8, size * 4 - 9, size * 4 - 9), fill=(10, 24, 38, 255), outline=PORTAL_BLUE + (255,), width=44)
    icon = icon.resize((size, size), Image.LANCZOS)
    mark = iris(round(size * .7))
    icon.alpha_composite(mark, ((size - mark.width) // 2, (size - mark.height) // 2))
    path.parent.mkdir(parents=True, exist_ok=True)
    icon.save(path, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])


if __name__ == "__main__":
    write_icon()
