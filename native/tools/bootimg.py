#!/usr/bin/env python3
"""Unpacks and packs an Android boot.img (format v0, as in the stock Amlogic firmware).

  bootimg.py unpack <boot.img> <dir>           -> kernel, ramdisk, second, header.txt
  bootimg.py pack <dir> <ramdisk> <out.img>    -> kernel/second/addresses from <dir>, new ramdisk
"""
import hashlib
import struct
import sys
from pathlib import Path

HDR = '<8s10I16s512s32s1024s'


def pad(n, page):
    return (n + page - 1) // page * page


def unpack(img, out):
    data = Path(img).read_bytes()
    (magic, ksize, kaddr, rsize, raddr, ssize, saddr, tags, page,
     _dt, _unused, name, cmdline, _id, _extra) = struct.unpack_from(HDR, data)
    assert magic == b'ANDROID!', 'not an Android boot image'
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    off = page
    (out / 'kernel').write_bytes(data[off:off + ksize])
    off += pad(ksize, page)
    (out / 'ramdisk').write_bytes(data[off:off + rsize])
    off += pad(rsize, page)
    (out / 'second').write_bytes(data[off:off + ssize])
    (out / 'header.txt').write_text(
        f'kernel_addr=0x{kaddr:x}\nramdisk_addr=0x{raddr:x}\nsecond_addr=0x{saddr:x}\n'
        f'tags_addr=0x{tags:x}\npage_size={page}\nname={name.rstrip(b"\0").decode()}\n'
        f'cmdline={cmdline.rstrip(b"\0").decode()}\n')


def pack(src, ramdisk_path, out):
    src = Path(src)
    h = dict(l.split('=', 1) for l in (src / 'header.txt').read_text().splitlines() if l)
    page = int(h['page_size'])
    kernel = (src / 'kernel').read_bytes()
    second = (src / 'second').read_bytes()
    ramdisk = Path(ramdisk_path).read_bytes()

    # id is computed the same way as in mkbootimg
    sha = hashlib.sha1()
    for blob in (kernel, ramdisk, second):
        sha.update(blob)
        sha.update(struct.pack('<I', len(blob)))
    header = struct.pack(
        HDR, b'ANDROID!',
        len(kernel), int(h['kernel_addr'], 16),
        len(ramdisk), int(h['ramdisk_addr'], 16),
        len(second), int(h['second_addr'], 16),
        int(h['tags_addr'], 16), page, 0, 0,
        h['name'].encode(), h['cmdline'].encode(), sha.digest(), b'')

    def padded(b):
        return b + b'\0' * (pad(len(b), page) - len(b))

    Path(out).write_bytes(padded(header) + padded(kernel) + padded(ramdisk) + padded(second))


if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[1] == 'unpack':
        unpack(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 5 and sys.argv[1] == 'pack':
        pack(sys.argv[2], sys.argv[3], sys.argv[4])
    else:
        sys.exit(__doc__)
