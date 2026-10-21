/* 2D drawing speed of an X server, the work a browser gives it:
 * a full-window XShmPutImage (how WebKit and GTK paint), XCopyArea within the
 * window (scrolling) and solid fills. x2dbench [W H] */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
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

int main(int argc, char **argv)
{
    int W = argc > 1 ? atoi(argv[1]) : 1280, H = argc > 2 ? atoi(argv[2]) : 680;
    Display *d = XOpenDisplay(NULL);
    if (!d) { printf("no display\n"); return 1; }
    int s = DefaultScreen(d);
    Window w = XCreateSimpleWindow(d, RootWindow(d, s), 0, 0, W, H, 0, 0, 0);
    XMapWindow(d, w);
    GC gc = XCreateGC(d, w, 0, NULL);
    XSync(d, False);

    XShmSegmentInfo shm;
    XImage *img = XShmCreateImage(d, DefaultVisual(d, s), DefaultDepth(d, s), ZPixmap, NULL, &shm, W, H);
    shm.shmid = shmget(IPC_PRIVATE, img->bytes_per_line * H, IPC_CREAT | 0600);
    shm.shmaddr = img->data = shmat(shm.shmid, NULL, 0);
    shm.readOnly = False;
    XShmAttach(d, &shm);
    shmctl(shm.shmid, IPC_RMID, NULL);
    for (int i = 0; i < img->bytes_per_line * H; i++)
        img->data[i] = i * 7;

    int n = 60;
    double t = now();
    for (int i = 0; i < n; i++)
        XShmPutImage(d, w, gc, img, 0, 0, 0, 0, W, H, False);
    XSync(d, False);
    double put = (now() - t) / n;

    t = now();
    for (int i = 0; i < n; i++)
        XCopyArea(d, w, w, gc, 0, 40, W, H - 40, 0, 0);
    XSync(d, False);
    double copy = (now() - t) / n;

    t = now();
    for (int i = 0; i < n; i++) {
        XSetForeground(d, gc, i * 0x1234);
        XFillRectangle(d, w, gc, 0, 0, W, H);
    }
    XSync(d, False);
    double fill = (now() - t) / n;

    printf("%dx%d: ShmPutImage %.1f ms, CopyArea (scroll) %.1f ms, FillRectangle %.1f ms\n",
           W, H, put * 1000, copy * 1000, fill * 1000);
    return 0;
}
