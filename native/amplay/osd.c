/*
 * On-screen controls for amplay.
 *
 * The video layer sits under the desktop OSD layer (fb0, where X draws), and fb0 has a hardware
 * color key: pixels of exactly that color are transparent. So the player is an ordinary X window
 * filled with the key color, and the bars are child windows on top of it. X redraws only the bars,
 * about once a second while they are visible, and nothing while they are hidden; the video itself
 * never passes through the CPU.
 *
 * Text is anti-aliased only over the opaque bars: an edge blended with the key color would no
 * longer match it and show up as a magenta fringe.
 */
#include "osd.h"

#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/Xft/Xft.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define KEY_COLOR   "#ff00ff"   /* not in the Catppuccin palette used everywhere else */
#define BAR_H       64
#define TOP_H       44
#define PAD         24
#define TRACK_Y     14          /* progress track inside the bottom bar */
#define TRACK_H     6
#define HIDE_AFTER  3000        /* ms without input */

/* Font Awesome 4 */
#define ICON_PLAY   "\xef\x81\x8b"   /* f04b */
#define ICON_PAUSE  "\xef\x81\x8c"   /* f04c */
#define ICON_BACK   "\xef\x81\x8a"   /* f04a */
#define ICON_FWD    "\xef\x81\x8e"   /* f04e */
#define ICON_VOL    "\xef\x80\xa8"   /* f028 */
#define ICON_MUTE   "\xef\x80\xa6"   /* f026 */
#define ICON_CLOSE  "\xef\x80\x8d"   /* f00d */

enum { C_KEY, C_BG, C_TRACK, C_FILL, C_TEXT, C_SUB, NCOLORS };
static const char *COLOR_NAMES[NCOLORS] = { KEY_COLOR, "#11111b", "#45475a", "#cba6f7", "#cdd6f4", "#a6adc8" };

/* clickable areas in the bottom bar: x from, x to */
enum { HIT_BACK, HIT_PLAY, HIT_FWD, HIT_VOL, NHITS };

static struct {
    Display *dpy;
    int scr, w, h;
    Window win, top, bar;
    Pixmap top_pm, bar_pm;
    XftDraw *top_d, *bar_d;
    XftFont *font, *icons;
    XftColor col[NCOLORS];
    Cursor arrow, blank;
    int visible, grabbed;
    int64_t last_input;
    char held[256];            /* keys down, to tell autorepeat from a new press */
    int hit[NHITS][2];
    struct osd_state shown;    /* what the bars show now */
    double dur;
    char sig[256];
} O;

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

static void show(void)
{
    O.last_input = now_ms();
    if (O.visible)
        return;
    XMapRaised(O.dpy, O.top);
    XMapRaised(O.dpy, O.bar);
    XDefineCursor(O.dpy, O.win, O.arrow);
    O.visible = 1;
    O.sig[0] = 0;   /* force a redraw */
}

static void hide(void)
{
    if (!O.visible)
        return;
    XUnmapWindow(O.dpy, O.top);
    XUnmapWindow(O.dpy, O.bar);
    XDefineCursor(O.dpy, O.win, O.blank);
    O.visible = 0;
}

