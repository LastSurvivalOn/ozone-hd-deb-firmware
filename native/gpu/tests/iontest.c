/* Render with Mali into an ION dma-buf (fbdev pixmap) and read it back with the CPU.
 * iontest HEAPMASK CACHED(0|1) */
#define MALI_USE_DMA_BUF 1
#include <EGL/egl.h>
#include <EGL/fbdev_window.h>
#include <GLES2/gl2.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

struct ion_allocation_data { size_t len, align; unsigned int heap_id_mask, flags; int handle; };
struct ion_fd_data { int handle, fd; };
struct ion_handle_data { int handle; };
#define ION_IOC_ALLOC _IOWR('I', 0, struct ion_allocation_data)
#define ION_IOC_SHARE _IOWR('I', 4, struct ion_fd_data)
#define ION_IOC_SYNC  _IOWR('I', 7, struct ion_fd_data)

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
    unsigned mask = argc > 1 ? strtoul(argv[1], 0, 0) : 1;
    int cached = argc > 2 ? atoi(argv[2]) : 0;
    const int W = 1280, H = 720, size = W * H * 2;
    int ion = open("/dev/ion", O_RDWR);
    struct ion_allocation_data a = { size, 4096, mask, cached ? 3 : 0, 0 };
    if (ioctl(ion, ION_IOC_ALLOC, &a)) { perror("ION_IOC_ALLOC"); return 1; }
    struct ion_fd_data sh = { a.handle, -1 };
    if (ioctl(ion, ION_IOC_SHARE, &sh)) { perror("ION_IOC_SHARE"); return 1; }
    void *ptr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, sh.fd, 0);
    if (ptr == MAP_FAILED) { perror("mmap"); return 1; }
    memset(ptr, 0x55, size);
    printf("ION buffer: heap mask 0x%x, %s, fd %d\n", mask, cached ? "cached" : "uncached", sh.fd);

    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(d, 0, 0);
    EGLint attr[] = { EGL_SURFACE_TYPE, EGL_PIXMAP_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                      EGL_RED_SIZE, 5, EGL_GREEN_SIZE, 6, EGL_BLUE_SIZE, 5, EGL_ALPHA_SIZE, 0, EGL_NONE };
    EGLConfig cfg;
    EGLint n;
    if (!eglChooseConfig(d, attr, &cfg, 1, &n) || !n) { printf("no pixmap config\n"); return 1; }
    struct fbdev_dma_buf db = { sh.fd, size, ptr };
    int mode = argc > 3 ? atoi(argv[3]) : 0;
    unsigned short *mem = malloc(size);
    fbdev_pixmap pm = { H, W, 2, 16, 5, 6, 5, 0, 0, FBDEV_PIXMAP_DMA_BUF, (unsigned short *)&db, 0 };
    if (mode == 1) { pm.flags = FBDEV_PIXMAP_DEFAULT; pm.data = mem; ptr = mem; }
    if (mode == 2) { pm.flags = FBDEV_PIXMAP_DEFAULT; pm.data = ptr; }
    if (mode == 3) { pm.flags = FBDEV_PIXMAP_EGL_MEMORY; pm.data = NULL; }
    printf("mode %d flags 0x%x\n", mode, pm.flags);
    EGLSurface s = eglCreatePixmapSurface(d, cfg, (EGLNativePixmapType)&pm, NULL);
    if (s == EGL_NO_SURFACE) { printf("eglCreatePixmapSurface failed 0x%x\n", eglGetError()); return 1; }
    if (mode == 3) { ptr = pm.data; printf("EGL memory at %p\n", ptr); }
    EGLint cattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, cattr);
    if (c == EGL_NO_CONTEXT) { printf("eglCreateContext failed 0x%x\n", eglGetError()); return 1; }
    if (!eglMakeCurrent(d, s, s, c)) { printf("eglMakeCurrent failed 0x%x\n", eglGetError()); return 1; }

    unsigned short *copy = malloc(size);
    double render = 0, sync = 0, cpy = 0;
    for (int i = 0; i < 30; i++) {
        double t0 = now();
        glClearColor((i & 1) ? 1.0f : 0.0f, 0.5f, (i & 1) ? 0.0f : 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glFinish();
        eglWaitGL();
        double t1 = now();
        if (cached)
            ioctl(ion, ION_IOC_SYNC, &sh);
        double t2 = now();
        memcpy(copy, ptr, size);
        double t3 = now();
        render += t1 - t0, sync += t2 - t1, cpy += t3 - t2;
        if (i >= 28) {
            unsigned short p = copy[W * (H / 2) + W / 2];
            printf("frame %d: centre pixel r=%d g=%d b=%d (expected %s)\n", i, p >> 11, (p >> 5) & 63, p & 31,
                   (i & 1) ? "31,32,0" : "0,32,31");
        }
    }
    printf("per frame: clear+finish %.1f ms, sync %.1f ms, CPU copy of %d KB %.1f ms\n",
           render / 30 * 1000, sync / 30 * 1000, size / 1024, cpy / 30 * 1000);
    return 0;
}
