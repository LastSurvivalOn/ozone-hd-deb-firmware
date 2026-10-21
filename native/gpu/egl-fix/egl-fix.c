/* libEGL.so.1 for the Mali r5p0 X11 driver (EGL 1.4): libMali.so with fixes for what today's
 * programs trip over. Everything not here comes from libMali.so, a dependency:
 *   gcc -O2 -shared -fPIC -Wl,-soname,libEGL.so.1 egl-fix.c gles-fix.c -o libEGL.so.1 \
 *       -Wl,-Bsymbolic -L<mali> -Wl,--no-as-needed -lMali -Wl,--as-needed -lUMP -lX11 -lXext -ldl -lpthread
 * libGLESv2.so.2 is the same library (gles-fix.c). Being these libraries itself, it fixes
 * programs that dlopen them and dlsym() from the handle (SDL2, libepoxy) as well as those linked
 * to them (mpv).
 *
 * - eglInitialize() fails when major or minor is NULL, which the spec allows (SDL2, mpv).
 * - No EGL_EXT_platform_base: mpv and others get their X11 display only through
 *   eglGetPlatformDisplay(EXT). Added on top of eglGetDisplay() for X11, the
 *   only platform the driver has.
 * - eglQueryString(EGL_NO_DISPLAY, ...) returns NULL; it now lists the client
 *   extensions above.
 * - eglGetProcAddress() returns NULL for core GL functions. Programs then take the first
 *   glGetString etc. in the process, which is glvnd's libGL.so.1 if they link it too (mpv):
 *   a stub that does nothing without a GLX context. And for extensions it returns what its own
 *   relocations found first in the process, the same stubs (glEGLImageTargetTexture2DOES,
 *   glBindVertexArrayOES... in WebKit). Everything now comes from libMali.so itself
 *   (EGL_KHR_get_all_proc_addresses).
 * - The RGB565 configs name a DirectColor visual as their native one: the screen's TrueColor
 *   one instead.
 * - On a 16-bit screen, window configs asking for up to 8 bits a colour get the RGB565 ones
 *   first: an 8888 config takes a 32-bit visual, and X converts every frame of such a window
 *   to the screen's format on the CPU (mpv).
 * - No surfaceless platform or context, which WebKitGTK's web process needs: the surfaceless
 *   display is an X connection of its own (EGL_MESA_platform_surfaceless), and a context made
 *   current with no surface gets a hidden 1x1 pbuffer (EGL_KHR_surfaceless_context). */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "mali-fix.h"

#ifndef EGL_PLATFORM_X11_KHR
#define EGL_PLATFORM_X11_KHR 0x31D5
#endif
#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

int mali_debug(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("OZONE_GL_DEBUG");
        on = e && strcmp(e, "0");
    }
    return on;
}

void *mali_symbol(const char *name)
{
    static void *lib;
    if (!lib)
        lib = dlopen("libMali.so", RTLD_NOW | RTLD_NOLOAD);
    return lib ? dlsym(lib, name) : NULL;
}

#define REAL(name) \
    static __typeof__(&name) real_##name; \
    if (!real_##name) \
        real_##name = (__typeof__(&name))mali_symbol(#name)

static const char client_extensions[] =
    "EGL_EXT_client_extensions EGL_EXT_platform_base EGL_EXT_platform_x11 EGL_KHR_platform_x11 "
    "EGL_MESA_platform_surfaceless EGL_KHR_client_get_all_proc_addresses";

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
    REAL(eglInitialize);
    EGLint ma, mi;
    EGLBoolean r = real_eglInitialize(dpy, major ? major : &ma, minor ? minor : &mi);
    DEBUG("eglInitialize(%p) = %d\n", dpy, r);
    return r;
}

EGLBoolean eglTerminate(EGLDisplay dpy)
{
    REAL(eglTerminate);
    DEBUG("eglTerminate(%p)\n", dpy);
    return real_eglTerminate(dpy);
}

/* the same for every display of the one driver */
static char display_extensions[2048];
static pthread_mutex_t extensions_lock = PTHREAD_MUTEX_INITIALIZER;

