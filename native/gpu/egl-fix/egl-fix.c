/* Fixes for the Mali r5p0 X11 EGL (EGL 1.4) that today's applications trip over.
 * Preloaded, or linked ahead of libMali.so:
 *   gcc -O2 -shared -fPIC egl-fix.c -o libmali-egl-fix.so -ldl
 *
 * - eglInitialize() fails when major or minor is NULL, which the spec allows.
 * - No EGL_EXT_platform_base: mpv and others get their X11 display only through
 *   eglGetPlatformDisplay(EXT). Added on top of eglGetDisplay() for X11, the
 *   only platform the driver has.
 * - eglQueryString(EGL_NO_DISPLAY, ...) returns NULL; it now lists the client
 *   extensions above. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <string.h>
#include <EGL/egl.h>
#include <X11/Xlib.h>

#ifndef EGL_PLATFORM_X11_KHR
#define EGL_PLATFORM_X11_KHR 0x31D5
#endif

#define REAL(name) \
    static __typeof__(&name) real_##name; \
    if (!real_##name) \
        real_##name = (__typeof__(&name))dlsym(RTLD_NEXT, #name)

static const char client_extensions[] =
    "EGL_EXT_client_extensions EGL_EXT_platform_base EGL_EXT_platform_x11 EGL_KHR_platform_x11";

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
    REAL(eglInitialize);
    EGLint ma, mi;
    return real_eglInitialize(dpy, major ? major : &ma, minor ? minor : &mi);
}

const char *eglQueryString(EGLDisplay dpy, EGLint name)
{
    REAL(eglQueryString);
    if (dpy == EGL_NO_DISPLAY && name == EGL_EXTENSIONS)
        return client_extensions;
    if (dpy == EGL_NO_DISPLAY && name == EGL_VERSION)
        return "1.4";
    return real_eglQueryString(dpy, name);
}

EGLDisplay eglGetPlatformDisplayEXT(EGLenum platform, void *native, const EGLint *attribs)
{
    (void)attribs;
    if (platform != EGL_PLATFORM_X11_KHR)
        return EGL_NO_DISPLAY;
    return eglGetDisplay((EGLNativeDisplayType)native);
}

#ifdef EGL_VERSION_1_5
EGLDisplay eglGetPlatformDisplay(EGLenum platform, void *native, const EGLAttrib *attribs)
#else
EGLDisplay eglGetPlatformDisplay(EGLenum platform, void *native, const EGLint *attribs)
#endif
{
    (void)attribs;
    return eglGetPlatformDisplayEXT(platform, native, NULL);
}

/* The platform variants take a pointer to the X11 Window or Pixmap */
EGLSurface eglCreatePlatformWindowSurfaceEXT(EGLDisplay dpy, EGLConfig config, void *native, const EGLint *attribs)
{
    return eglCreateWindowSurface(dpy, config, *(Window *)native, attribs);
}

EGLSurface eglCreatePlatformPixmapSurfaceEXT(EGLDisplay dpy, EGLConfig config, void *native, const EGLint *attribs)
{
    return eglCreatePixmapSurface(dpy, config, *(Pixmap *)native, attribs);
}

__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *name)
{
    REAL(eglGetProcAddress);
    static const struct { const char *name; void *f; } ours[] = {
        { "eglGetPlatformDisplayEXT", (void *)eglGetPlatformDisplayEXT },
        { "eglGetPlatformDisplay", (void *)eglGetPlatformDisplay },
        { "eglCreatePlatformWindowSurfaceEXT", (void *)eglCreatePlatformWindowSurfaceEXT },
        { "eglCreatePlatformPixmapSurfaceEXT", (void *)eglCreatePlatformPixmapSurfaceEXT },
        { "eglInitialize", (void *)eglInitialize },
        { "eglQueryString", (void *)eglQueryString },
    };
    for (unsigned i = 0; i < sizeof ours / sizeof *ours; i++)
        if (!strcmp(name, ours[i].name))
            return (__eglMustCastToProperFunctionPointerType)ours[i].f;
    return real_eglGetProcAddress(name);
}
