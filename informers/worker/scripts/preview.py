#!/usr/bin/env python3
"""Converts informer BMPs (1-bit or 2-bit, which Pillow cannot open) to PNG.

    python3 scripts/preview.py in.bmp [more.bmp ...] out.png

Several inputs are placed side by side, so B/W and gray can be compared.
"""

import struct
import sys

from PIL import Image


def read_bmp(path):
    data = open(path, "rb").read()
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    colors = struct.unpack_from("<I", data, 46)[0] or (1 << bpp)
    palette = [data[54 + i * 4] for i in range(colors)]
    row_bytes = (width * bpp + 31) // 32 * 4
    img = Image.new("L", (width, abs(height)))
    px = img.load()
    per_byte = 8 // bpp
    mask = (1 << bpp) - 1
    for y in range(abs(height)):
        row = offset + (abs(height) - 1 - y if height > 0 else y) * row_bytes
        for x in range(width):
            byte = data[row + x // per_byte]
            index = (byte >> (8 - bpp * (x % per_byte + 1))) & mask
            px[x, y] = palette[index]
    return img


def main():
    *inputs, out = sys.argv[1:]
    images = [read_bmp(p) for p in inputs]
    gap = 10
    sheet = Image.new("L", (sum(i.width for i in images) + gap * (len(images) - 1), max(i.height for i in images)), 128)
    x = 0
    for img in images:
        sheet.paste(img, (x, 0))
        x += img.width + gap
    sheet.save(out)


if __name__ == "__main__":
    main()
