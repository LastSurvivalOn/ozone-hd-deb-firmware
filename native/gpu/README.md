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
instead of autotools. One change: it falls back to the `fbump` secure id when
the fb ioctls fail.

```
make -C ddx MALI_INC=<mali_headers> MALI_LIB=<dir with libUMP.so>
```

It needs `xserver-xorg-dev` and `libdrm-dev`. The userspace driver comes from
github.com/mdrjr/c1_mali_libs at 31d5c21 (`x11/`: r5p0, with vsync and swap
interval).

## Results

On a test X server (`:1`, sharing fb0) with an ordinary X11 window
(`tests/x11win.c`):

| Window | fps |
|---|---|
| 640x360 | 307 |
| 1000x600 | 162 |
| 1280x720 (full screen) | 95 |

The swap copies the window from cached UMP memory, ~7 ms for 720p.
Pure GPU fill is ~280 fps at 720p.

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
