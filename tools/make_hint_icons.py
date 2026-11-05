"""Cut the VR controller icons for instructor hints out of an icon sheet.

The sheet is the 1920x1080 image the project owner supplied on 2026-11-05
(not in the repository). Its large set holds three rows of
light icons on a dark background:

    Y  X  L  L-press  L-right  L-down  L-left  L-up
    B  A  R  R-press  R-right  R-down  R-left  R-up
    menu  oculus  LG  RG  LT  RT

Each icon gets straight alpha: the dark background around it becomes
transparent, its antialiased rim keeps partial coverage and the dark letters
inside stay opaque. The output is a C++ header of gray/alpha pairs.

    python -I tools/make_hint_icons.py SHEET L4D2VR/hint_icons.h [--preview PNG]
"""

import argparse
from collections import deque
from pathlib import Path

import numpy as np
from PIL import Image

# Cell centres of the large set, in sheet pixels.
COLUMNS = (572, 682, 792, 902, 1012, 1123, 1237, 1347)
ROWS = (230, 340, 449)
HALF_CELL = 46

ICONS = (
    ("Y", 0, 0), ("X", 1, 0), ("LeftStick", 2, 0), ("LeftStickPress", 3, 0),
    ("LeftStickRight", 4, 0), ("LeftStickDown", 5, 0), ("LeftStickLeft", 6, 0), ("LeftStickUp", 7, 0),
    ("B", 0, 1), ("A", 1, 1), ("RightStick", 2, 1), ("RightStickPress", 3, 1),
    ("RightStickRight", 4, 1), ("RightStickDown", 5, 1), ("RightStickLeft", 6, 1), ("RightStickUp", 7, 1),
    ("Menu", 0, 2), ("System", 1, 2), ("LeftGrip", 2, 2), ("RightGrip", 3, 2),
    ("LeftTrigger", 4, 2), ("RightTrigger", 5, 2),
)

INK = 60          # brighter than this is part of an icon
MIN_PART = 25     # smaller bright specks are the sheet's grid marks
BACKGROUND = 32   # coverage ramp starts here


def neighbours(y, x, h, w):
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            if (dy or dx) and 0 <= y + dy < h and 0 <= x + dx < w:
                yield y + dy, x + dx


def components(mask):
    h, w = mask.shape
    label = np.zeros(mask.shape, dtype=np.int32)
    parts = []
    for y in range(h):
        for x in range(w):
            if not mask[y, x] or label[y, x]:
                continue
            index = len(parts) + 1
            label[y, x] = index
            queue, cells = deque([(y, x)]), []
            while queue:
                cy, cx = queue.popleft()
                cells.append((cy, cx))
                for ny, nx in neighbours(cy, cx, h, w):
                    if mask[ny, nx] and not label[ny, nx]:
                        label[ny, nx] = index
                        queue.append((ny, nx))
            parts.append(cells)
    return parts


def exterior(solid):
    """Pixels reachable from the border without crossing `solid` (4-connected)."""
    h, w = solid.shape
    outside = np.zeros(solid.shape, dtype=bool)
    queue = deque()
    for y in range(h):
        for x in (0, w - 1):
            if not solid[y, x] and not outside[y, x]:
                outside[y, x] = True
                queue.append((y, x))
    for x in range(w):
        for y in (0, h - 1):
            if not solid[y, x] and not outside[y, x]:
                outside[y, x] = True
                queue.append((y, x))
    while queue:
        y, x = queue.popleft()
        for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
            if 0 <= ny < h and 0 <= nx < w and not solid[ny, nx] and not outside[ny, nx]:
                outside[ny, nx] = True
                queue.append((ny, nx))
    return outside


def dilate(mask, steps):
    out = mask.copy()
    for _ in range(steps):
        grown = out.copy()
        grown[1:, :] |= out[:-1, :]
        grown[:-1, :] |= out[1:, :]
        grown[:, 1:] |= out[:, :-1]
        grown[:, :-1] |= out[:, 1:]
        grown[1:, 1:] |= out[:-1, :-1]
        grown[:-1, :-1] |= out[1:, 1:]
        grown[1:, :-1] |= out[:-1, 1:]
        grown[:-1, 1:] |= out[1:, :-1]
        out = grown
    return out


