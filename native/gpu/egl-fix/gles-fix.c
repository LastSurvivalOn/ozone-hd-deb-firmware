/* The GLES side of libEGL.so.1 (libGLESv2.so.2 is the same library): reading back renders fast.
 *
 * glReadPixels on the Mali r5p0 takes 160 ms for 720p, the driver being the slow part, and it
 * doesn't read GL_BGRA at all. A framebuffer drawing to a texture on an EGLImage of an X pixmap
 * renders fast, and the pixmap is in cached UMP memory, which XShmGetImage reads in ~3 ms.
 *
 * So an RGBA8 renderbuffer of a display that has an X connection gets such a texture, which
 * takes its place wherever it is attached as a colour buffer, and glReadPixels of it reads the
 * pixmap: straight from its UMP memory (DRI2 names the buffer), or failing that with
 * XShmGetImage, which costs the X server 11 ms a 720p frame. (glEGLImageTargetRenderbufferStorageOES would be simpler, but in WebKit's web process
 * it leaves the renderbuffer empty, 0x0, with no error.)
 *
 * WebKitGTK needs it: with shared-memory buffers its web process renders every frame into an
 * RGBA8 renderbuffer and copies it out with glReadPixels (AcceleratedSurface::RenderTargetSHMImage).
 * OZONE_GL_PIXMAP_RENDERBUFFERS=0 turns it off. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/Xlibint.h>
#include <X11/extensions/dri2proto.h>
#include <X11/extensions/dri2tokens.h>
#include <ump/ump.h>
#include <ump/ump_ref_drv.h>

#include "mali-fix.h"

#define REAL(name) \
    static __typeof__(&name) real_##name; \
    if (!real_##name) \
        real_##name = (__typeof__(&name))mali_symbol(#name)

/* small ones aren't worth a pixmap */
#define MIN_SIZE 64

struct target {
    GLuint renderbuffer, texture;
    EGLDisplay egl;
    Display *x;
    Pixmap pixmap;
    EGLImageKHR image;
    int width, height;
    XImage *xi;
    XShmSegmentInfo shm;
    /* the pixmap's UMP memory, mapped here */
    ump_handle ump;
    ump_secure_id ump_id;
    uint8_t *ump_data;
    unsigned pitch;
    int ump_failed;
    struct target *next;
};

static struct target *targets;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static int enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("OZONE_GL_PIXMAP_RENDERBUFFERS");
        on = !e || strcmp(e, "0");
    }
    return on;
}

static void target_free(struct target *t)
{
    PFNEGLDESTROYIMAGEKHRPROC destroy = mali_symbol("eglDestroyImageKHR");
    if (t->texture)
        MALI(glDeleteTextures)(1, &t->texture);
    if (t->ump_data)
        ump_mapped_pointer_release(t->ump);
    if (t->ump)
        ump_reference_release(t->ump);
    if (t->xi) {
        XShmDetach(t->x, &t->shm);
        XDestroyImage(t->xi);
        shmdt(t->shm.shmaddr);
    }
    if (destroy && t->image != EGL_NO_IMAGE_KHR)
        destroy(t->egl, t->image);
    if (t->pixmap)
        XFreePixmap(t->x, t->pixmap);
    XFlush(t->x);
    free(t);
}

/* takes the target of a renderbuffer out of the list; the caller frees it */
static struct target *target_take(GLuint renderbuffer)
{
    for (struct target **p = &targets; *p; p = &(*p)->next)
        if ((*p)->renderbuffer == renderbuffer) {
            struct target *t = *p;
            *p = t->next;
            return t;
        }
    return NULL;
}

/* the target of a renderbuffer, or of the texture standing in for one */
static struct target *target_find(GLuint renderbuffer, GLuint texture)
{
    pthread_mutex_lock(&lock);
    struct target *t = targets;
    while (t && !(renderbuffer ? t->renderbuffer == renderbuffer : t->texture == texture))
        t = t->next;
    pthread_mutex_unlock(&lock);
    return t && t->egl == MALI(eglGetCurrentDisplay)() ? t : NULL;
}

static int x_error(Display *x, XErrorEvent *e)
{
    char text[128];
    XGetErrorText(x, e->error_code, text, sizeof text);
    DEBUG("X error %s: request %d.%d, resource 0x%lx\n", text, e->request_code, e->minor_code, e->resourceid);
    return 0;
}

