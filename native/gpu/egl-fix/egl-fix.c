/* libEGL.so.1 for the Mali r5p0 X11 driver (EGL 1.4): libMali.so with fixes for what today's
 * programs trip over. Everything not here comes from libMali.so, a dependency:
 *   gcc -O2 -shared -fPIC -Wl,-soname,libEGL.so.1 egl-fix.c -o libEGL.so.1 -L<mali> -lMali -ldl
 * Being libEGL.so.1 itself, it fixes programs that dlopen EGL and dlsym() from the handle
 * (SDL2, libepoxy) as well as those linked to it (mpv).
 *
 * - eglInitialize() fails when major or minor is NULL, which the spec allows (SDL2, mpv).
 * - No EGL_EXT_platform_base: mpv and others get their X11 display only through
 *   eglGetPlatformDisplay(EXT). Added on top of eglGetDisplay() for X11, the
 *   only platform the driver has.
 * - eglQueryString(EGL_NO_DISPLAY, ...) returns NULL; it now lists the client
 *   extensions above.
 * - eglGetProcAddress() returns NULL for core GL functions. Programs then take the first
 *   glGetString etc. in the process, which is glvnd's libGL.so.1 if they link it too (mpv):
 *   a stub that does nothing without a GLX context. Core functions now come from libMali.so
 *   (EGL_KHR_get_all_proc_addresses).
 * - The RGB565 configs name a DirectColor visual as their native one: the screen's TrueColor
 *   one instead.
 * - On a 16-bit screen, window configs asking for up to 8 bits a colour get the RGB565 ones
 *   first: an 8888 config takes a 32-bit visual, and X converts every frame of such a window
 *   to the screen's format on the CPU (mpv). */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <string.h>
#include <EGL/egl.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#ifndef EGL_PLATFORM_X11_KHR
#define EGL_PLATFORM_X11_KHR 0x31D5
#endif

static void *mali(const char *name)
{
    static void *lib;
    if (!lib)
        lib = dlopen("libMali.so", RTLD_NOW | RTLD_NOLOAD);
    return lib ? dlsym(lib, name) : NULL;
}

#define REAL(name) \
    static __typeof__(&name) real_##name; \
    if (!real_##name) \
        real_##name = (__typeof__(&name))mali(#name)

static const char client_extensions[] =
    "EGL_EXT_client_extensions EGL_EXT_platform_base EGL_EXT_platform_x11 EGL_KHR_platform_x11 "
    "EGL_KHR_client_get_all_proc_addresses";

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

/* The X display behind each EGL display, for the visuals */
static struct { EGLDisplay egl; Display *x; } displays[8];

EGLDisplay eglGetDisplay(EGLNativeDisplayType native)
{
    REAL(eglGetDisplay);
    EGLDisplay d = real_eglGetDisplay(native);
    for (unsigned i = 0; d != EGL_NO_DISPLAY && native != EGL_DEFAULT_DISPLAY && i < 8; i++)
        if (!displays[i].egl || displays[i].egl == d) {
            displays[i].egl = d, displays[i].x = (Display *)native;
            break;
        }
    return d;
}

static Display *x_display(EGLDisplay d)
{
    for (unsigned i = 0; i < 8 && displays[i].egl; i++)
        if (displays[i].egl == d)
            return displays[i].x;
    return NULL;
}

/* A TrueColor visual for the depth of the given one, the screen's default first */
static VisualID truecolor(Display *x, VisualID id)
{
    XVisualInfo t = { .visualid = id }, *v;
    int n;
    if (!(v = XGetVisualInfo(x, VisualIDMask, &t, &n)))
        return id;
    t.depth = v->depth, t.class = TrueColor, t.screen = v->screen;
    int class = v->class;
    XFree(v);
    if (class == TrueColor)
        return id;
    if (DefaultDepth(x, t.screen) == t.depth)
        return XVisualIDFromVisual(DefaultVisual(x, t.screen));
    if (!(v = XGetVisualInfo(x, VisualDepthMask | VisualClassMask | VisualScreenMask, &t, &n)))
        return id;
    id = v->visualid;
    XFree(v);
    return id;
}

EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value)
{
    REAL(eglGetConfigAttrib);
    EGLBoolean r = real_eglGetConfigAttrib(dpy, config, attribute, value);
    Display *x = x_display(dpy);
    if (r && x && attribute == EGL_NATIVE_VISUAL_ID)
        *value = truecolor(x, *value);
    else if (r && x && attribute == EGL_NATIVE_VISUAL_TYPE)
        *value = TrueColor;
    return r;
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attribs, EGLConfig *configs, EGLint size, EGLint *num)
{
    REAL(eglChooseConfig);
    Display *x = x_display(dpy);
    EGLint a[64], n = 0, window = 1, deep = 0;

    if (!x || !attribs || DefaultDepth(x, DefaultScreen(x)) != 16)
        return real_eglChooseConfig(dpy, attribs, configs, size, num);
    for (const EGLint *p = attribs; *p != EGL_NONE && n < 60; p += 2) {
        a[n] = p[0], a[n + 1] = p[1];
        if (p[0] == EGL_SURFACE_TYPE)
            window = p[1] != EGL_DONT_CARE && (p[1] & EGL_WINDOW_BIT);
        if ((p[0] == EGL_RED_SIZE || p[0] == EGL_GREEN_SIZE || p[0] == EGL_BLUE_SIZE) && p[1] > 8)
            deep = 1;
        if (p[0] == EGL_ALPHA_SIZE && p[1] > 0 && p[1] != EGL_DONT_CARE)
            deep = 1;
        if (p[0] == EGL_RED_SIZE || p[0] == EGL_BLUE_SIZE)
            a[n + 1] = p[1] > 5 ? 5 : p[1];
        if (p[0] == EGL_GREEN_SIZE)
            a[n + 1] = p[1] > 6 ? 6 : p[1];
        n += 2;
    }
    a[n] = EGL_NONE;
    if (!window || deep)
        return real_eglChooseConfig(dpy, attribs, configs, size, num);

    /* the 16-bit configs first, the order otherwise kept */
    EGLConfig all[64];
    EGLint got, out = 0;
    if (!real_eglChooseConfig(dpy, a, all, 64, &got))
        return EGL_FALSE;
    for (int pass = 0; pass < 2; pass++)
        for (EGLint i = 0; i < got; i++) {
            EGLint bits;
            eglGetConfigAttrib(dpy, all[i], EGL_BUFFER_SIZE, &bits);
            if ((bits == 16) == (pass == 0)) {
                if (configs && out < size)
                    configs[out] = all[i];
                out++;
            }
        }
    *num = configs && out > size ? size : out;
    return EGL_TRUE;
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
        { "eglGetDisplay", (void *)eglGetDisplay },
        { "eglGetConfigAttrib", (void *)eglGetConfigAttrib },
        { "eglChooseConfig", (void *)eglChooseConfig },
    };
    for (unsigned i = 0; i < sizeof ours / sizeof *ours; i++)
        if (!strcmp(name, ours[i].name))
            return (__eglMustCastToProperFunctionPointerType)ours[i].f;
    __eglMustCastToProperFunctionPointerType f = real_eglGetProcAddress(name);
    return f ? f : (__eglMustCastToProperFunctionPointerType)mali(name);
}
