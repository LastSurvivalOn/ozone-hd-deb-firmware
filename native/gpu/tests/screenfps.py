"""How often the screen really changes: screenfps.py [seconds [x,y ...]]

Samples pixels of /dev/fb0 (RGB565, 1280 wide) in a tight loop and counts the times any of them
changes. A page's requestAnimationFrame rate says nothing about frames that reach the screen.
"""
import os, sys, time

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 3
points = [tuple(map(int, p.split(','))) for p in sys.argv[2:]] or [(640, 360), (400, 300), (900, 400), (300, 500)]
fb = os.open('/dev/fb0', os.O_RDONLY)


def sample():
    out = []
    for x, y in points:
        os.lseek(fb, (y * 1280 + x) * 2, 0)
        out.append(os.read(fb, 2))
    return out


last, changes, t0 = sample(), 0, time.monotonic()
while time.monotonic() - t0 < secs:
    now = sample()
    if now != last:
        changes, last = changes + 1, now
    time.sleep(0.002)
print('screen changed %.1f times a second' % (changes / secs))
