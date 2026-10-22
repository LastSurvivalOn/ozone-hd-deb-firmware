# GPU (Mali-450 MP) under X11

Installed by `../install-gpu.sh <ip>` (and games by `../install-games.sh <ip>`). What it gives:

- the desktop's X runs the Mali driver (with the plain framebuffer as the fallback);
- OpenGL ES 2.0 for programs started with `ozone-gl`; mpv and RetroArch use it by themselves;
- computing in fragment shaders.

The browser does not use it (see the end of Results).

The way Amlogic and Hardkernel (ODROID-C1, the same S805) ran Mali under X11:
the Mali X11 userspace driver (`libMali.so`, r5p0, EGL on DRI2) shares buffers
with the X server through UMP, and the Mali DDX (`mali_drv.so`) allocates every
pixmap in UMP memory. The stock kernel has neither UMP nor DRM, so this needs
modules of our own.

## Kernel modules (`build-modules.sh`, on the laptop)

`build-modules.sh` builds them into `native/out/gpu/`:

| Module | What |
|---|---|
| `ump.ko` | Unified Memory Provider (`/dev/ump`) |
| `umplock.ko` | cross-process buffer locks (`/dev/umplock`) |
| `mali.ko` | Mali r5p0 (API 600) with UMP; replaces Android's `mali.ko` |
| `hdmi.ko` | `drivers/video/hdmi.c`, which `drm.ko` needs and the kernel lacks |
| `drm.ko` | the DRM core, 3.10.33 |
| `mali_drm.ko` | DRM stub, for DRI2 authentication (`/dev/dri/card0`) |
| `fbump.ko` | fb0 memory as a UMP buffer (ours, see below) |

Load order: ump, umplock, mali, hdmi, drm, mali_drm, fbump.

- Sources are from Hardkernel's kernel: the `odroidc-3.10.y-android` branch
  (3.10.33) for the tree, and the last Mali r5p0 from `odroidc-3.10.y` for the
  driver.
- The config is the box's own (`kernel-3.10.33.config`, IKCONFIG out of the
  stock `boot.img`).
- The toolchain is AOSP's `arm-eabi-4.8`; the kernel was built with Linaro 4.7.
- The kernel has no MODVERSIONS, so only vermagic is checked and struct layouts
  are our responsibility. The OS-layer functions of our `mali.ko` access the
  same `task_struct`, `vm_area_struct`, `file`, dma-buf and sync offsets as
  Android's `mali.ko`.

Patches (`kernel/patches/`):

- `ump-as-module.patch`: Hardkernel built UMP into the kernel, next to Mali.
  As a module it needs its own copy of the Mali OS layer and `map_errcode()`.
- `mali-never-unload.patch`: `mali.ko` pins itself. The vendor `aml_thermal.ko`
  keeps calling the GPU cooling device after the module is gone, and with
  `panic_on_oops=1` that reboots the box.
  **Never `rmmod` Android's `mali.ko` either**: it fails the same way.

`fbump.ko` stands in for `CONFIG_FB_AMLOGIC_UMP`, which gives the framebuffer
as UMP through the fb ioctls `GET_UMP_SECURE_ID_BUF1/2`. It wraps all of fb0
and puts the secure id in `/sys/module/fbump/parameters/secure_id`.

## The X driver (`ddx/`, on the box)

`ddx/` is Hardkernel's `c1_mali_ddx` (ARM's xf86-video-mali, MIT).
It builds against Xorg 21 unchanged, apart from a Makefile and `config.h`
instead of autotools. Changes:

- `ShadowFB` (on by default): the screen pixmap lives in cached UMP memory, and
  the damaged parts go to the framebuffer before X waits for clients. Drawing
  straight to the framebuffer reads uncached memory: a window scroll
  (CopyArea) took 47.5 ms instead of 3. With the shadow there is no page
  flipping.
- Without the shadow, it falls back to the `fbump` secure id when the fb
  ioctls fail.
- `fbdev_crtc_config_resize` set the screen pixmap's width to its height.

```
make -C ddx MALI_INC=<mali_headers> MALI_LIB=<dir with libUMP.so>
```

It needs `xserver-xorg-dev` and `libdrm-dev`. The userspace driver comes from
github.com/mdrjr/c1_mali_libs at 31d5c21 (`x11/`: r5p0, with vsync and swap
interval).

## `egl-fix/`

