/* The patched WebKit's render target (native/webkit/patches): the web process makes an X pixmap on
 * an X connection of its own, while the EGL display (the surfaceless one of libEGL.so.1, or here
 * an X one) is another connection. Checks that Mali renders into a texture on an EGLImage of a
 * pixmap made by another connection, and in which row order the frame lands in the pixmap.
 * xpixmap2conn [W H] */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
    int W = argc > 1 ? atoi(argv[1]) : 1280, H = argc > 2 ? atoi(argv[2]) : 680;
    Display *a = XOpenDisplay(NULL), *b = XOpenDisplay(NULL);
    if (!a || !b) { printf("no X\n"); return 1; }
    Pixmap pix = XCreatePixmap(a, DefaultRootWindow(a), W, H, 32);
    XSync(a, False);

    EGLDisplay d = eglGetDisplay(b);
    EGLint ma, mi, nc;
    eglInitialize(d, &ma, &mi);
    EGLint at[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8,
                    EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
    EGLConfig cfg;
    eglChooseConfig(d, at, &cfg, 1, &nc);
    EGLint pa[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
    EGLSurface s = eglCreatePbufferSurface(d, cfg, pa);
    EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ca);
    if (!eglMakeCurrent(d, s, s, c)) { printf("make current failed 0x%x\n", eglGetError()); return 1; }

    PFNEGLCREATEIMAGEKHRPROC create_image = (void *)eglGetProcAddress("eglCreateImageKHR");
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_texture = (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    EGLint ia[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
    EGLImageKHR img = create_image(d, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR, (EGLClientBuffer)pix, ia);
    if (img == EGL_NO_IMAGE_KHR) { printf("EGLImage of the other connection's pixmap failed 0x%x\n", eglGetError()); return 1; }
    GLuint tex, fbo;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    image_texture(GL_TEXTURE_2D, img);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) { printf("FBO incomplete 0x%x\n", st); return 1; }
    glViewport(0, 0, W, H);

    double t = 0;
    for (int i = 0; i < 10; i++) {
        double t0 = now();
        glDisable(GL_SCISSOR_TEST);
        glClearColor(1, 0, 0, 1);    /* red everywhere */
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, W, H / 4);   /* green: GL's bottom quarter, y = 0 */
        glClearColor(0, 1, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glFinish();
        t += now() - t0;
    }
    /* what X has: row 0 is the top of the pixmap */
    XImage *top = XGetImage(a, pix, 0, 0, 1, 1, AllPlanes, ZPixmap), *bottom = XGetImage(a, pix, 0, H - 1, 1, 1, AllPlanes, ZPixmap);
    unsigned long pt = XGetPixel(top, 0, 0), pb = XGetPixel(bottom, 0, 0);
    printf("%dx%d pixmap of connection A rendered through connection B: %.1f ms a frame; "
           "X row 0 = %06lx, last row = %06lx (GL's y = 0 is green 00ff00: %s)\n", W, H, t / 10 * 1000, pt & 0xffffff, pb & 0xffffff,
           (pt & 0xffffff) == 0x00ff00 ? "row 0, so FlipY renders top down" : (pb & 0xffffff) == 0x00ff00 ? "the last row" : "neither?");
    return 0;
}