int osd_open(void)
{
    if (!(O.dpy = XOpenDisplay(NULL)))
        return -1;
    O.scr = DefaultScreen(O.dpy);
    O.w = DisplayWidth(O.dpy, O.scr);
    O.h = DisplayHeight(O.dpy, O.scr);
    Visual *vis = DefaultVisual(O.dpy, O.scr);
    Colormap cmap = DefaultColormap(O.dpy, O.scr);
    for (int i = 0; i < NCOLORS; i++)
        XftColorAllocName(O.dpy, vis, cmap, COLOR_NAMES[i], &O.col[i]);
    O.font = XftFontOpenName(O.dpy, O.scr, "JetBrains Mono:pixelsize=18:weight=medium");
    O.icons = XftFontOpenName(O.dpy, O.scr, "FontAwesome:pixelsize=20");
    if (!O.font || !O.icons)
        return -1;

    /* override-redirect: above the panel and outside the window manager's tiling */
    XSetWindowAttributes a = {
        .background_pixel = O.col[C_KEY].pixel,
        .override_redirect = True,
        .event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | PointerMotionMask | StructureNotifyMask,
    };
    O.win = XCreateWindow(O.dpy, RootWindow(O.dpy, O.scr), 0, 0, O.w, O.h, 0, CopyFromParent, InputOutput,
                          CopyFromParent, CWBackPixel | CWOverrideRedirect | CWEventMask, &a);
    XStoreName(O.dpy, O.win, "amplay");
    a.background_pixel = O.col[C_BG].pixel;
    a.event_mask = ButtonPressMask | PointerMotionMask | ExposureMask;
    O.top = XCreateWindow(O.dpy, O.win, 0, 0, O.w, TOP_H, 0, CopyFromParent, InputOutput, CopyFromParent,
                          CWBackPixel | CWEventMask, &a);
    O.bar = XCreateWindow(O.dpy, O.win, 0, O.h - BAR_H, O.w, BAR_H, 0, CopyFromParent, InputOutput,
                          CopyFromParent, CWBackPixel | CWEventMask, &a);
    int depth = DefaultDepth(O.dpy, O.scr);
    O.top_pm = XCreatePixmap(O.dpy, O.top, O.w, TOP_H, depth);
    O.bar_pm = XCreatePixmap(O.dpy, O.bar, O.w, BAR_H, depth);
    O.top_d = XftDrawCreate(O.dpy, O.top_pm, vis, cmap);
    O.bar_d = XftDrawCreate(O.dpy, O.bar_pm, vis, cmap);

    O.arrow = XCreateFontCursor(O.dpy, XC_left_ptr);
    static char nothing[1];
    Pixmap empty = XCreateBitmapFromData(O.dpy, O.win, nothing, 1, 1);
    XColor black = { 0 };
    O.blank = XCreatePixmapCursor(O.dpy, empty, empty, &black, &black, 0, 0);
    XFreePixmap(O.dpy, empty);

    XkbSetDetectableAutoRepeat(O.dpy, True, NULL);
    XMapRaised(O.dpy, O.win);
    XSync(O.dpy, False);
    show();
    return 0;
}

int osd_fd(void)
{
    return ConnectionNumber(O.dpy);
}

unsigned osd_key565(void)
{
    const XRenderColor *c = &O.col[C_KEY].color;
    return (c->red >> 11) << 11 | (c->green >> 10) << 5 | c->blue >> 11;
}

static int text_width(XftFont *f, const char *s)
{
    XGlyphInfo g;
    XftTextExtentsUtf8(O.dpy, f, (const FcChar8 *)s, strlen(s), &g);
    return g.xOff;
}

static void text(XftDraw *d, XftFont *f, int color, int x, int y, const char *s)
{
    XftDrawStringUtf8(d, &O.col[color], f, x, y, (const FcChar8 *)s, strlen(s));
}

