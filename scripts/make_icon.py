#!/usr/bin/env python3
"""Generate SURL's branding assets.

Everything SURL ships as an image is produced here so the branding is
reproducible from source and nobody has to keep a binary editor around:

  installer/windows/assets/surl.ico          multi-resolution application icon
  installer/windows/assets/wizard-large.bmp  Inno Setup welcome/finish page art
  installer/windows/assets/wizard-small.bmp  Inno Setup header art

The mark is a download arrow dropping onto a baseline: "save this URL". It has
to stay legible at 16x16, so the glyph is deliberately chunky and the palette
is high contrast.

Usage:  python scripts/make_icon.py
"""

from __future__ import annotations

import math
import os
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "installer", "windows", "assets")

ICON_SIZES = (16, 24, 32, 48, 64, 128, 256)

# Brand palette.
BG_TOP = (79, 70, 229)      # indigo 600
BG_BOTTOM = (14, 165, 233)  # sky 500
GLYPH = (255, 255, 255)

SUPERSAMPLE = 4


def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def rounded_rect_alpha(x: float, y: float, w: float, h: float, radius: float) -> float:
    """Coverage of a rounded rectangle at a point, 1.0 inside, 0.0 outside."""
    # Distance from the rounded-rectangle boundary (signed, negative inside).
    dx = max(radius - x, x - (w - radius), 0.0)
    dy = max(radius - y, y - (h - radius), 0.0)
    if dx == 0.0 and dy == 0.0:
        return 1.0 if (0 <= x <= w and 0 <= y <= h) else 0.0
    return 1.0 if math.hypot(dx, dy) <= radius else 0.0


def in_arrow(x: float, y: float, size: float) -> bool:
    """The download glyph, expressed in a 0..1 unit square."""
    u = x / size
    v = y / size

    shaft_half = 0.085
    shaft_top = 0.20
    shaft_bottom = 0.50

    # Vertical shaft.
    if abs(u - 0.5) <= shaft_half and shaft_top <= v <= shaft_bottom:
        return True

    # Arrow head: a triangle pointing down.
    head_top = 0.44
    head_bottom = 0.68
    head_half = 0.235
    if head_top <= v <= head_bottom:
        t = (v - head_top) / (head_bottom - head_top)
        half = lerp(head_half, 0.0, t)
        if abs(u - 0.5) <= half:
            return True

    # Baseline the arrow lands on, with a small gap.
    if 0.755 <= v <= 0.855 and 0.235 <= u <= 0.765:
        return True

    return False


def render_rgba(size: int) -> bytes:
    """Render the icon at `size` pixels with supersampled antialiasing."""
    hi = size * SUPERSAMPLE
    radius = hi * 0.22

    out = bytearray(size * size * 4)

    for py in range(size):
        for px in range(size):
            acc_a = 0.0
            acc_glyph = 0.0
            for sy in range(SUPERSAMPLE):
                for sx in range(SUPERSAMPLE):
                    fx = px * SUPERSAMPLE + sx + 0.5
                    fy = py * SUPERSAMPLE + sy + 0.5
                    cover = rounded_rect_alpha(fx, fy, hi, hi, radius)
                    if cover <= 0.0:
                        continue
                    acc_a += cover
                    if in_arrow(fx, fy, hi):
                        acc_glyph += cover

            samples = float(SUPERSAMPLE * SUPERSAMPLE)
            alpha = acc_a / samples
            if alpha <= 0.0:
                continue

            t = py / max(1, size - 1)
            bg = tuple(int(round(lerp(BG_TOP[i], BG_BOTTOM[i], t))) for i in range(3))

            glyph_ratio = (acc_glyph / acc_a) if acc_a > 0 else 0.0
            colour = tuple(
                int(round(lerp(bg[i], GLYPH[i], glyph_ratio))) for i in range(3)
            )

            offset = (py * size + px) * 4
            out[offset + 0] = colour[0]
            out[offset + 1] = colour[1]
            out[offset + 2] = colour[2]
            out[offset + 3] = int(round(alpha * 255))

    return bytes(out)


# --- PNG -------------------------------------------------------------------


def png_chunk(tag: bytes, data: bytes) -> bytes:
    body = tag + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def encode_png(size: int, rgba: bytes) -> bytes:
    raw = bytearray()
    stride = size * 4
    for y in range(size):
        raw.append(0)  # filter type: none
        raw += rgba[y * stride:(y + 1) * stride]
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
        + png_chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + png_chunk(b"IEND", b"")
    )


# --- ICO -------------------------------------------------------------------