const char *eglQueryString(EGLDisplay dpy, EGLint name)
{
    REAL(eglQueryString);
    if (dpy == EGL_NO_DISPLAY && name == EGL_EXTENSIONS)
        return client_extensions;
    if (dpy == EGL_NO_DISPLAY && name == EGL_VERSION)
        return "1.4";
    if (name == EGL_EXTENSIONS) {
        const char *real = real_eglQueryString(dpy, name);
        pthread_mutex_lock(&extensions_lock);
        if (real && !display_extensions[0])
            snprintf(display_extensions, sizeof display_extensions, "%s EGL_KHR_surfaceless_context", real);
        pthread_mutex_unlock(&extensions_lock);
        return real ? display_extensions : NULL;
    }
    return real_eglQueryString(dpy, name);
}

/* The X display behind each EGL display, for the visuals and the pixmaps */
static struct { EGLDisplay egl; Display *x; } displays[8];
static pthread_mutex_t displays_lock = PTHREAD_MUTEX_INITIALIZER;

EGLDisplay eglGetDisplay(EGLNativeDisplayType native)
{
    REAL(eglGetDisplay);
    EGLDisplay d = real_eglGetDisplay(native);
    pthread_mutex_lock(&displays_lock);
    for (unsigned i = 0; d != EGL_NO_DISPLAY && native != EGL_DEFAULT_DISPLAY && i < 8; i++)
        if (!displays[i].egl || displays[i].egl == d) {
            displays[i].egl = d, displays[i].x = (Display *)native;
            break;
        }
    pthread_mutex_unlock(&displays_lock);
    return d;
}

