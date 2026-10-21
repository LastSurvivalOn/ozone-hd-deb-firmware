# GPU (Mali-450 MP) experiments

Work in progress: nothing here is deployed by the install scripts yet.

## What works

- Android's `mali.ko` (Utgard, Amlogic `meson_m450`, user API 600, no UMP, has dma_buf)
  loads on native Linux: `insmod /lib/modules/3.10.33/extra/mali.ko mali_debug_level=2`
  (as `init.amlogic.rc` does), which creates `/dev/mali` (10,49) and `/sys/class/mpgpu`.
- The armhf fbdev blobs from Amlogic's buildroot
  (github.com/hardkernel/buildroot, `package/opengl/src/lib/eabihf/<ver>/m450/libMali.so`)
  match it: **r4p1 and r5p0** (API 600). r5p1 and r6p2 are API 800 ("API mismatch").
  The blob dlopens `libMali.so`, `libGLESv2.so`, `libGLESv1_CM.so` by name, so
  `LD_LIBRARY_PATH` must point at a directory of symlinks to it.
- EGL 1.4, GLES 2.0; ~280 fps full-screen shader fill at 1280x720 (~260 Mpixel/s).
- WebKitGTK picks the blob up through `LD_LIBRARY_PATH` and renders on the GPU,
  but straight into fb0, full screen, past X.

## What doesn't

| Path | Result |
|---|---|
| X11 blobs (`m450-X`) | need UMP (`libUMP.so`, `/dev/ump`): neither the kernel nor `mali.ko` has it |
| fbdev blob in an X window | draws only to the whole of `/dev/fb0` |
| pbuffer + `glReadPixels` into X | ~130 ms per 720p frame (uncached GPU memory) |
| ION dma-buf as `fbdev_pixmap` | `EGL_BAD_NATIVE_PIXMAP` / `EGL_BAD_MATCH` |
| fb1 (OSD2) as the target | 1 MB of memory only (a cursor layer); `EGL_BAD_ALLOC` |
| Lima (Mesa) | needs kernel >= 5.2 |

## Tests (`tests/`)

Built on the box with the headers from the buildroot tree
(`native/cache/mali/include`, not in git), e.g.
`gcc -O2 -I include gltest.c -o gltest -L wrap -lMali`, and run with
`LD_LIBRARY_PATH=wrap`.

- `gltest.c`: pbuffer GLES2 fill benchmark and readback timing.
- `fbwin.c`: fbdev window animation (`W H seconds`; `FRAMEBUFFER=/dev/fbN` picks the target).
- `iontest.c`: ION allocation as an fbdev pixmap.
- `wktest.py`: a WebKit2 window (`url policy seconds`).
