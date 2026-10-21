/* Computing on the GPU with GLES 2.0: a 3x3 blur of an RGBA image, the round trip through X
 * pixmaps (cached UMP memory): XShmPutImage in, the pixmap as a texture with no upload
 * (EGL_KHR_image_pixmap), a fragment shader into a framebuffer object on another pixmap,
 * XShmGetImage out. An FBO on a pixmap renders 20 times faster than a pixmap surface (10.8 ms
 * against 207 for this blur) and 4 times faster than a pbuffer.
 * The same blur on the CPU for comparison. gpufilter [W H [upload]]: "upload" puts the input
 * in an ordinary texture with glTexImage2D instead. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static XImage *shm_image(Display *x, Visual *v, int W, int H, XShmSegmentInfo *shm)
{
    XImage *img = XShmCreateImage(x, v, 32, ZPixmap, NULL, shm, W, H);
    shm->shmid = shmget(IPC_PRIVATE, img->bytes_per_line * H, IPC_CREAT | 0600);
    shm->shmaddr = img->data = shmat(shm->shmid, NULL, 0);
    shm->readOnly = False;
    XShmAttach(x, shm);
    shmctl(shm->shmid, IPC_RMID, NULL);
    return img;
}

/* the reference: the same 3x3 box blur, per channel */
static void cpu_blur(const uint8_t *in, uint8_t *out, int W, int H)
{
    for (int y = 1; y < H - 1; y++)
        for (int x = 1; x < W - 1; x++)
            for (int c = 0; c < 4; c++) {
                int s = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                        s += in[((y + dy) * W + x + dx) * 4 + c];
                out[(y * W + x) * 4 + c] = s / 9;
            }
}