Built as `libEGL.so.1` in front of `libMali.so`, a dependency that provides everything else.
Being the library itself, it also fixes programs that `dlopen` EGL and `dlsym` from the handle
(SDL2, libepoxy). It covers what the EGL 1.4 driver lacks and current programs expect:

| Fix | Who needs it |
|---|---|
| `eglInitialize` with NULL version pointers (Mali fails) | SDL2, mpv |
| `EGL_EXT_platform_base`/`platform_x11` on top of `eglGetDisplay`; client extensions from `eglQueryString(EGL_NO_DISPLAY)` | mpv, glmark2 |
| core GL functions from `eglGetProcAddress` (`EGL_KHR_client_get_all_proc_addresses`); Mali returns NULL, and programs then took glvnd's `libGL.so.1` stubs | mpv |
| a TrueColor native visual for the RGB565 configs, which name a DirectColor one | glmark2 |
| on the 16-bit screen, window configs of up to 8 bits a colour list RGB565 first | mpv |

The last one matters because an 8888 config takes a 32-bit visual, and X then converts every frame
on the CPU. With it, mpv dropped from 190 % to 111 % CPU.

Building against it needs `-Wl,-rpath-link` (`libUMP.so`) and `-lGLESv2` as well:
`pkg-config mali-gles` has both.

## On the box

| What | Where |
|---|---|
| modules | `/lib/modules/3.10.33/extra/gpu/`, loaded at boot by `/etc/init.d/ozone-gpu` (native only); it marks `/run/ozone-gpu` |
| skip the GPU | `touch /etc/ozone/no-gpu` |
| X screen config | `/usr/local/lib/ozone/gen-xorg-screen` writes `00-ozone-screen.conf` before X starts: `mali` when the GPU loaded, else `fbdev` |
| X driver | `/usr/lib/xorg/modules/drivers/mali_drv.so` |
| userspace driver | `/usr/local/lib/mali`: `libMali.so`, `libUMP.so`, and `libEGL.so.1`, which is `egl-fix` linked to `libMali.so` |
| building your own GL code | `pkg-config --cflags --libs mali-gles` |

- The desktop script marks `/run/ozone-gpu-desktop` when X has the Mali driver.
- If that X dies with "Fatal server error", the script sets `/run/ozone-gpu-failed`, and the next X
  runs on fbdev until a reboot. The log goes to `/var/log/ozone-desktop-gpu-failed.log`.
- `ozone-gl PROGRAM` puts `/usr/local/lib/mali` first in the library path, with SDL2 on EGL/GLES.
  Without the Mali desktop it changes nothing.
- `/usr/local/bin/mpv` runs mpv with `--vo=gpu --gpu-context=x11egl` and drops the config's CPU
  scaling filter.
- `/usr/local/bin/retroarch` runs RetroArch with `/etc/ozone/retroarch.cfg` appended.

## Results

These were measured on a test X server (`:1`, sharing fb0).

GLES2 in an ordinary X11 window (`tests/x11win.c`), with the shadow and the direct framebuffer
copy (below):

| Window | fps |
|---|---|
| 640x360 | 242 |
| 1280x720 (full screen) | 120 |

The direct copy: ShadowFB went from back buffer to shadow to framebuffer, two copies of the frame,
which this memory bus can't afford at 720p60. Now a DRI2 swap goes from the back buffer straight to
the framebuffer where the window shows, and the shadow is skipped there.
A 32-bit window is the other case: RetroArch accepts only 8888 configs, and Composite redirects such
a window on the 16-bit screen. Its frame is converted to RGB565 with NEON on the way, instead of
pixman compositing it from the window pixmap. For full-screen NES in RetroArch (Nestopia) this went
57 → 61 → 82 fps.

Games (`install-games.sh`):

- RetroArch on `gl` with the RGUI menu; NES (Nestopia), Game Boy (Gambatte) and GBA (mGBA) run at
  full speed.
- glmark2-es2 at 720p full screen: ~110 fps in the simple scenes, score 81. It needs
  `--visual-config r=5:g=6:b=5:a=0:d=24:s=8`, as every Mali config has a stencil buffer.

Computing (`tests/gpufilter.c`), a 3x3 blur of a 1280x720 RGBA image:

| Step | ms |
|---|---|
| in (`XShmPutImage`) | 6 |
| shader | 14 |
| out (`XShmGetImage`) | 7 |
| GPU, all of it | 27 |
| CPU, the same blur | 180 |

