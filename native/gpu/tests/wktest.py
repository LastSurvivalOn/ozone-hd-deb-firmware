import sys, time, gi
gi.require_version('Gtk', '3.0'); gi.require_version('WebKit2', '4.1')
from gi.repository import Gtk, GLib, WebKit2
url = sys.argv[1] if len(sys.argv) > 1 else 'https://webkit.org/blog-files/3d-transforms/poster-circle.html'
w = Gtk.Window(title='wktest'); w.set_default_size(1280, 680)
v = WebKit2.WebView()
s = v.get_settings()
s.set_hardware_acceleration_policy(getattr(WebKit2.HardwareAccelerationPolicy, sys.argv[2] if len(sys.argv) > 2 else 'ALWAYS'))
v.connect('load-changed', lambda v, e: print('load', e.value_nick, round(time.time() - t0, 1), flush=True))
w.add(v); w.show_all(); t0 = time.time(); v.load_uri(url)
GLib.timeout_add_seconds(int(sys.argv[3]) if len(sys.argv) > 3 else 20, Gtk.main_quit)
Gtk.main()
