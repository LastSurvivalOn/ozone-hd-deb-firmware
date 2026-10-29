#!/usr/bin/env python3
"""Generates a Catppuccin Mocha wallpaper: a dark gradient with blurred color blobs.

  wallpaper.py <out.png> [width height] [palette]

The palettes: mocha (the desktop's own, dotfiles' ozone.png), ocean, sunset, forest, night, rose;
ozone-wallpaper offers the others as the box's built-in pictures (rootfs/usr/local/share/ozone/wallpapers).
"""
import math
import struct
import sys
import zlib

BASE = (0x1e, 0x1e, 0x2e)
CRUST = (0x11, 0x11, 0x1b)
# (x, y, radius) as fractions of width/height, color, strength
PALETTES = {
    'mocha': [((0.78, 0.22, 0.42), (0xcb, 0xa6, 0xf7), 0.55),   # mauve
              ((0.18, 0.85, 0.45), (0x89, 0xb4, 0xfa), 0.45),   # blue
              ((0.55, 0.95, 0.30), (0xf5, 0xc2, 0xe7), 0.30),   # pink
              ((0.05, 0.10, 0.25), (0x94, 0xe2, 0xd5), 0.18)],  # teal
    'ocean': [((0.25, 0.30, 0.50), (0x74, 0xc7, 0xec), 0.55),   # sapphire
              ((0.85, 0.75, 0.45), (0x94, 0xe2, 0xd5), 0.45),   # teal
              ((0.60, 0.10, 0.30), (0x89, 0xb4, 0xfa), 0.35)],  # blue
    'sunset': [((0.70, 0.80, 0.50), (0xfa, 0xb3, 0x87), 0.55),  # peach
               ((0.20, 0.20, 0.40), (0xf3, 0x8b, 0xa8), 0.45),  # red
               ((0.95, 0.20, 0.30), (0xf9, 0xe2, 0xaf), 0.35)], # yellow
    'forest': [((0.30, 0.75, 0.50), (0xa6, 0xe3, 0xa1), 0.45),  # green
               ((0.80, 0.25, 0.40), (0x94, 0xe2, 0xd5), 0.40),  # teal
               ((0.05, 0.15, 0.25), (0xf9, 0xe2, 0xaf), 0.20)], # yellow
    'night': [((0.85, 0.15, 0.35), (0xcb, 0xa6, 0xf7), 0.25),   # mauve, faint
              ((0.10, 0.90, 0.40), (0x89, 0xb4, 0xfa), 0.18)],  # blue, faint
    'rose': [((0.30, 0.30, 0.50), (0xf5, 0xc2, 0xe7), 0.50),    # pink
             ((0.80, 0.85, 0.45), (0xcb, 0xa6, 0xf7), 0.45),    # mauve
             ((0.90, 0.05, 0.25), (0xf2, 0xcd, 0xcd), 0.30)],   # flamingo
}
BLOBS = PALETTES['mocha']


def pixel(x, y, w, h):
    t = (x / w * 0.4 + y / h * 0.6)
    col = [CRUST[i] + (BASE[i] - CRUST[i]) * (1 - t) for i in range(3)]
    for (bx, by, br), c, k in BLOBS:
        d = math.hypot((x / w - bx) * w / h, y / h - by) / br
        a = k * math.exp(-d * d * 2.2)
        col = [col[i] + (c[i] - col[i]) * a for i in range(3)]
    # a little noise so the gradient doesn't band
    n = ((x * 73856093 ^ y * 19349663) % 7) - 3
    return bytes(max(0, min(255, int(v + n * 0.6))) for v in col)


def png(path, w, h):
    rows = b''.join(b'\0' + b''.join(pixel(x, y, w, h) for x in range(w)) for y in range(h))

    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(rows, 9)))
        f.write(chunk(b'IEND', b''))


if __name__ == '__main__':
    if len(sys.argv) not in (2, 3, 4, 5):
        sys.exit(__doc__)
    if len(sys.argv) in (3, 5):
        BLOBS = PALETTES[sys.argv[-1]]
    w, h = (int(sys.argv[2]), int(sys.argv[3])) if len(sys.argv) >= 4 else (1280, 720)
    png(sys.argv[1], w, h)
