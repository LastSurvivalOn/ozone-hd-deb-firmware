#!/usr/bin/env python3
"""Unpacks and repacks the Amlogic logo partition (AML_RES! v2), which u-boot shows at power-on.

  logoimg.py unpack <logo.img> <dir>              -> one .bmp per resource
  logoimg.py pack <logo.img> <image.png> <out.img> -> same partition, boot picture replaced

u-boot draws the "bootup" resource (`imgread pic logo bootup`); "bootup.bmp" is a spare copy and
gets the same picture. The PNG must be 1280x720, 8-bit RGB or RGBA. It is converted to RGB565 with
ordered dithering and written into the original BMP header, so every offset in the container stays put.
"""
import struct
import sys
import zlib
from pathlib import Path

HDR = '<II8sIII'           # crc, version, magic, image size, item count, alignment
ITEM = '<7I'               # magic, hcrc, size, start, end, next header, index; name at +0x20
BOOT_PICTURES = ('bootup', 'bootup.bmp')
BAYER = (0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5)


def items(data):
    _crc, ver, magic, _size, _num, _align = struct.unpack_from(HDR, data)
    assert magic == b'AML_RES!' and ver == 2, 'not an AML_RES v2 logo image'
    off = 0x40
    while off:
        _m, _hcrc, size, start, _end, nxt, _idx = struct.unpack_from(ITEM, data, off)
        name = data[off + 0x20:off + 0x40].split(b'\0')[0].decode()
        yield name, start, size
        off = nxt


def crc(data, size):
    # the stored value is the CRC-32 of everything after the field, without the final inversion
    return zlib.crc32(data[4:size]) ^ 0xffffffff


def read_png(path):
    data = Path(path).read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', 'not a PNG'
    off, idat = 8, b''
    while off < len(data):
        n, tag = struct.unpack_from('>I4s', data, off)
        body = data[off + 8:off + 8 + n]
        if tag == b'IHDR':
            w, h, depth, ctype, _c, _f, interlace = struct.unpack('>IIBBBBB', body)
            assert depth == 8 and ctype in (2, 6) and not interlace, 'need 8-bit RGB/RGBA, not interlaced'
        elif tag == b'IDAT':
            idat += body
        off += 12 + n
    bpp = 3 if ctype == 2 else 4
    raw, stride = zlib.decompress(idat), w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line) if bpp == 3 else bytes(v for i, v in enumerate(line) if i % 4 != 3))
        prev = line
    return w, h, rows


def rgb565(rows, w):
    """Bottom-up RGB565 pixel data, as a BMP stores it."""
    out = bytearray()
    for y in range(len(rows) - 1, -1, -1):
        row = rows[y]
        for x in range(w):
            t = BAYER[(y & 3) * 4 + (x & 3)]
            r, g, b = row[x * 3:x * 3 + 3]
            r = min(31, (r * 31 + t * 16) // 255)
            g = min(63, (g * 63 + t * 8) // 255)
            b = min(31, (b * 31 + t * 16) // 255)
            out += struct.pack('<H', r << 11 | g << 5 | b)
    return bytes(out)


def unpack(img, out):
    data = Path(img).read_bytes()
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    for name, start, size in items(data):
        (out / (name if name.endswith('.bmp') else name + '.bmp')).write_bytes(data[start:start + size])
        print(f'{name:20} {size:8} bytes')


def pack(img, png, out):
    data = bytearray(Path(img).read_bytes())
    size = struct.unpack_from(HDR, data)[3]
    assert struct.unpack_from('<I', data)[0] == crc(data, size), 'logo image CRC mismatch'
    w, h, rows = read_png(png)
    pixels = rgb565(rows, w)
    done = 0
    for name, start, _size in items(data):
        if name not in BOOT_PICTURES:
            continue
        bw, bh = struct.unpack_from('<ii', data, start + 18)
        bpp, comp = struct.unpack_from('<HI', data, start + 28)
        assert (bw, bh, bpp, comp) == (w, h, 16, 3), f'{name}: stock is {bw}x{bh} {bpp} bpp, picture is {w}x{h}'
        pix = start + struct.unpack_from('<I', data, start + 10)[0]
        data[pix:pix + len(pixels)] = pixels
        done += 1
    assert done, 'no bootup resource found'
    struct.pack_into('<I', data, 0, crc(data, size))
    Path(out).write_bytes(bytes(data[:size]))
    print(f'{out}: {size} bytes, {done} pictures replaced')


if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[1] == 'unpack':
        unpack(*sys.argv[2:])
    elif len(sys.argv) == 5 and sys.argv[1] == 'pack':
        pack(*sys.argv[2:])
    else:
        sys.exit(__doc__)