static void clock_text(char *buf, size_t n, double t)
{
    int s = t > 0 ? (int)t : 0;
    if (s >= 3600)
        snprintf(buf, n, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    else
        snprintf(buf, n, "%d:%02d", s / 60, s % 60);
}

static void draw(const struct osd_state *s)
{
    char now[16], total[16], line[48], vol[8];
    int track_w = O.w - 2 * PAD;
    int fill = s->dur > 0 ? (int)(track_w * (s->pos / s->dur)) : 0;
    fill = fill < 0 ? 0 : fill > track_w ? track_w : fill;
    clock_text(now, sizeof now, s->pos);
    clock_text(total, sizeof total, s->dur);
    if (s->loading)
        snprintf(line, sizeof line, "Loading…");
    else if (s->dur > 0)
        snprintf(line, sizeof line, "%s / %s", now, total);
    else
        snprintf(line, sizeof line, "%s", now);
    snprintf(vol, sizeof vol, "%d%%", s->volume);

    /* redraw only when something visible changed */
    char sig[sizeof O.sig];
    snprintf(sig, sizeof sig, "%d|%s|%d|%d|%s|%d|%s", fill, line, s->paused, s->muted, vol, O.w, s->title ? s->title : "");
    if (!strcmp(sig, O.sig))
        return;
    strcpy(O.sig, sig);

    /* top: title and close */
    XftDrawRect(O.top_d, &O.col[C_BG], 0, 0, O.w, TOP_H);
    if (s->title) {
        char t[200];
        snprintf(t, sizeof t, "%s", s->title);
        int max = O.w - 3 * PAD - 24;
        for (size_t n = strlen(t); n > 4 && text_width(O.font, t) > max; n--)
            strcpy(t + n - 4, "…");   /* shorten until it fits */
        text(O.top_d, O.font, C_TEXT, PAD, 28, t);
    }
    text(O.top_d, O.icons, C_SUB, O.w - PAD - 16, 29, ICON_CLOSE);

    /* bottom: progress, buttons, time, volume */
    XftDrawRect(O.bar_d, &O.col[C_BG], 0, 0, O.w, BAR_H);
    XftDrawRect(O.bar_d, &O.col[C_TRACK], PAD, TRACK_Y, track_w, TRACK_H);
    XftDrawRect(O.bar_d, &O.col[C_FILL], PAD, TRACK_Y, fill, TRACK_H);
    if (s->dur > 0)
        XftDrawRect(O.bar_d, &O.col[C_TEXT], PAD + fill - 2, TRACK_Y - 5, 4, TRACK_H + 10);

    int x = PAD, y = 50;
    const char *icons[3] = { ICON_BACK, s->paused ? ICON_PLAY : ICON_PAUSE, ICON_FWD };
    for (int i = 0; i < 3; i++) {
        O.hit[i][0] = x - 8;
        O.hit[i][1] = x + 28;
        text(O.bar_d, O.icons, i == 1 ? C_TEXT : C_SUB, x, y, icons[i]);
        x += 44;
    }
    text(O.bar_d, O.font, C_TEXT, x + 8, y - 1, line);

    int vw = text_width(O.font, vol);
    int vx = O.w - PAD - vw;
    text(O.bar_d, O.font, C_SUB, vx, y - 1, vol);
    text(O.bar_d, O.icons, s->muted ? C_SUB : C_TEXT, vx - 32, y, s->muted ? ICON_MUTE : ICON_VOL);
    O.hit[HIT_VOL][0] = vx - 40;
    O.hit[HIT_VOL][1] = O.w;

    GC gc = DefaultGC(O.dpy, O.scr);
    XCopyArea(O.dpy, O.top_pm, O.top, gc, 0, 0, O.w, TOP_H, 0, 0);
    XCopyArea(O.dpy, O.bar_pm, O.bar, gc, 0, 0, O.w, BAR_H, 0, 0);
    XFlush(O.dpy);
}

void osd_update(const struct osd_state *s)
{
    O.shown = *s;
    O.dur = s->dur;
    if (!O.grabbed)   /* the window has to be viewable first */
        O.grabbed = XGrabKeyboard(O.dpy, O.win, True, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess;
    if (O.visible && !s->paused && !s->loading && now_ms() - O.last_input > HIDE_AFTER)
        hide();
    if (s->paused || s->loading)
        show();
    if (O.visible)
        draw(s);
    XFlush(O.dpy);
}

static int key_action(KeySym k, int repeat, struct osd_event *e)
{
    int can_repeat = 1;
    switch (k) {
    case XK_space: case XK_p: case XK_Return: case XK_KP_Enter: case XF86XK_AudioPlay: case XF86XK_AudioPause:
        *e = (struct osd_event){ OSD_PAUSE, 0 };
        can_repeat = 0;
        break;
    case XK_Left: case XF86XK_AudioRewind:
        *e = (struct osd_event){ OSD_SEEK_REL, -10 };
        break;
    case XK_Right: case XF86XK_AudioForward:
        *e = (struct osd_event){ OSD_SEEK_REL, 10 };
        break;
    case XK_Page_Down: case XF86XK_AudioPrev:
        *e = (struct osd_event){ OSD_SEEK_REL, -60 };
        break;
    case XK_Page_Up: case XF86XK_AudioNext:
        *e = (struct osd_event){ OSD_SEEK_REL, 60 };
        break;
    case XK_Up: case XF86XK_AudioRaiseVolume:
        *e = (struct osd_event){ OSD_VOLUME, 5 };
        break;
    case XK_Down: case XF86XK_AudioLowerVolume:
        *e = (struct osd_event){ OSD_VOLUME, -5 };
        break;
    case XK_m: case XF86XK_AudioMute:
        *e = (struct osd_event){ OSD_MUTE, 0 };
        can_repeat = 0;
        break;
    case XK_q: case XK_Escape: case XK_BackSpace: case XF86XK_Back: case XF86XK_AudioStop:
        *e = (struct osd_event){ OSD_QUIT, 0 };
        can_repeat = 0;
        break;
    default:
        return 0;
    }
    return !repeat || can_repeat;
}

static int click_action(XButtonEvent *b, struct osd_event *e)
{
    if (b->button == Button4 || b->button == Button5) {
        *e = (struct osd_event){ OSD_VOLUME, b->button == Button4 ? 5 : -5 };
        return 1;
    }
    if (b->button != Button1)
        return 0;
    if (b->window == O.top) {
        if (b->x >= O.w - PAD - 32) {
            *e = (struct osd_event){ OSD_QUIT, 0 };
            return 1;
        }
        return 0;
    }
    if (b->window == O.bar) {
        if (b->y < TRACK_Y + TRACK_H + 10) {
            if (O.dur <= 0)
                return 0;
            double f = (double)(b->x - PAD) / (O.w - 2 * PAD);
            *e = (struct osd_event){ OSD_SEEK_ABS, (f < 0 ? 0 : f > 1 ? 1 : f) * O.dur };
            return 1;
        }
        static const struct osd_event buttons[] = {
            [HIT_BACK] = { OSD_SEEK_REL, -10 }, [HIT_PLAY] = { OSD_PAUSE, 0 },
            [HIT_FWD] = { OSD_SEEK_REL, 10 },   [HIT_VOL] = { OSD_MUTE, 0 },
        };
        for (int i = 0; i < NHITS; i++)
            if (b->x >= O.hit[i][0] && b->x < O.hit[i][1]) {
                *e = buttons[i];
                return 1;
            }
        return 0;
    }
    /* a click on the picture itself */
    *e = (struct osd_event){ OSD_PAUSE, 0 };
    return 1;
}

int osd_poll(struct osd_event *ev, int max)
{
    int n = 0;
    while (XPending(O.dpy)) {
        XEvent x;
        XNextEvent(O.dpy, &x);
        switch (x.type) {
        case KeyPress: {
            unsigned kc = x.xkey.keycode & 255;
            int repeat = O.held[kc];
            O.held[kc] = 1;
            show();
            if (n < max && key_action(XLookupKeysym(&x.xkey, 0), repeat, &ev[n]))
                n++;
            break;
        }
        case KeyRelease:
            O.held[x.xkey.keycode & 255] = 0;
            break;
        case ButtonPress: {
            int was_visible = O.visible;
            show();
            /* the first click on a hidden bar only brings it up */
            if (n < max && (was_visible || x.xbutton.window == O.win) && click_action(&x.xbutton, &ev[n]))
                n++;
            break;
        }
        case MotionNotify:
            show();
            break;
        case Expose:
            O.sig[0] = 0;
            break;
        }
    }
    if (O.visible)
        draw(&O.shown);
    return n;
}

void osd_close(void)
{
    if (!O.dpy)
        return;
    XUngrabKeyboard(O.dpy, CurrentTime);
    XDestroyWindow(O.dpy, O.win);
    XCloseDisplay(O.dpy);
    O.dpy = NULL;
}