def encode_dib(size: int, rgba: bytes) -> bytes:
    """A 32-bit BGRA DIB with the AND mask an .ico entry still requires."""
    header = struct.pack(
        "<IiiHHIIiiII",
        40,          # biSize
        size,        # biWidth
        size * 2,    # biHeight: image plus mask
        1,           # biPlanes
        32,          # biBitCount
        0,           # biCompression (BI_RGB)
        0,           # biSizeImage
        0, 0, 0, 0,  # resolution and palette counts
    )

    colour = bytearray()
    for y in range(size - 1, -1, -1):  # DIB rows run bottom-up
        row = rgba[y * size * 4:(y + 1) * size * 4]
        for x in range(size):
            r, g, b, a = row[x * 4:x * 4 + 4]
            colour += bytes((b, g, r, a))

    # 1bpp AND mask, rows padded to 4 bytes. Fully transparent pixels are
    # masked out for the benefit of anything that ignores the alpha channel.
    mask_stride = ((size + 31) // 32) * 4
    mask = bytearray()
    for y in range(size - 1, -1, -1):
        bits = bytearray(mask_stride)
        for x in range(size):
            alpha = rgba[(y * size + x) * 4 + 3]
            if alpha < 128:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask += bits

    return header + bytes(colour) + bytes(mask)


def write_ico(path: str, images: "list[tuple[int, bytes]]") -> None:
    entries = []
    payloads = []
    offset = 6 + 16 * len(images)

    for size, rgba in images:
        # Windows reads 256px entries as PNG; smaller ones stay classic DIBs so
        # every consumer, old shell code included, renders them.
        payload = encode_png(size, rgba) if size >= 256 else encode_dib(size, rgba)
        entries.append(
            struct.pack(
                "<BBBBHHII",
                size if size < 256 else 0,
                size if size < 256 else 0,
                0, 0, 1, 32,
                len(payload),
                offset,
            )
        )
        payloads.append(payload)
        offset += len(payload)

    with open(path, "wb") as handle:
        handle.write(struct.pack("<HHH", 0, 1, len(images)))
        for entry in entries:
            handle.write(entry)
        for payload in payloads:
            handle.write(payload)


# --- BMP (Inno Setup wizard art) -------------------------------------------


def write_bmp24(path: str, width: int, height: int, pixels: "list[list[tuple[int,int,int]]]") -> None:
    stride = ((width * 3 + 3) // 4) * 4
    body = bytearray()
    for y in range(height - 1, -1, -1):
        row = bytearray()
        for x in range(width):
            r, g, b = pixels[y][x]
            row += bytes((b, g, r))
        row += bytes(stride - len(row))
        body += row

    file_size = 14 + 40 + len(body)
    header = struct.pack("<2sIHHI", b"BM", file_size, 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(body), 2835, 2835, 0, 0)
    with open(path, "wb") as handle:
        handle.write(header + info + bytes(body))


def wizard_pixels(width: int, height: int, glyph_size: int, glyph_cx: int, glyph_cy: int):
    icon = render_rgba(glyph_size)
    rows = []
    for y in range(height):
        row = []
        for x in range(width):
            # Diagonal brand gradient behind everything.
            t = (x / max(1, width - 1) * 0.35) + (y / max(1, height - 1) * 0.65)
            base = tuple(int(round(lerp(BG_TOP[i], BG_BOTTOM[i], t))) for i in range(3))

            gx = x - (glyph_cx - glyph_size // 2)
            gy = y - (glyph_cy - glyph_size // 2)
            if 0 <= gx < glyph_size and 0 <= gy < glyph_size:
                offset = (gy * glyph_size + gx) * 4
                r, g, b, a = icon[offset:offset + 4]
                if a > 0:
                    alpha = a / 255.0
                    base = tuple(
                        int(round(lerp(base[i], (r, g, b)[i], alpha))) for i in range(3)
                    )
            row.append(base)
        rows.append(row)
    return rows


def main() -> None:
    os.makedirs(ASSETS, exist_ok=True)

    images = [(size, render_rgba(size)) for size in ICON_SIZES]

    ico_path = os.path.join(ASSETS, "surl.ico")
    write_ico(ico_path, images)
    print(f"wrote {ico_path} ({os.path.getsize(ico_path)} bytes, sizes {list(ICON_SIZES)})")

    png_path = os.path.join(ASSETS, "surl-256.png")
    with open(png_path, "wb") as handle:
        handle.write(encode_png(256, dict(images)[256]))
    print(f"wrote {png_path}")

    large = os.path.join(ASSETS, "wizard-large.bmp")
    write_bmp24(large, 164, 314, wizard_pixels(164, 314, 96, 82, 110))
    print(f"wrote {large}")

    small = os.path.join(ASSETS, "wizard-small.bmp")
    write_bmp24(small, 55, 55, wizard_pixels(55, 55, 44, 27, 27))
    print(f"wrote {small}")


if __name__ == "__main__":
    main()
