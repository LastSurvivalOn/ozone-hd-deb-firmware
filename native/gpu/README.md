# GPU (Mali-450 MP) under X11

Work in progress: nothing here is deployed by the install scripts yet.

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

`libmali-egl-fix.so`, preloaded (or linked ahead of `libMali.so`). It covers
what the EGL 1.4 driver lacks and current programs expect:

- `eglInitialize` with NULL version pointers (it fails);
- `EGL_EXT_platform_base` and `platform_x11`, on top of `eglGetDisplay`;
- client extensions from `eglQueryString(EGL_NO_DISPLAY)`.

## Results

These were measured on a test X server (`:1`, sharing fb0).

GLES2 in an ordinary X11 window (`tests/x11win.c`), with the shadow:

| Window | fps |
|---|---|
| 640x360 | 242 |
| 1280x720 (full screen) | 92 |

Pure GPU fill is ~280 fps at 720p.

2D, 1280x680 (`tests/x2dbench.c`), in ms:

| Operation | fbdev (shadow) | mali, direct | mali, ShadowFB |
|---|---|---|---|
| ShmPutImage | 3.0 | 1.9 | 3.2 |
| CopyArea (scroll) | 3.0 | 47.5 | 2.7 |
| FillRectangle | 1.0 | 0.6 | 0.9 |

mpv, full screen, a yuv420p source without decoding (`--untimed`), in fps.
`vo=gpu` runs on GLES 2.0 in "dumb mode", with `egl-fix`:

| Source | source alone | `vo=x11` | `vo=gpu` |
|---|---|---|---|
| 640x360 | 65 | 8.4 | 32 |
| 1280x720 | 39 | 8.5 | 22.5 |

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
