"""Frame rate of an animated page in WebKitGTK: wkfps.py [seconds [policy [canvas|css|URL]]]

The page counts requestAnimationFrame callbacks and puts the rate in its title.
"canvas" (the default) draws a full-window 2D canvas every frame; "css" only
moves big layers with CSS animations, work for the compositor alone.
"""
import sys, time, gi
gi.require_version('Gtk', '3.0'); gi.require_version('WebKit2', '4.1')
from gi.repository import Gtk, GLib, WebKit2

PAGE = """<!doctype html><html><body style="margin:0;background:#1e1e2e;overflow:hidden">
<canvas id=c width=1280 height=680 style="position:absolute"></canvas>
<div id=b style="position:absolute;left:100px;top:100px;width:300px;height:300px;
 background:linear-gradient(45deg,#f38ba8,#89b4fa);border-radius:40px"></div>
<script>
const CANVAS = true;
const c = document.getElementById('c').getContext('2d'), b = document.getElementById('b');
let n = 0, t0 = performance.now();
function frame(t) {
  if (CANVAS) {
  c.fillStyle = '#1e1e2e'; c.fillRect(0, 0, 1280, 680);
  for (let i = 0; i < 40; i++) {
    c.fillStyle = 'hsl(' + ((t / 10 + i * 9) % 360) + ',70%,60%)';
    c.beginPath(); c.arc(640 + 400 * Math.sin(t / 900 + i), 340 + 250 * Math.cos(t / 700 + i * 1.3), 30, 0, 7); c.fill();
  }
  }
  b.style.transform = 'translate(' + (400 + 300 * Math.sin(t / 500)) + 'px,0) rotate(' + (t / 8) + 'deg)';
  n++;
  if (t - t0 > 1000) { document.title = (n * 1000 / (t - t0)).toFixed(1); n = 0; t0 = t; }
  requestAnimationFrame(frame);
}
requestAnimationFrame(frame);
</script></body></html>"""

secs = int(sys.argv[1]) if len(sys.argv) > 1 else 12
policy = sys.argv[2] if len(sys.argv) > 2 else 'ALWAYS'
w = Gtk.Window(title='wkfps'); w.set_default_size(1280, 680)
v = WebKit2.WebView()
v.get_settings().set_hardware_acceleration_policy(getattr(WebKit2.HardwareAccelerationPolicy, policy))
rates = []
v.connect('notify::title', lambda v, p: (rates.append(float(v.get_title())), print('fps', v.get_title(), flush=True))
          if (v.get_title() or '').replace('.', '').isdigit() else None)
w.add(v); w.show_all()
CSS = """<!doctype html><html><head><style>
body { margin: 0; overflow: hidden; background: linear-gradient(120deg, #1e1e2e, #45475a) }
.l { position: absolute; width: 600px; height: 400px; border-radius: 40px; opacity: .8;
     background: linear-gradient(45deg, #f38ba8, #89b4fa); animation: m 3s linear infinite }
@keyframes m { from { transform: translate(0, 0) rotate(0) } 50% { transform: translate(600px, 250px) rotate(180deg) }
               to { transform: translate(0, 0) rotate(360deg) } }
</style></head><body><div class=l></div><div class=l style="animation-delay:-1s"></div>
<div class=l style="animation-delay:-2s"></div><script>
let n = 0, t0 = performance.now();
function frame(t) { n++; if (t - t0 > 1000) { document.title = (n * 1000 / (t - t0)).toFixed(1); n = 0; t0 = t; }
                    requestAnimationFrame(frame); }
requestAnimationFrame(frame);
</script></body></html>"""
what = sys.argv[3] if len(sys.argv) > 3 else 'canvas'
if what == 'canvas':
    v.load_html(PAGE, 'about:blank')
elif what == 'css':
    v.load_html(CSS, 'about:blank')
else:
    v.load_uri(what)
GLib.timeout_add_seconds(secs, Gtk.main_quit)
Gtk.main()
if rates[2:]:
    print('median fps %.1f' % sorted(rates[2:])[len(rates[2:]) // 2])
