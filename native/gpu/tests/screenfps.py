"""How often the screen really changes: screenfps.py [seconds [x,y ...]]

Samples pixels of /dev/fb0 (RGB565, 1280 wide) in a tight loop and counts the times any of them
changes. A page's requestAnimationFrame rate says nothing about frames that reach the screen.
X takes a few milliseconds to put a whole frame on the screen, so changes less than 12 ms after
the one before count as the same frame (fast scrolling was counted at 50 for 30 wheel steps).
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
changed_at = 0
while time.monotonic() - t0 < secs:
    now = sample()
    if now != last:
        t = time.monotonic()
        if t - changed_at > 0.012:
            changes += 1
        changed_at, last = t, now
    time.sleep(0.002)
print('screen changed %.1f times a second' % (changes / secs))