Write the result to an FBO whose texture is an EGLImage of an X pixmap. A pixmap lives in cached UMP
memory, so X reads it in ~3 ms. `glReadPixels` takes 160 ms for 720p, as the driver is the slow part,
not the memory. For the same reason the input pixmap is a texture with no upload.
Targets compared on that blur:

| Target | ms |
|---|---|
| FBO on a pixmap | 10.8 |
| pbuffer | 39 |
| pixmap surface | 207 |

Limits:

- GLES 2.0 has no compute shaders and no float render targets: results are RGBA8.
- Fragment shaders run at fp16 (`mediump`).
- Texture coordinates computed in the fragment shader make dependent reads, 3x slower than ones
  passed as varyings.

Standby (Android early suspend) and waking leave the GPU working; Mali has no early suspend handler.

Pure GPU fill is ~280 fps at 720p.

2D, 1280x680 (`tests/x2dbench.c`), in ms:

| Operation | fbdev (shadow) | mali, direct | mali, ShadowFB |
|---|---|---|---|
| ShmPutImage | 3.0 | 1.9 | 3.2 |
| CopyArea (scroll) | 3.0 | 47.5 | 2.7 |
| FillRectangle | 1.0 | 0.6 | 0.9 |

mpv, full screen, H.264 decoded on the CPU. `vo=x11` uses the config's tuned scaler; `vo=gpu` uses
GLES 2.0 in "dumb mode" with a 565 config:

| Video | `vo=x11` | `vo=gpu` |
|---|---|---|
| 360p, CPU (mpv + Xorg) | 190 % | 111 % |
| 360p, untimed | 27.5 fps | 35 fps |
| 720p, frames dropped in 20 s | 140 | 57 |
| 720p, untimed | 22.6 fps | 21.4 fps |

Decoding alone is 26 fps at 720p. Texture uploads are the GPU side's cost: there is no
`GL_EXT_unpack_subimage`, and the driver retiles on the CPU.

WebKitGTK 2.50 on the GPU, through `egl-fix` (no change to WebKit). Out of the box it turns
acceleration off (`webkit://gpu`: "Policy: never"):

- the UI process wants `EGL_MESA_platform_surfaceless` or `EGL_KHR_platform_gbm` among the client
  extensions;
- the web process wants that surfaceless display and `EGL_KHR_surfaceless_context`;
- with no DMA-BUF it renders each frame into an RGBA8 renderbuffer and copies it out with
  `glReadPixels(GL_BGRA)` into shared memory for the UI process. Mali doesn't read BGRA at all
  (`GL_INVALID_ENUM`), and 720p takes it 160 ms in RGBA.

`egl-fix` makes up all three. The surfaceless display is an X connection of its own, and a context
without a surface gets a hidden 1x1 pbuffer. An RGBA8 renderbuffer gets a texture on an EGLImage of
an X pixmap, in its place as the colour attachment. `glReadPixels` of it copies the pixmap's UMP
memory, which DRI2 names: 16 ms for 1280x680, with no X request.

Two traps on the way:

- WebKit's process has glvnd's `libGL.so.1` loaded. Every libMali function the shim calls goes
  through a pointer from `dlsym` on libMali's handle (`MALI()`), as a plain call binds to glvnd's
  stubs. libMali's own extension table resolved to those stubs too (`glEGLImageTargetTexture2DOES`
  did nothing): `eglGetProcAddress` now looks in libMali first.
- `glEGLImageTargetRenderbufferStorageOES` left the renderbuffer 0x0 in WebKit, hence the texture.

Results, with webkit://gpu showing the render process on "Mali-450 MP", screen updates a second:

| Test | CPU (as ozone-web) | GPU |
|---|---|---|
| CSS animation, three big layers | 3 | 24 |
| smooth scrolling Wikipedia | 6.5 | 9.7 |
| ozone-web wheel scrolling Wikipedia (no smooth scrolling) | 5.2 | 4.5 |

The limit is X:

- every frame crosses whole through shared memory;
- GTK clears its double buffer twice;
- cairo uploads the frame into a new pixmap and composites it flipped (GL rows go bottom up).

That is ~24 MB of memory traffic a frame, ~25 frames a second on this bus, and X at 100 %. The CPU
path redraws only the strip a scroll exposes. So ozone-web takes the GPU only with
`OZONE_WEB_GPU=1`. It costs ~45 MB of memory.

The X driver now does less for every client:

