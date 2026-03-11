#!/usr/bin/env python3
"""Generate a sample 16x16 cursor asset for MiniDOS."""

import struct
import sys
import zlib

WIDTH = 16
HEIGHT = 16
MAGENTA = (255, 0, 255)
BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
PATTERN = [
    [1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    [1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    [1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    [1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    [1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    [1, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    [1, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    [1, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0],
    [1, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0],
    [1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0],
    [1, 2, 2, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    [1, 2, 2, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    [1, 2, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0],
    [1, 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0],
    [1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0],
    [0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0],
]


def color_for(value):
    if value == 1:
        return BLACK
    if value == 2:
        return WHITE
    return MAGENTA


def rgba_for(value):
    if value == 1:
        return (0, 0, 0, 255)
    if value == 2:
        return (255, 255, 255, 255)
    return (0, 0, 0, 0)


def write_bmp(path):
    row_size = (WIDTH * 3 + 3) & ~3
    pixel_data = bytearray()

    for y in range(HEIGHT - 1, -1, -1):
        row = bytearray()
        for x in range(WIDTH):
            r, g, b = color_for(PATTERN[y][x])
            row.extend((b, g, r))
        while len(row) < row_size:
            row.append(0)
        pixel_data.extend(row)

    file_size = 14 + 40 + len(pixel_data)
    bmp = bytearray()
    bmp.extend(b"BM")
    bmp.extend(struct.pack("<I", file_size))
    bmp.extend(struct.pack("<HH", 0, 0))
    bmp.extend(struct.pack("<I", 54))
    bmp.extend(struct.pack("<I", 40))
    bmp.extend(struct.pack("<i", WIDTH))
    bmp.extend(struct.pack("<i", HEIGHT))
    bmp.extend(struct.pack("<H", 1))
    bmp.extend(struct.pack("<H", 24))
    bmp.extend(struct.pack("<I", 0))
    bmp.extend(struct.pack("<I", len(pixel_data)))
    bmp.extend(struct.pack("<i", 2835))
    bmp.extend(struct.pack("<i", 2835))
    bmp.extend(struct.pack("<I", 0))
    bmp.extend(struct.pack("<I", 0))
    bmp.extend(pixel_data)

    with open(path, "wb") as f:
        f.write(bmp)


def _png_chunk(tag, data):
    chunk = tag + data
    return struct.pack(">I", len(data)) + chunk + struct.pack(">I", zlib.crc32(chunk) & 0xFFFFFFFF)


def write_png(path):
    raw = bytearray()

    for y in range(HEIGHT):
        raw.append(0)
        for x in range(WIDTH):
            raw.extend(rgba_for(PATTERN[y][x]))

    ihdr = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0)
    compressed = zlib.compress(bytes(raw), level=9)

    png = bytearray(b"\x89PNG\r\n\x1a\n")
    png.extend(_png_chunk(b"IHDR", ihdr))
    png.extend(_png_chunk(b"IDAT", compressed))
    png.extend(_png_chunk(b"IEND", b""))

    with open(path, "wb") as f:
        f.write(png)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "cursor.png"
    if out.lower().endswith(".bmp"):
        write_bmp(out)
    else:
        write_png(out)
    print(f"✓ Demo cursor written to {out}")