int main(int argc, char **argv)
{
    int W = argc > 1 ? atoi(argv[1]) : 1280, H = argc > 2 ? atoi(argv[2]) : 720, n = 10;
    int upload = argc > 3 && !strcmp(argv[3], "upload");
    Display *x = XOpenDisplay(NULL);
    XVisualInfo vt = { .depth = 32, .class = TrueColor }, *vi;
    int nv;
    if (!x || !(vi = XGetVisualInfo(x, VisualDepthMask | VisualClassMask, &vt, &nv))) { printf("no 32-bit visual\n"); return 1; }
    Pixmap in = XCreatePixmap(x, DefaultRootWindow(x), W, H, 32), out = XCreatePixmap(x, DefaultRootWindow(x), W, H, 32);
    GC gc = XCreateGC(x, in, 0, NULL);
    XShmSegmentInfo si, so;
    XImage *iin = shm_image(x, vi->visual, W, H, &si), *iout = shm_image(x, vi->visual, W, H, &so);
    for (int i = 0; i < W * H * 4; i++)
        iin->data[i] = (i * 2654435761u) >> 24;  /* noise */

    EGLDisplay d = eglGetDisplay(x);
    EGLint ma, mi, nc;
    eglInitialize(d, &ma, &mi);
    EGLint at[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8,
                    EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
    EGLConfig cfg;
    if (!eglChooseConfig(d, at, &cfg, 1, &nc) || !nc) { printf("no 8888 config\n"); return 1; }
    /* a surface only to make the context current: the output is the FBO */
    EGLint pa[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
    EGLSurface s = eglCreatePbufferSurface(d, cfg, pa);
    EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ca);
    if (s == EGL_NO_SURFACE || !eglMakeCurrent(d, s, s, c)) { printf("surface failed 0x%x\n", eglGetError()); return 1; }

    PFNEGLCREATEIMAGEKHRPROC create_image = (void *)eglGetProcAddress("eglCreateImageKHR");
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_texture = (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    GLuint otex, fbo, tex;
    EGLImageKHR oimage = create_image(d, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR, (EGLClientBuffer)out, NULL);
    glGenTextures(1, &otex);
    glBindTexture(GL_TEXTURE_2D, otex);
    image_texture(GL_TEXTURE_2D, oimage);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, otex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { printf("FBO on the pixmap incomplete\n"); return 1; }

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    if (!upload) {
        EGLImageKHR image = create_image(d, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR, (EGLClientBuffer)in, NULL);
        if (image == EGL_NO_IMAGE_KHR) { printf("EGLImage from the pixmap failed 0x%x\n", eglGetError()); return 1; }
        image_texture(GL_TEXTURE_2D, image);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    /* The 9 texture coordinates come from the vertex shader: on the Mali-400/450 a coordinate
     * computed in the fragment shader makes a dependent read, 3x slower here (102 ms against 30) */
    const char *vs = "attribute vec2 a; uniform vec2 px; varying vec4 v0, v1, v2, v3; varying vec2 v4;"
        "void main() { vec2 t = a * 0.5 + 0.5; v0 = vec4(t - px, t + vec2(0.0, -px.y));"
        " v1 = vec4(t + vec2(px.x, -px.y), t + vec2(-px.x, 0.0)); v2 = vec4(t, t + vec2(px.x, 0.0));"
        " v3 = vec4(t + vec2(-px.x, px.y), t + vec2(0.0, px.y)); v4 = t + px; gl_Position = vec4(a, 0.0, 1.0); }";
    const char *fs = "precision mediump float; varying vec4 v0, v1, v2, v3; varying vec2 v4; uniform sampler2D t;"
        "void main() { gl_FragColor = (texture2D(t, v0.xy) + texture2D(t, v0.zw) + texture2D(t, v1.xy)"
        " + texture2D(t, v1.zw) + texture2D(t, v2.xy) + texture2D(t, v2.zw) + texture2D(t, v3.xy)"
        " + texture2D(t, v3.zw) + texture2D(t, v4)) / 9.0; }";
    GLuint p = glCreateProgram(), sh[2] = { glCreateShader(GL_VERTEX_SHADER), glCreateShader(GL_FRAGMENT_SHADER) };
    glShaderSource(sh[0], 1, &vs, NULL);
    glShaderSource(sh[1], 1, &fs, NULL);
    for (int i = 0; i < 2; i++) { glCompileShader(sh[i]); glAttachShader(p, sh[i]); }
    glBindAttribLocation(p, 0, "a");
    glLinkProgram(p);
    glUseProgram(p);
    glUniform2f(glGetUniformLocation(p, "px"), 1.0f / W, 1.0f / H);
    static const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glEnableVertexAttribArray(0);
    glViewport(0, 0, W, H);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);  /* the driver compiles the shader at the first draw */
    glFinish();
    double put = 0, run = 0, get = 0, wait = 0;
    for (int i = 0; i < n; i++) {
        double t0 = now();
        if (upload) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, iin->data);
            glFinish();
        } else {
            XShmPutImage(x, in, gc, iin, 0, 0, 0, 0, W, H, False);
            XSync(x, False);
        }
        double t1 = now();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glFinish();
        double tf = now();
        eglWaitClient();
        double t2 = now();
        wait += t2 - tf;
        XShmGetImage(x, out, iout, 0, 0, AllPlanes);
        double t3 = now();
        put += t1 - t0, run += t2 - t1, get += t3 - t2;
    }
    uint8_t *ref = calloc(W * H, 4);
    double t0 = now();
    cpu_blur((uint8_t *)iin->data, ref, W, H);
    double cpu = now() - t0;

    /* the GPU rows may come out bottom up: compare both ways, inside the border */
    long diff[2] = { 0, 0 };
    for (int flip = 0; flip < 2; flip++)
        for (int y = 2; y < H - 2; y++)
            for (int xx = 2; xx < W - 2; xx++)
                for (int k = 0; k < 3; k++) {
                    int g = (uint8_t)iout->data[((flip ? H - 1 - y : y) * W + xx) * 4 + k], r = ref[(y * W + xx) * 4 + k];
                    diff[flip] += abs(g - r);
                }
    double md = (diff[0] < diff[1] ? diff[0] : diff[1]) / (double)((W - 4) * (H - 4) * 3);
    printf("%s (eglWaitClient %.1f ms): ", upload ? "glTexImage2D" : "pixmap texture", wait / n * 1000);
    printf("%dx%d RGBA 3x3 blur. GPU: in %.1f ms + shader %.1f ms + out %.1f ms = %.1f ms; CPU: %.1f ms; "
           "mean difference %.2f/255 (%s)\n", W, H, put / n * 1000, run / n * 1000, get / n * 1000,
           (put + run + get) / n * 1000, cpu * 1000, md, diff[0] < diff[1] ? "same rows" : "rows flipped");
    return 0;
}
