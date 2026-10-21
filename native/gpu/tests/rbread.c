/* What WebKitGTK's web process does with shared-memory buffers, on a surfaceless display: an RGBA8
 * renderbuffer in an FBO, rendering, glReadPixels(GL_BGRA). Checks that libEGL.so.1's pixmap
 * renderbuffers give the driver's own result (OZONE_GL_PIXMAP_RENDERBUFFERS=0) faster.
 * rbread [W H] */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Mali's headers are EGL 1.4 */
typedef EGLDisplay (*get_platform_display_t)(EGLenum, void *, const EGLint *);

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
    int W = argc > 1 ? atoi(argv[1]) : 1280, H = argc > 2 ? atoi(argv[2]) : 680, n = 10;
    get_platform_display_t get_display = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
    const char *client = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (!client || !strstr(client, "EGL_MESA_platform_surfaceless") || !get_display) { printf("no surfaceless platform\n"); return 1; }
    EGLDisplay d = get_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    EGLint ma, mi, nc;
    if (!eglInitialize(d, &ma, &mi)) { printf("eglInitialize failed\n"); return 1; }
    if (!strstr(eglQueryString(d, EGL_EXTENSIONS), "EGL_KHR_surfaceless_context")) { printf("no surfaceless context\n"); return 1; }
    EGLint at[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8,
                    EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
    EGLConfig cfg;
    eglChooseConfig(d, at, &cfg, 1, &nc);
    EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ca);
    if (!eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c)) { printf("surfaceless make current failed 0x%x\n", eglGetError()); return 1; }

    /* in WebKit's order: depth and stencil first, then the colour (RenderTargetShareableBuffer) */
    GLuint fbo, rb, ds;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenRenderbuffers(1, &ds);
    glBindRenderbuffer(GL_RENDERBUFFER, ds);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, W, H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, ds);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, ds);
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8_OES, W, H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) { printf("FBO incomplete: 0x%x\n", status); return 1; }
    glViewport(0, 0, W, H);

    unsigned char *px = malloc(W * H * 4);
    double t = 0;
    for (int i = 0; i < n; i++) {
        glDisable(GL_SCISSOR_TEST);
        glClearColor(1, 0, 0, 1);  /* red: the top half */
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, W, H / 2);
        glClearColor(0, 1, 0, 1);  /* green: GL's bottom half, its first rows */
        glClear(GL_COLOR_BUFFER_BIT);
        double t0 = now();
        glReadPixels(0, 0, W, H, GL_BGRA_EXT, GL_UNSIGNED_BYTE, px);
        t += now() - t0;
    }
    unsigned char *first = px, *last = px + (size_t)(H - 1) * W * 4;
    printf("%dx%d: glReadPixels %.1f ms; first row B,G,R,A %d,%d,%d,%d (green expected), last %d,%d,%d,%d (red), err 0x%x\n",
           W, H, t / n * 1000, first[0], first[1], first[2], first[3], last[0], last[1], last[2], last[3], glGetError());
    return 0;
}
