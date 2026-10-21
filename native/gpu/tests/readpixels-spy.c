/* LD_PRELOAD shim: how often and how long a process calls glReadPixels and eglSwapBuffers.
 * libepoxy looks GL functions up with dlsym() on the library handle, so dlsym is wrapped:
 * the real function comes from the handle asked for, the caller gets the wrapper.
 * gcc -O2 -shared -fPIC readpixels-spy.c -o readpixels-spy.so -ldl */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void *(*real_dlsym)(void *, const char *);
static void (*real_readpixels)(int, int, int, int, unsigned, unsigned, void *);
static unsigned (*real_swap)(void *, void *);

static double last, rp_time, sw_time;
static int rp_calls, sw_calls;
static long rp_pixels;

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static void report(void)
{
    double t = now();
    if (t - last < 2)
        return;
    fprintf(stderr, "[spy %d] per s: glReadPixels %.1f calls, %.0f kpixel, %.0f ms; eglSwapBuffers %.1f calls, %.0f ms\n",
            getpid(), rp_calls / (t - last), rp_pixels / (t - last) / 1000, rp_time / (t - last) * 1000,
            sw_calls / (t - last), sw_time / (t - last) * 1000);
    last = t, rp_time = sw_time = 0, rp_calls = sw_calls = 0, rp_pixels = 0;
}

static void spy_readpixels(int x, int y, int w, int h, unsigned format, unsigned type, void *data)
{
    double t = now();
    real_readpixels(x, y, w, h, format, type, data);
    rp_time += now() - t, rp_calls++, rp_pixels += (long)w * h;
    report();
}

static unsigned spy_swap(void *dpy, void *surface)
{
    double t = now();
    unsigned r = real_swap(dpy, surface);
    sw_time += now() - t, sw_calls++;
    report();
    return r;
}

void *dlsym(void *handle, const char *name)
{
    if (!real_dlsym)
        real_dlsym = dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.34");
    void *f = real_dlsym(handle, name);
    if (f && !strcmp(name, "glReadPixels")) {
        real_readpixels = f;
        return (void *)spy_readpixels;
    }
    if (f && !strcmp(name, "eglSwapBuffers")) {
        real_swap = f;
        return (void *)spy_swap;
    }
    return f;
}