- pixmaps live in system memory until DRI2 gives them to the GPU;
- freed pixmap memory is pooled;
- MIT-SHM has shared pixmaps.

Under WebKit this took X from 35000 page faults and ~1150 UMP cache ioctls a second down to none.

## A WebKit of our own (in progress)

Built and running (2026-10-22): `webkit/build.sh` cross-builds Debian's 2.50.6-1~deb12u2 with
`webkit/patches/0001-x11-pixmap-renderer.patch`, `install-webkit.sh` puts it in
`/usr/local/lib/ozone-webkit`, and `OZONE_WEB_GPU=1 ozone-web` uses it with frames as X pixmaps.
The build runs in docker (Debian 12, clang-16 for armhf), capped at all but 4 GB of the laptop's
memory: 8 compile jobs on WebCore once took all 15 GB and froze the desktop.

`wkfps.py 14 ALWAYS css` (the compositor alone), screen updates a second:

| WebKit | screen | X | UI process | web process |
|---|---|---|---|---|
| Debian's, shared memory | 28.8 | 99 % | 24 % | 61 % |
| ours, shared memory | 29.2 | 100 % | 22 % | 60 % |
| ours, X pixmaps | 34.6 | 81 % | 9 % | 49 % |

With 0002 (only the damage is drawn again, SOURCE for an opaque page) and 0003 (the view in an
X window of its own, without GTK's double buffer), CSS animations: 35.2 a second, X at 62 %.

A static page with a small spinning indicator (`tests/spinner.html`: text and an 80 px CSS
animation) shows the damage at work, as most real pages animate a small part:

| WebKit | X | UI process | web process |
|---|---|---|---|
| Debian's, shared memory | 101 % | 21 % | 74 % |
| ours, shared memory | 100 % | 22 % | 71 % |
| ours, pixmaps + damage (0002) | 29 % | 10 % | 58 % |
| ours, + no double buffer (0003) | 76 % at 46 updates a second | 6 % | 87 % |

With 0003 the spinner reaches 45.8 screen updates a second (the text around it none), so the CPU
goes into more frames, not into whole ones.

The page itself runs at 61.7 frames a second.

Wheel scrolling (`tests/scrolltest.sh`: one `xdotool click --repeat` at 30 steps a second,
`screenfps.py` on a grid of 60 points), screen updates a second:

| ozone-web | Wikipedia (JS) | `tests/long.html` (static) |
|---|---|---|
| CPU (the default) | 12.0, X 14 % | 32.3, X 27 % |
| GPU, Debian's WebKit | 21.3, X 74 % | 23.7, X 86 % |
| GPU, ours (pixmaps) | 25.3, X 48 % | 29.3, X 56 % |

On a page with scripts the GPU path scrolls twice as fast: in composited mode WebKit scrolls on
its scrolling thread (async scrolling, on by default for GTK), without waiting for the page's
JavaScript, while the CPU path scrolls on the main thread. On a static page the CPU path is
ahead, as it draws only the strip a scroll uncovers and X moves the rest. The earlier finding
that scrolling gained nothing came from the method: one `xdotool` process per wheel step gave
~10 steps a second, and 4 sampled points often fell between lines of text. X is still the busiest: it composites the whole 32-bit frame onto
GTK's 16-bit double buffer (OVER, so it reads that too) and copies that to the window.

The plan it followed, written before the build:

Scrolling on the GPU needs WebKit to hand its frames to X as pixmaps, not through shared memory.
The shims can't do that, because the copy into shared memory and the cairo upload happen inside
WebKit's two processes. Plan, written 2026-10-21, against the box's `libwebkit2gtk-4.1-0`
2.50.6-1~deb12u2:

1. **Build it unchanged first.**
   - Cross-build on the laptop. The box can't: a link alone needs several GB of RAM.
   - Source: `apt source webkit2gtk` at exactly the box's Debian version, so the library can
     replace the packaged one with the same ABI.
   - Toolchain: a bookworm chroot with `dpkg --add-architecture armhf`, `crossbuild-essential-armhf`
     and the armhf build-deps (`DEB_BUILD_PROFILES="cross nocheck nodoc"`). Not qemu-user: that
     would take days.
   - Build only the 4.1 (GTK 3) library and its processes, Release, no debug info (`-g0`), with
     lld or gold. Expect hours and 30–50 GB of disk.
   - Milestone: `ozone-web` runs on the box with the new `libwebkit2gtk-4.1.so.0`,
     `libjavascriptcoregtk-4.1.so.0` and `WebKitWebProcess`, all taken from
     `/usr/local/lib/ozone-webkit` through `LD_LIBRARY_PATH`. The Debian package stays installed
     and untouched, and its typelibs keep working because the ABI is the same.
2. **Web process: render into X pixmaps.**
   - Add a render target next to the shared-memory one: an X pixmap, with a texture on its
     EGLImage as the FBO's colour attachment. This is what `gles-fix.c` already does under
     WebKit.
   - Keep WebKit's swapchain of buffers (one pixmap per buffer). Flip the rows when drawing so
     that X gets them top down.
   - `glFinish` before the frame is announced, and send the pixmap's XID over IPC in place of the
     shared-memory handle.
   - The X connection can be the one the shims' surfaceless display already opens.
3. **UI process: show the pixmap.**
   - In the backing store's paint, wrap the XID in `cairo_xlib_surface_create`. GTK then
     composites it inside X (XRender), with no copy in the client.
   - If that is still slow, use a child X window under the view and `XCopyArea` into it.
   - Pass WebKit's damage rectangles on, so that X copies only what changed.
4. **Measure** with `screenfps.py` and `wkfps.py` against the table above.
   - Goal: wheel scrolling in ozone-web beats the CPU path (5.2 updates a second).
   - Expected traffic per frame: the GPU writes 3.5 MB, X reads it and writes 1.7 MB of 565 into
     the shadow, and the flush copies 1.7 MB. That is about 9 MB against ~24 MB now.
   - The display side is known to keep up: a 720p GLES window already reaches 120 fps over DRI2.
5. **Ship** the libraries in `/usr/local/lib/ozone-webkit` and point `ozone-web` at them only
   with `OZONE_WEB_GPU=1`. Every Debian security update of webkit2gtk needs a rebuild (or a hold),
   because the private library ABI follows the package version.

Risks:
- WebKit's X11 support in the GTK 3 port is shrinking release by release.
- The IPC and backing-store classes change between minor versions, so the patch is tied to
  2.50.
- Page JavaScript still limits heavy sites, which no GPU fixes.

## fbdev-only findings (before UMP)

- Android's `mali.ko` works with the fbdev blobs from Amlogic's buildroot
  (github.com/hardkernel/buildroot, `package/opengl/src/lib/eabihf/<ver>/m450`):
  r4p1 and r5p0 (API 600).
- r5p1 and r6p2 are API 800 ("API mismatch").
- The fbdev blob draws only to the whole of `/dev/fb0`, past X.
- `glReadPixels` of a 720p frame takes ~130 ms (uncached GPU memory).
- ION dma-buf as an `fbdev_pixmap` fails with `EGL_BAD_NATIVE_PIXMAP`.
- fb1 has only 1 MB, as a cursor layer.

## Tests (`tests/`)

| Test | What |
|---|---|
| `x11win.c` | GLES2 animation in an X11 window (`W H seconds x y`) |
| `gltest.c` | pbuffer fill benchmark and readback timing |
| `fbwin.c` | fbdev window animation (`FRAMEBUFFER=/dev/fbN`) |
| `iontest.c` | ION allocation as an fbdev pixmap |
| `wktest.py` | a WebKit2 window (`url policy seconds`) |
| `wkfps.py` | WebKit animation frame rate (`seconds policy canvas|css|URL`) |
| `x2dbench.c` | X 2D speed: ShmPutImage, CopyArea, FillRectangle |
| `pixmapread.c` | render into an X pixmap, read it back with XShmGetImage vs glReadPixels |
| `gpufilter.c` | GPU computing round trip (a blur), against the CPU |
| `readpixels-spy.c` | LD_PRELOAD: glReadPixels and eglSwapBuffers calls and time |
| `rbread.c` | WebKit's frame readback: surfaceless display, RGBA8 renderbuffer, glReadPixels(BGRA) |
| `screenfps.py` | how often the screen really changes (samples fb0; changes within 12 ms are one frame) |
| `scrolltest.sh` | ozone-web wheel scrolling (`MODE URL CLICKS INTERVAL`, modes cpu, debian, pixmap) |
| `long.html`, `spinner.html` | a static page to scroll; static text with a small CSS animation |
| `xpixmap2conn.c` | rendering into another X connection's pixmap, and its row order (our WebKit's case) |