def cut(luma, column, row):
    cx, cy = COLUMNS[column], ROWS[row]
    cell = luma[cy - HALF_CELL:cy + HALF_CELL, cx - HALF_CELL:cx + HALF_CELL]
    ink = np.zeros(cell.shape, dtype=bool)
    for part in components(cell > INK):
        if len(part) >= MIN_PART:
            for y, x in part:
                ink[y, x] = True
    outside = exterior(ink)
    solid = ~outside
    rim = (outside | dilate(outside, 1)) & dilate(solid, 2)
    foreground = float(np.percentile(cell[ink], 90))
    coverage = np.clip((cell - BACKGROUND) / (foreground - BACKGROUND), 0.0, 1.0)
    gray = np.where(rim, foreground, cell)
    alpha = np.where(rim, coverage, np.where(solid, 1.0, 0.0))
    ys, xs = np.nonzero(alpha > 0.0)
    top, bottom, left, right = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    return (np.clip(np.rint(gray[top:bottom, left:right]), 0, 255).astype(np.uint8),
            np.clip(np.rint(alpha[top:bottom, left:right] * 255), 0, 255).astype(np.uint8))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("sheet", type=Path)
    parser.add_argument("header", type=Path)
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()

    rgb = np.asarray(Image.open(args.sheet).convert("RGB")).astype(np.float64)
    luma = rgb[..., 0] * 0.299 + rgb[..., 1] * 0.587 + rgb[..., 2] * 0.114
    icons = [(name, *cut(luma, column, row)) for name, column, row in ICONS]
    canvas = max(max(gray.shape) for _, gray, _ in icons) + 2
    canvas += canvas % 2

    lines = [
        "#pragma once",
        "",
        "// Generated by tools/make_hint_icons.py. Do not edit.",
        "//",
        "// VR controller icons for instructor hints, cut from the icon sheet the",
        "// project owner supplied on 2026-11-05. The sheet is not in this repository.",
        "",
        "#include <cstdint>",
        "",
        "namespace HintIcons {",
        "",
        "enum class Id : std::uint8_t {",
        "    " + ", ".join(name for name, _, _ in icons) + ", Count",
        "};",
        "",
        "// Every icon sits centred in a kCanvas x kCanvas square, at the sheet's scale.",
        f"constexpr int kCanvas = {canvas};",
        "",
        "struct Icon {",
        "    const char *name;",
        "    std::uint8_t left, top, width, height; // inside the canvas",
        "    std::uint32_t offset;                  // into kPixels",
        "};",
        "",
        "inline constexpr Icon kIcons[] = {",
    ]
    data, offset = [], 0
    for name, gray, alpha in icons:
        height, width = gray.shape
        left, top = (canvas - width) // 2, (canvas - height) // 2
        lines.append(f'    {{"{name}", {left}, {top}, {width}, {height}, {offset}}},')
        pairs = np.stack([gray, alpha], axis=-1).reshape(-1)
        data.extend(int(v) for v in pairs)
        offset += width * height * 2
    lines += ["};", "", "// Gray and straight alpha per pixel, row by row.",
              "inline constexpr std::uint8_t kPixels[] = {"]
    for start in range(0, len(data), 32):
        lines.append("    " + ",".join(str(v) for v in data[start:start + 32]) + ",")
    lines += ["};", "", "} // namespace HintIcons", ""]
    args.header.write_text("\n".join(lines), encoding="ascii", newline="\n")
    print(f"{len(icons)} icons, canvas {canvas}, {len(data)} bytes -> {args.header}")
    for name, gray, _ in icons:
        print(f"  {name}: {gray.shape[1]}x{gray.shape[0]}")

    if args.preview:
        scale, columns = 3, 8
        tiles = []
        for _, gray, alpha in icons:
            tile = np.zeros((canvas, canvas, 4), dtype=np.uint8)
            h, w = gray.shape
            top, left = (canvas - h) // 2, (canvas - w) // 2
            tile[top:top + h, left:left + w, 0:3] = gray[..., None]
            tile[top:top + h, left:left + w, 3] = alpha
            tiles.append(Image.fromarray(tile, "RGBA").resize((canvas * scale,) * 2, Image.NEAREST))
        rows = (len(tiles) + columns - 1) // columns
        sheet = Image.new("RGBA", (columns * canvas * scale, rows * canvas * scale * 2))
        for band, color in enumerate(((40, 120, 60, 255), (150, 150, 150, 255))):
            for index, tile in enumerate(tiles):
                x = (index % columns) * canvas * scale
                y = (band * rows + index // columns) * canvas * scale
                back = Image.new("RGBA", tile.size, color)
                back.alpha_composite(tile)
                sheet.paste(back, (x, y))
        sheet.save(args.preview)


if __name__ == "__main__":
    main()
