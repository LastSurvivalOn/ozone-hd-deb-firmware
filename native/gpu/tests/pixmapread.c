/* GPU -> CPU without glReadPixels: render into an X pixmap (cached UMP memory in the Mali DDX)
 * and fetch it with XShmGetImage; glReadPixels on the same surface for comparison.
 * pixmapread [W H] */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
    int W = argc > 1 ? atoi(argv[1]) : 1280, H = argc > 2 ? atoi(argv[2]) : 720, n = 20;
    Display *x = XOpenDisplay(NULL);
    if (!x) return 1;
    Pixmap pm = XCreatePixmap(x, DefaultRootWindow(x), W, H, DefaultDepth(x, DefaultScreen(x)));
    XSync(x, False);

    EGLDisplay d = eglGetDisplay(x);
    EGLint ma, mi, nc;
    eglInitialize(d, &ma, &mi);
    EGLint at[] = { EGL_SURFACE_TYPE, EGL_PIXMAP_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                    EGL_RED_SIZE, 5, EGL_GREEN_SIZE, 6, EGL_BLUE_SIZE, 5, EGL_ALPHA_SIZE, 0, EGL_NONE };
    EGLConfig cfg;
    if (!eglChooseConfig(d, at, &cfg, 1, &nc) || !nc) { printf("no pixmap config\n"); return 1; }
    EGLSurface s = eglCreatePixmapSurface(d, cfg, pm, NULL);
    if (s == EGL_NO_SURFACE) { printf("pixmap surface failed 0x%x\n", eglGetError()); return 1; }
    EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ca);
    eglMakeCurrent(d, s, s, c);
    glViewport(0, 0, W, H);

    XShmSegmentInfo shm;
    XImage *img = XShmCreateImage(x, DefaultVisual(x, 0), DefaultDepth(x, 0), ZPixmap, NULL, &shm, W, H);
    shm.shmid = shmget(IPC_PRIVATE, img->bytes_per_line * H, IPC_CREAT | 0600);
    shm.shmaddr = img->data = shmat(shm.shmid, NULL, 0);
    shm.readOnly = False;
    XShmAttach(x, &shm);
    shmctl(shm.shmid, IPC_RMID, NULL);

    double render = 0, fetch = 0;
    int ok = 0;
    for (int i = 0; i < n; i++) {
        double t0 = now();
        glClearColor((i & 1) ? 1 : 0, 0.5f, (i & 1) ? 0 : 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glFinish();
        eglWaitClient();
        double t1 = now();
        XShmGetImage(x, pm, img, 0, 0, AllPlanes);
        double t2 = now();
        render += t1 - t0, fetch += t2 - t1;
        unsigned short p = ((unsigned short *)img->data)[W * (H / 2) + W / 2];
        ok += (i & 1) ? (p >> 11) == 31 : (p & 31) == 31;
    }
    printf("%dx%d: render %.1f ms, XShmGetImage %.1f ms per frame, %d/%d frames correct\n",
           W, H, render / n * 1000, fetch / n * 1000, ok, n);

    static unsigned short buf[1920 * 1080];
    double t0 = now();
    for (int i = 0; i < 5; i++)
        glReadPixels(0, 0, W, H, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, buf);
    printf("glReadPixels from the same surface: %.1f ms per frame\n", (now() - t0) / 5 * 1000);
    return 0;
}
