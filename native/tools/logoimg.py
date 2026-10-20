#!/usr/bin/env python3
"""Unpacks and repacks the Amlogic logo partition (AML_RES! v2), which u-boot shows at power-on.

  logoimg.py unpack <logo.img> <dir>                  -> one .bmp per resource
  logoimg.py pack <logo.img> <image.png> <out.img>    -> same partition, boot picture replaced
  logoimg.py fb <image.png> <out.raw> [width height]  -> raw RGB565 for /dev/fb0 (default 1280x720)

u-boot draws the "bootup" resource (`imgread pic logo bootup`) 1:1 on the 1080p OSD, so the picture
should be 1920x1080; "bootup.bmp" is a spare copy and gets the same picture. The PNG must be 8-bit
RGB or RGBA. Pixels are converted to RGB565 with ordered dithering, since both u-boot and the
desktop framebuffer are 16 bit.
"""
import struct
import sys
import zlib
from pathlib import Path

HDR = '<II8sIII'           # crc, version, magic, image size, item count, alignment
ITEM = '<7I'               # magic, hcrc, size, start, end, next header, index; name at +0x20
ITEM_SIZE = 0x40
BOOT_PICTURES = ('bootup', 'bootup.bmp')
BAYER = (0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5)


def items(data):
    _crc, ver, magic, _size, _num, _align = struct.unpack_from(HDR, data)
    assert magic == b'AML_RES!' and ver == 2, 'not an AML_RES v2 logo image'
    off = 0x40
    while off:
        _m, _hcrc, size, start, _end, nxt, _idx = struct.unpack_from(ITEM, data, off)
        name = data[off + 0x20:off + 0x40].split(b'\0')[0].decode()
        yield off, name, data[start:start + size]
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


def resize(rows, w, h, nw, nh):
    """Area-average downscale (or nearest-neighbour upscale) of RGB rows."""
    if (nw, nh) == (w, h):
        return rows
    out = []
    for ny in range(nh):
        y0, y1 = ny * h // nh, max(ny * h // nh + 1, (ny + 1) * h // nh)
        line = bytearray()
        for nx in range(nw):
            x0, x1 = nx * w // nw, max(nx * w // nw + 1, (nx + 1) * w // nw)
            acc, n = [0, 0, 0], (x1 - x0) * (y1 - y0)
            for y in range(y0, y1):
                row = rows[y]
                for x in range(x0, x1):
                    acc[0] += row[x * 3]
                    acc[1] += row[x * 3 + 1]
                    acc[2] += row[x * 3 + 2]
            line += bytes(v // n for v in acc)
        out.append(bytes(line))
    return out


def rgb565(rows, w, bottom_up):
    out = bytearray()
    for y in (range(len(rows) - 1, -1, -1) if bottom_up else range(len(rows))):
        row = rows[y]
        for x in range(w):
            t = BAYER[(y & 3) * 4 + (x & 3)]
            r, g, b = row[x * 3:x * 3 + 3]
            r = min(31, (r * 31 + t * 16) // 255)
            g = min(63, (g * 63 + t * 8) // 255)
            b = min(31, (b * 31 + t * 16) // 255)
            out += struct.pack('<H', r << 11 | g << 5 | b)
    return bytes(out)


def bmp(stock, w, h, pixels):
    """A BMP in the same layout as the stock one (V3 header with RGB565 bit masks)."""
    hdr = bytearray(stock[:struct.unpack_from('<I', stock, 10)[0]])
    assert struct.unpack_from('<HI', stock, 28) == (16, 3), 'stock picture is not RGB565'
    assert w * 2 % 4 == 0, 'width must be even'
    body = pixels + b'\0\0'   # the stock files carry two padding bytes, keep that
    struct.pack_into('<I', hdr, 2, len(hdr) + len(body))
    struct.pack_into('<ii', hdr, 18, w, h)
    struct.pack_into('<I', hdr, 34, len(body))
    return bytes(hdr) + body


def unpack(img, out):
    data = Path(img).read_bytes()
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    for _off, name, body in items(data):
        (out / (name if name.endswith('.bmp') else name + '.bmp')).write_bytes(body)
        w, h = struct.unpack_from('<ii', body, 18)
        print(f'{name:20} {w}x{h} {len(body):8} bytes')


def pack(img, png, out):
    data = Path(img).read_bytes()
    _crc, ver, magic, size, num, align = struct.unpack_from(HDR, data)
    assert struct.unpack_from('<I', data)[0] == crc(data, size), 'logo image CRC mismatch'
    w, h, rows = read_png(png)
    pixels = rgb565(rows, w, bottom_up=True)

    heads, bodies = [], []
    for off, name, body in items(data):
        heads.append(bytearray(data[off:off + ITEM_SIZE]))
        bodies.append(bmp(body, w, h, pixels) if name in BOOT_PICTURES else body)
    assert len(heads) == num, 'item count does not match the header'
    assert any(bytes(h[0x20:0x40]).split(b'\0')[0].decode() in BOOT_PICTURES for h in heads), 'no bootup resource'

    # header, item headers, then the bodies, each aligned like the stock layout
    image = bytearray(data[:0x40]) + bytearray(ITEM_SIZE * num)
    for i, (head, body) in enumerate(zip(heads, bodies)):
        image += bytes(-len(image) % align)
        struct.pack_into('<II', head, 8, len(body), len(image))
        struct.pack_into('<I', head, 20, 0x40 + ITEM_SIZE * (i + 1) if i + 1 < num else 0)
        image[0x40 + ITEM_SIZE * i:0x40 + ITEM_SIZE * (i + 1)] = head
        image += body
    image += bytes(-len(image) % align)
    struct.pack_into('<I', image, 16, len(image))
    struct.pack_into('<I', image, 0, crc(image, len(image)))
    Path(out).write_bytes(bytes(image))
    print(f'{out}: {len(image)} bytes, boot picture {w}x{h}')


def fb(png, out, nw=1280, nh=720):
    w, h, rows = read_png(png)
    Path(out).write_bytes(rgb565(resize(rows, w, h, int(nw), int(nh)), int(nw), bottom_up=False))
    print(f'{out}: {nw}x{nh} RGB565')


if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[1] == 'unpack':
        unpack(*sys.argv[2:])
    elif len(sys.argv) == 5 and sys.argv[1] == 'pack':
        pack(*sys.argv[2:])
    elif len(sys.argv) in (4, 6) and sys.argv[1] == 'fb':
        fb(*sys.argv[2:])
    else:
        sys.exit(__doc__)
