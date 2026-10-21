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

WebKitGTK 2.50 does not use the GPU (`webkit://gpu`: "Policy: never"):

- Its render process needs an EGL 1.5 platform display: surfaceless or GBM.
- It hands frames over as DMA-BUF or shared memory. Shared memory means
  `glReadPixels`, which takes ~130 ms on this GPU.
- With Mali, a CSS animation of three big layers runs at the same 2.1 fps as
  CPU rendering, all in the WebProcess main thread (Skia on the CPU).
- So the browser needs WebKit itself changed, not just a driver.

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
