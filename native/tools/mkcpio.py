#!/usr/bin/env python3
"""Writes an initramfs (cpio newc + gzip) from a spec list, without root or fakeroot.

Each spec line:
  dir   <path> <mode>
  file  <path> <mode> <local file>
  nod   <path> <mode> c <major> <minor>
Owner is always root:root.
"""
import gzip
import stat
import sys
from pathlib import Path


def entry(name, mode, data=b'', rdev=(0, 0), ino=[0]):
    ino[0] += 1
    name_b = name.encode() + b'\0'
    hdr = '070701' + ''.join(f'{v:08X}' for v in (
        ino[0], mode, 0, 0, 1, 0, len(data), 0, 0, rdev[0], rdev[1], len(name_b), 0))
    out = hdr.encode() + name_b
    out += b'\0' * (-len(out) % 4)
    out += data + b'\0' * (-len(data) % 4)
    return out


def build(spec, out):
    blob = b''
    for line in Path(spec).read_text().splitlines():
        f = line.split()
        if not f or f[0].startswith('#'):
            continue
        kind, path, mode = f[0], f[1].lstrip('/'), int(f[2], 8)
        if kind == 'dir':
            blob += entry(path, stat.S_IFDIR | mode)
        elif kind == 'file':
            blob += entry(path, stat.S_IFREG | mode, Path(f[3]).read_bytes())
        elif kind == 'nod':
            blob += entry(path, stat.S_IFCHR | mode, rdev=(int(f[4]), int(f[5])))
        else:
            sys.exit(f'unknown type: {line}')
    blob += entry('TRAILER!!!', 0)
    Path(out).write_bytes(gzip.compress(blob, 9, mtime=0))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    build(sys.argv[1], sys.argv[2])