Display *mali_x_display(EGLDisplay d)
{
    Display *x = NULL;
    pthread_mutex_lock(&displays_lock);
    for (unsigned i = 0; i < 8 && displays[i].egl; i++)
        if (displays[i].egl == d)
            x = displays[i].x;
    pthread_mutex_unlock(&displays_lock);
    return x;
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
    Display *x = mali_x_display(dpy);
    if (r && x && attribute == EGL_NATIVE_VISUAL_ID)
        *value = truecolor(x, *value);
    else if (r && x && attribute == EGL_NATIVE_VISUAL_TYPE)
        *value = TrueColor;
    return r;
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attribs, EGLConfig *configs, EGLint size, EGLint *num)
{
    REAL(eglChooseConfig);
    Display *x = mali_x_display(dpy);
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
    DEBUG("eglGetPlatformDisplay(0x%x, %p)\n", platform, native);
    if (platform == EGL_PLATFORM_SURFACELESS_MESA) {
        /* an X connection of our own: the driver has no other platform, and the pixmaps of
         * gles-fix.c need one (libX11 1.8 is thread-safe by default; EGL runs on several threads) */
        static Display *own;
        static pthread_mutex_t own_lock = PTHREAD_MUTEX_INITIALIZER;
        pthread_mutex_lock(&own_lock);
        if (!own) {
            XInitThreads();
            own = XOpenDisplay(NULL);
        }
        pthread_mutex_unlock(&own_lock);
        DEBUG("surfaceless display: X connection %p\n", (void *)own);
        return own ? eglGetDisplay((EGLNativeDisplayType)own) : EGL_NO_DISPLAY;
    }
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

/* EGL_KHR_surfaceless_context: a hidden 1x1 pbuffer for each context (a surface can't be current
 * to two contexts on different threads), current while the context has no surface */
#define DUMMIES 64
static struct { EGLDisplay egl; EGLContext ctx; EGLSurface pbuffer; } dummies[DUMMIES];
static pthread_mutex_t dummies_lock = PTHREAD_MUTEX_INITIALIZER;
static __thread int dummy_current;

static EGLSurface dummy_pbuffer(EGLDisplay dpy, EGLContext ctx)
{
    EGLint id = 0, n = 0;
    EGLConfig config;
    EGLSurface s = EGL_NO_SURFACE;

    if (!MALI(eglQueryContext)(dpy, ctx, EGL_CONFIG_ID, &id))
        return EGL_NO_SURFACE;
    pthread_mutex_lock(&dummies_lock);
    for (unsigned i = 0; i < DUMMIES; i++)
        if (dummies[i].egl == dpy && dummies[i].ctx == ctx)
            s = dummies[i].pbuffer;
    if (s == EGL_NO_SURFACE) {
        EGLint attribs[] = { EGL_CONFIG_ID, id, EGL_NONE }, size[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
        REAL(eglChooseConfig);
        if (real_eglChooseConfig(dpy, attribs, &config, 1, &n) && n)
            s = MALI(eglCreatePbufferSurface)(dpy, config, size);
        for (unsigned i = 0; s != EGL_NO_SURFACE && i < DUMMIES; i++)
            if (!dummies[i].egl) {
                dummies[i].egl = dpy, dummies[i].ctx = ctx, dummies[i].pbuffer = s;
                break;
            }
    }
    pthread_mutex_unlock(&dummies_lock);
    return s;
}

EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx)
{
    REAL(eglDestroyContext);
    EGLSurface s = EGL_NO_SURFACE;
    pthread_mutex_lock(&dummies_lock);
    for (unsigned i = 0; i < DUMMIES; i++)
        if (dummies[i].egl == dpy && dummies[i].ctx == ctx) {
            s = dummies[i].pbuffer;
            dummies[i].egl = NULL, dummies[i].ctx = NULL;
        }
    pthread_mutex_unlock(&dummies_lock);
    EGLBoolean r = real_eglDestroyContext(dpy, ctx);
    if (s != EGL_NO_SURFACE)
        MALI(eglDestroySurface)(dpy, s);
    return r;
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx)
{
    REAL(eglMakeCurrent);
    if (ctx != EGL_NO_CONTEXT && draw == EGL_NO_SURFACE && read == EGL_NO_SURFACE) {
        EGLSurface s = dummy_pbuffer(dpy, ctx);
        if (s != EGL_NO_SURFACE && real_eglMakeCurrent(dpy, s, s, ctx)) {
            dummy_current = 1;
            return EGL_TRUE;
        }
        DEBUG("surfaceless eglMakeCurrent(%p) failed: pbuffer %p, error 0x%x\n", ctx, s, MALI(eglGetError)());
    }
    dummy_current = 0;
    return real_eglMakeCurrent(dpy, draw, read, ctx);
}

EGLSurface eglGetCurrentSurface(EGLint which)
{
    REAL(eglGetCurrentSurface);
    return dummy_current ? EGL_NO_SURFACE : real_eglGetCurrentSurface(which);
}

/* The platform variants take a pointer to the X11 Window or Pixmap */
EGLSurface eglCreatePlatformWindowSurfaceEXT(EGLDisplay dpy, EGLConfig config, void *native, const EGLint *attribs)
{
    return MALI(eglCreateWindowSurface)(dpy, config, *(Window *)native, attribs);
}

EGLSurface eglCreatePlatformPixmapSurfaceEXT(EGLDisplay dpy, EGLConfig config, void *native, const EGLint *attribs)
{
    return MALI(eglCreatePixmapSurface)(dpy, config, *(Pixmap *)native, attribs);
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
        { "eglTerminate", (void *)eglTerminate },
        { "eglQueryString", (void *)eglQueryString },
        { "eglGetDisplay", (void *)eglGetDisplay },
        { "eglGetConfigAttrib", (void *)eglGetConfigAttrib },
        { "eglChooseConfig", (void *)eglChooseConfig },
        { "eglMakeCurrent", (void *)eglMakeCurrent },
        { "eglGetCurrentSurface", (void *)eglGetCurrentSurface },
        { "eglDestroyContext", (void *)eglDestroyContext },
        /* gles-fix.c */
        { "glRenderbufferStorage", (void *)glRenderbufferStorage },
        { "glDeleteRenderbuffers", (void *)glDeleteRenderbuffers },
        { "glReadPixels", (void *)glReadPixels },
        { "glCheckFramebufferStatus", (void *)glCheckFramebufferStatus },
        { "glFramebufferRenderbuffer", (void *)glFramebufferRenderbuffer },
    };
    for (unsigned i = 0; i < sizeof ours / sizeof *ours; i++)
        if (!strcmp(name, ours[i].name))
            return (__eglMustCastToProperFunctionPointerType)ours[i].f;
    /* libMali.so's own definition first: dlsym on its handle doesn't see the process's others */
    void *f = mali_symbol(name);
    return f ? (__eglMustCastToProperFunctionPointerType)f : real_eglGetProcAddress(name);
}