/* a texture on a new X pixmap's EGLImage, the texture binding left as it was */
static int target_texture(struct target *t)
{
    PFNEGLCREATEIMAGEKHRPROC create_image = mali_symbol("eglCreateImageKHR");
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_texture = mali_symbol("glEGLImageTargetTexture2DOES");
    GLint bound = 0;

    if (!create_image || !image_texture)
        return 0;
    int (*handler)(Display *, XErrorEvent *) = mali_debug() ? XSetErrorHandler(x_error) : NULL;
    t->pixmap = XCreatePixmap(t->x, DefaultRootWindow(t->x), t->width, t->height, 32);
    XSync(t->x, False);
    t->image = create_image(t->egl, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR, (EGLClientBuffer)t->pixmap, NULL);
    if (mali_debug()) {
        XSync(t->x, False);
        XSetErrorHandler(handler);
    }
    if (t->image == EGL_NO_IMAGE_KHR)
        return 0;
    while (MALI(glGetError)() != GL_NO_ERROR)
        ;
    MALI(glGetIntegerv)(GL_TEXTURE_BINDING_2D, &bound);
    MALI(glGenTextures)(1, &t->texture);
    MALI(glBindTexture)(GL_TEXTURE_2D, t->texture);
    image_texture(GL_TEXTURE_2D, t->image);
    /* no mipmaps */
    MALI(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    MALI(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    MALI(glBindTexture)(GL_TEXTURE_2D, bound);
    if (mali_debug()) {
        GLint fbo = 0;
        GLuint probe;
        MALI(glGetIntegerv)(GL_FRAMEBUFFER_BINDING, &fbo);
        MALI(glGenFramebuffers)(1, &probe);
        MALI(glBindFramebuffer)(GL_FRAMEBUFFER, probe);
        MALI(glFramebufferTexture2D)(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->texture, 0);
        DEBUG("pixmap texture alone: status 0x%x, image %p, egl error 0x%x\n",
              MALI(glCheckFramebufferStatus)(GL_FRAMEBUFFER), t->image, MALI(eglGetError)());
        MALI(glBindFramebuffer)(GL_FRAMEBUFFER, fbo);
        MALI(glDeleteFramebuffers)(1, &probe);
    }
    return MALI(glGetError)() == GL_NO_ERROR;
}

void glRenderbufferStorage(GLenum target, GLenum format, GLsizei width, GLsizei height)
{
    REAL(glRenderbufferStorage);
    EGLDisplay egl = MALI(eglGetCurrentDisplay)();
    Display *x = mali_x_display(egl);
    GLint renderbuffer = 0;

    MALI(glGetIntegerv)(GL_RENDERBUFFER_BINDING, &renderbuffer);
    pthread_mutex_lock(&lock);
    struct target *old = target_take(renderbuffer);
    pthread_mutex_unlock(&lock);
    if (old)
        target_free(old);

    /* the renderbuffer gets its own storage all the same, for whoever asks about it */
    real_glRenderbufferStorage(target, format, width, height);
    if (!enabled() || target != GL_RENDERBUFFER || format != GL_RGBA8_OES || !x || !renderbuffer ||
        width < MIN_SIZE || height < MIN_SIZE)
        return;

    struct target *t = calloc(1, sizeof *t);
    t->renderbuffer = renderbuffer, t->egl = egl, t->x = x, t->width = width, t->height = height;
    if (!target_texture(t)) {
        DEBUG("renderbuffer %d %dx%d: no pixmap texture (image %p)\n", renderbuffer, width, height, t->image);
        target_free(t);
        return;
    }
    pthread_mutex_lock(&lock);
    t->next = targets, targets = t;
    pthread_mutex_unlock(&lock);
    DEBUG("renderbuffer %d %dx%d: texture %u on pixmap 0x%lx\n", renderbuffer, width, height, t->texture, t->pixmap);
}

void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer)
{
    REAL(glFramebufferRenderbuffer);
    struct target *t = attachment == GL_COLOR_ATTACHMENT0 && renderbuffer ? target_find(renderbuffer, 0) : NULL;
    if (t)
        MALI(glFramebufferTexture2D)(target, attachment, GL_TEXTURE_2D, t->texture, 0);
    else
        real_glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer);
    if (mali_debug() && t)
        DEBUG("colour of renderbuffer %u: texture %u, status 0x%x\n", renderbuffer, t->texture,
              MALI(glCheckFramebufferStatus)(target));
}

void glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers)
{
    REAL(glDeleteRenderbuffers);
    for (GLsizei i = 0; i < n; i++) {
        pthread_mutex_lock(&lock);
        struct target *t = target_take(renderbuffers[i]);
        pthread_mutex_unlock(&lock);
        if (t)
            target_free(t);
    }
    real_glDeleteRenderbuffers(n, renderbuffers);
}

/* the target the bound framebuffer draws to, if it is one of ours */
static struct target *bound_target(void)
{
    GLint type = 0, name = 0;
    MALI(glGetFramebufferAttachmentParameteriv)(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
    if (type != GL_TEXTURE)
        return NULL;
    MALI(glGetFramebufferAttachmentParameteriv)(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
    return name ? target_find(0, name) : NULL;
}

/* The UMP buffer behind the pixmap, which DRI2 names (as it does for libMali.so on this same
 * connection, so the client is authenticated) */
static int ump_map(struct target *t)
{
    Display *dpy = t->x;  /* the name Xlibint's request macros use */
    XExtCodes *dri2 = XInitExtension(dpy, DRI2_NAME);
    xDRI2CreateDrawableReq *create;
    xDRI2GetBuffersReq *get;
    xDRI2GetBuffersReply rep;
    xDRI2Buffer buffer;
    int ok = 0;

    if (!dri2 || ump_open() != UMP_OK)
        return 0;
    LockDisplay(dpy);
    GetReq(DRI2CreateDrawable, create);
    create->reqType = dri2->major_opcode;
    create->dri2ReqType = X_DRI2CreateDrawable;
    create->drawable = t->pixmap;
    GetReqExtra(DRI2GetBuffers, 4, get);
    get->reqType = dri2->major_opcode;
    get->dri2ReqType = X_DRI2GetBuffers;
    get->drawable = t->pixmap;
    get->count = 1;
    ((CARD32 *)&get[1])[0] = DRI2BufferFrontLeft;
    if (_XReply(dpy, (xReply *)&rep, 0, xFalse)) {
        for (CARD32 i = 0; i < rep.count; i++) {
            _XRead(dpy, (char *)&buffer, sizeof buffer);
            if (i == 0 && buffer.attachment == DRI2BufferFrontLeft && buffer.cpp == 4 && !buffer.flags) {
                t->ump_id = buffer.name, t->pitch = buffer.pitch;
                ok = 1;
            }
        }
    }
    UnlockDisplay(dpy);
    SyncHandle();
    if (!ok)
        return 0;
    t->ump = ump_handle_create_from_secure_id(t->ump_id);
    if (t->ump == UMP_INVALID_MEMORY_HANDLE || ump_size_get(t->ump) < (unsigned long)t->pitch * t->height) {
        t->ump = UMP_INVALID_MEMORY_HANDLE;
        return 0;
    }
    t->ump_data = ump_mapped_pointer_get(t->ump);
    DEBUG("pixmap 0x%lx: UMP 0x%x, pitch %u, mapped at %p\n", t->pixmap, t->ump_id, t->pitch, (void *)t->ump_data);
    return t->ump_data != NULL;
}

static int shm_image(struct target *t)
{
    XVisualInfo vi;
    if (!XMatchVisualInfo(t->x, DefaultScreen(t->x), 32, TrueColor, &vi))
        return 0;
    t->xi = XShmCreateImage(t->x, vi.visual, 32, ZPixmap, NULL, &t->shm, t->width, t->height);
    if (!t->xi)
        return 0;
    t->shm.shmid = shmget(IPC_PRIVATE, t->xi->bytes_per_line * t->height, IPC_CREAT | 0600);
    t->shm.shmaddr = t->xi->data = shmat(t->shm.shmid, NULL, 0);
    t->shm.readOnly = False;
    if (t->shm.shmaddr == (void *)-1 || !XShmAttach(t->x, &t->shm)) {
        XDestroyImage(t->xi);
        t->xi = NULL;
        return 0;
    }
    XSync(t->x, False);
    shmctl(t->shm.shmid, IPC_RMID, NULL);
    return 1;
}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels)
{
    REAL(glReadPixels);
    struct target *t = enabled() ? bound_target() : NULL;
    GLint align = 4;

    MALI(glGetIntegerv)(GL_PACK_ALIGNMENT, &align);
    if (t && !t->ump_data && !t->ump_failed && !ump_map(t))
        t->ump_failed = 1;
    if (!t || type != GL_UNSIGNED_BYTE || (format != GL_BGRA_EXT && format != GL_RGBA) ||
        x < 0 || y < 0 || x + width > t->width || y + height > t->height ||
        (!t->ump_data && !t->xi && !shm_image(t))) {
        DEBUG("glReadPixels %dx%d 0x%x by the driver (target %p)\n", width, height, format, (void *)t);
        real_glReadPixels(x, y, width, height, format, type, pixels);
        return;
    }
    double t0 = mali_debug() ? now() : 0;
    MALI(glFinish)();
    double t1 = mali_debug() ? now() : 0;
    const uint8_t *data;
    size_t pitch;
    if (t->ump_data) {
        /* the CPU's cache dropped where the GPU wrote */
        ump_cache_operations_control(UMP_CACHE_OP_START);
        ump_switch_hw_usage_secure_id(t->ump_id, UMP_USED_BY_CPU);
        ump_cache_operations_control(UMP_CACHE_OP_FINISH);
        data = t->ump_data, pitch = t->pitch;
    } else {
        XShmGetImage(t->x, t->pixmap, t->xi, 0, 0, AllPlanes);
        data = (const uint8_t *)t->xi->data, pitch = t->xi->bytes_per_line;
    }
    double t2 = mali_debug() ? now() : 0;

    /* GL rows go up from y and the pixmap's rows are in the same order (an EGLImage texture keeps
     * its first row at t = 0): ARGB32 in memory is B, G, R, A, which is GL_BGRA */
    size_t stride = ((size_t)width * 4 + align - 1) / align * align;
    for (GLsizei r = 0; r < height; r++) {
        const uint8_t *src = data + (size_t)(y + r) * pitch + x * 4;
        uint8_t *dst = (uint8_t *)pixels + r * stride;
        if (format == GL_BGRA_EXT)
            memcpy(dst, src, width * 4);
        else
            for (GLsizei i = 0; i < width; i++, src += 4, dst += 4)
                dst[0] = src[2], dst[1] = src[1], dst[2] = src[0], dst[3] = src[3];
    }
    /* back to the GPU, so the next read drops the cache again */
    if (t->ump_data)
        ump_switch_hw_usage_secure_id(t->ump_id, UMP_USED_BY_MALI);
    if (mali_debug()) {
        static double finish, fetch, copy, since;
        static int frames;
        double t3 = now();
        finish += t1 - t0, fetch += t2 - t1, copy += t3 - t2, frames++;
        if (t3 - since > 2) {
            DEBUG("readback, per frame of %.1f a second: glFinish %.1f ms, fetch %.1f ms, copy %.1f ms\n",
                  frames / (t3 - since), finish / frames * 1000, fetch / frames * 1000, copy / frames * 1000);
            finish = fetch = copy = 0, frames = 0, since = t3;
        }
    }
}

/* only to tell why a framebuffer is incomplete (OZONE_GL_DEBUG) */
GLenum glCheckFramebufferStatus(GLenum target)
{
    REAL(glCheckFramebufferStatus);
    GLenum status = real_glCheckFramebufferStatus(target);
    if (status != GL_FRAMEBUFFER_COMPLETE && mali_debug()) {
        static const GLenum points[] = { GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT };
        GLint fbo = 0;
        MALI(glGetIntegerv)(GL_FRAMEBUFFER_BINDING, &fbo);
        DEBUG("framebuffer %d incomplete: 0x%x, context %p\n", fbo, status, (void *)MALI(eglGetCurrentContext)());
        for (int i = 0; i < 3; i++) {
            GLint type = 0, name = 0;
            MALI(glGetFramebufferAttachmentParameteriv)(target, points[i], GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
            MALI(glGetFramebufferAttachmentParameteriv)(target, points[i], GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
            DEBUG("  attachment 0x%x: type 0x%x name %d\n", points[i], type, name);
        }
    }
    return status;
}
