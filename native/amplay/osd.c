/*
 * On-screen controls for amplay.
 *
 * The video layer sits under the desktop OSD layer (fb0, where X draws), and fb0 has a hardware
 * color key: pixels of exactly that color are transparent. So the player is an ordinary X window
 * filled with the key color, and the bars are child windows on top of it. X redraws only the bars,
 * about once a second while they are visible, and nothing while they are hidden; the video itself
 * never passes through the CPU.
 *
 * Live TV adds a channel list on the left (Enter, or a click on "Channels" in the bottom bar): the
 * arrows move in it, Enter switches, Esc or Left closes it.
 *
 * Text is anti-aliased only over the opaque bars: an edge blended with the key color would no
 * longer match it. The hardware scaler (1280x720 desktop to 1080p) blends edges anyway, around the
 * bars and the cursor, so the key is next to black: the blended pixels are dark, where a bright
 * key showed as colored fringes.
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
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define KEY_COLOR   "#000008"   /* RGB565 0x0001: drawn nowhere else (the cursor is pure black and white) */
#define BAR_H       64
#define TOP_H       44
#define PAD         24
#define TRACK_Y     14          /* progress track inside the bottom bar */
#define TRACK_H     6
#define HIDE_AFTER  3000        /* ms without input */
#define LIST_W      460         /* the channel list */
#define ROW_H       54
#define LIST_AFTER  15000       /* ms without input before the list closes too */

/* Font Awesome 4 */
#define ICON_PLAY   "\xef\x81\x8b"   /* f04b */
#define ICON_PAUSE  "\xef\x81\x8c"   /* f04c */
#define ICON_BACK   "\xef\x81\x8a"   /* f04a */
#define ICON_FWD    "\xef\x81\x8e"   /* f04e */
#define ICON_VOL    "\xef\x80\xa8"   /* f028 */
#define ICON_MUTE   "\xef\x80\xa6"   /* f026 */
#define ICON_CLOSE  "\xef\x80\x8d"   /* f00d */
#define ICON_PREV   "\xef\x81\x88"   /* f048 */
#define ICON_NEXT   "\xef\x81\x91"   /* f051 */
#define CHANNELS_HINT "Enter: channels"   /* in the bottom bar when there is a list */

enum { C_KEY, C_BG, C_TRACK, C_FILL, C_TEXT, C_SUB, NCOLORS };
static const char *COLOR_NAMES[NCOLORS] = { KEY_COLOR, "#11111b", "#45475a", "#cba6f7", "#cdd6f4", "#a6adc8" };

/* clickable areas in the bottom bar: x from, x to */
enum { HIT_BACK, HIT_PLAY, HIT_FWD, HIT_VOL, NHITS };

static struct {
    Display *dpy;
    int scr, w, h;
    Window win, top, bar, list;
    Pixmap top_pm, bar_pm, list_pm;
    XftDraw *top_d, *bar_d, *list_d;
    XftFont *font, *small, *icons;
    XftColor col[NCOLORS];
    Cursor arrow, blank;
    int visible, grabbed;
    int64_t last_input;
    char held[256];            /* keys down, to tell autorepeat from a new press */
    int hit[NHITS][2];
    int hit_list[2];           /* "Channels" in the bottom bar */
    /* live: the channel list */
    struct channel { char num[8], name[96], now[160]; } *ch;
    int nch, cur, sel, top_row, list_open, list_dirty, list_h;
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

static void list_show(void)
{
    if (O.list_open || !O.nch)
        return;
    O.sel = O.cur;
    O.top_row = O.cur - O.list_h / ROW_H / 2;   /* the playing one in the middle */
    if (O.top_row < 0)
        O.top_row = 0;
    XMapRaised(O.dpy, O.list);
    O.list_open = O.list_dirty = 1;
}

static void list_hide(void)
{
    if (!O.list_open)
        return;
    XUnmapWindow(O.dpy, O.list);
    O.list_open = 0;
}

static void hide(void)
{
    if (!O.visible)
        return;
    list_hide();
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
    O.small = XftFontOpenName(O.dpy, O.scr, "JetBrains Mono:pixelsize=15");
    O.icons = XftFontOpenName(O.dpy, O.scr, "FontAwesome:pixelsize=20");
    if (!O.font || !O.small || !O.icons)
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
    /* between the bars, left; mapped only while open */
    O.list_h = O.h - TOP_H - BAR_H;
    O.list = XCreateWindow(O.dpy, O.win, 0, TOP_H, LIST_W, O.list_h, 0, CopyFromParent, InputOutput,
                           CopyFromParent, CWBackPixel | CWEventMask, &a);
    O.list_pm = XCreatePixmap(O.dpy, O.list, LIST_W, O.list_h, depth);
    O.list_d = XftDrawCreate(O.dpy, O.list_pm, vis, cmap);

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

/* Shortens t (a buffer of at least 260 bytes holding at most 255) until it fits in max pixels, a
   whole character at a time: Xft stops at broken UTF-8 */
static void fit(char *t, XftFont *f, int max)
{
    size_t n = strlen(t);
    for (int cut = n == 255; n > 0 && (cut || text_width(f, t) > max); cut = 0) {
        do n--; while (n > 0 && ((unsigned char)t[n] & 0xc0) == 0x80);
        strcpy(t + n, "…");
    }
}

void osd_set_channels(char **lines, int n, int current)
{
    O.ch = calloc(n, sizeof *O.ch);
    O.nch = O.ch ? n : 0;
    for (int i = 0; i < O.nch; i++) {
        char *f[3] = { lines[i], NULL, NULL };
        for (int k = 1; k < 3 && f[k - 1]; k++)
            if ((f[k] = strchr(f[k - 1], '\t')))
                *f[k]++ = 0;
        snprintf(O.ch[i].num, sizeof O.ch[i].num, "%s", f[0] ? f[0] : "");
        snprintf(O.ch[i].name, sizeof O.ch[i].name, "%s", f[1] ? f[1] : "");
        snprintf(O.ch[i].now, sizeof O.ch[i].now, "%s", f[2] ? f[2] : "");
    }
    O.cur = O.sel = current >= 0 && current < O.nch ? current : 0;
}

static void draw_list(void)
{
    if (!O.list_open || !O.list_dirty)
        return;
    O.list_dirty = 0;
    int rows = O.list_h / ROW_H;
    if (O.sel < O.top_row)
        O.top_row = O.sel;
    if (O.sel >= O.top_row + rows)
        O.top_row = O.sel - rows + 1;
    XftDrawRect(O.list_d, &O.col[C_BG], 0, 0, LIST_W, O.list_h);
    for (int r = 0; r < rows && O.top_row + r < O.nch; r++) {
        int i = O.top_row + r, y = r * ROW_H;
        char t[260];
        if (i == O.sel)
            XftDrawRect(O.list_d, &O.col[C_TRACK], 0, y, LIST_W, ROW_H);
        text(O.list_d, O.font, i == O.cur ? C_FILL : C_SUB, PAD, y + 25, O.ch[i].num);
        snprintf(t, 256, "%s", O.ch[i].name);
        fit(t, O.font, LIST_W - PAD - 64 - 16);
        text(O.list_d, O.font, C_TEXT, PAD + 52, y + 25, t);
        snprintf(t, 256, "%s", O.ch[i].now);
        fit(t, O.small, LIST_W - PAD - 64 - 16);
        text(O.list_d, O.small, C_SUB, PAD + 52, y + 45, t);
    }
    XftDrawRect(O.list_d, &O.col[C_TRACK], LIST_W - 2, 0, 2, O.list_h);
    XCopyArea(O.dpy, O.list_pm, O.list, DefaultGC(O.dpy, O.scr), 0, 0, LIST_W, O.list_h, 0, 0);
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
    else if (s->live)
        snprintf(line, sizeof line, O.nch ? "LIVE      " CHANNELS_HINT : "LIVE");
    else if (s->dur > 0)
        snprintf(line, sizeof line, "%s / %s", now, total);
    else
        snprintf(line, sizeof line, "%s", now);
    snprintf(vol, sizeof vol, "%d%%", s->volume);

    /* redraw only when something visible changed */
    char sig[sizeof O.sig];
    snprintf(sig, sizeof sig, "%d|%s|%d|%d|%s|%d|%d|%s", fill, line, s->paused, s->muted, vol, O.w, s->live,
             s->title ? s->title : "");
    if (!strcmp(sig, O.sig))
        return;
    strcpy(O.sig, sig);

    /* top: title and close */
    XftDrawRect(O.top_d, &O.col[C_BG], 0, 0, O.w, TOP_H);
    if (s->title) {
        char t[260];
        snprintf(t, 256, "%s", s->title);
        fit(t, O.font, O.w - 3 * PAD - 24);
        text(O.top_d, O.font, C_TEXT, PAD, 28, t);
    }
    text(O.top_d, O.icons, C_SUB, O.w - PAD - 16, 29, ICON_CLOSE);

    /* bottom: progress, buttons, time, volume */
    XftDrawRect(O.bar_d, &O.col[C_BG], 0, 0, O.w, BAR_H);
    if (!s->live) {
        XftDrawRect(O.bar_d, &O.col[C_TRACK], PAD, TRACK_Y, track_w, TRACK_H);
        XftDrawRect(O.bar_d, &O.col[C_FILL], PAD, TRACK_Y, fill, TRACK_H);
    }
    if (s->dur > 0 && !s->live)
        XftDrawRect(O.bar_d, &O.col[C_TEXT], PAD + fill - 2, TRACK_Y - 5, 4, TRACK_H + 10);

    int x = PAD, y = 50;
    const char *icons[3] = { s->live ? ICON_PREV : ICON_BACK, s->paused ? ICON_PLAY : ICON_PAUSE,
                             s->live ? ICON_NEXT : ICON_FWD };
    for (int i = 0; i < 3; i++) {
        O.hit[i][0] = x - 8;
        O.hit[i][1] = x + 28;
        text(O.bar_d, O.icons, i == 1 ? C_TEXT : C_SUB, x, y, icons[i]);
        x += 44;
    }
    text(O.bar_d, O.font, C_TEXT, x + 8, y - 1, line);
    O.hit_list[0] = O.hit_list[1] = 0;
    if (s->live && O.nch && !s->loading) {   /* "Channels" after "LIVE", clickable */
        int lx = x + 8 + text_width(O.font, "LIVE      ");
        O.hit_list[0] = lx - 8;
        O.hit_list[1] = lx + text_width(O.font, CHANNELS_HINT) + 8;
    }

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
    if (O.list_open && now_ms() - O.last_input > LIST_AFTER)
        list_hide();
    if (O.visible && !O.list_open && !s->paused && !s->loading && now_ms() - O.last_input > HIDE_AFTER)
        hide();
    if (s->paused || s->loading)
        show();
    if (O.visible) {
        draw(s);
        draw_list();
    }
    XFlush(O.dpy);
}

/* Keys while the channel list is open: 1 if the key was the list's (an event may be stored) */
static int list_key(KeySym k, struct osd_event *e, int *made)
{
    int page = O.list_h / ROW_H;
    *made = 0;
    switch (k) {
    case XK_Up:        O.sel = O.sel > 0 ? O.sel - 1 : O.nch - 1; break;
    case XK_Down:      O.sel = O.sel < O.nch - 1 ? O.sel + 1 : 0; break;
    case XK_Page_Up:   O.sel = O.sel > page ? O.sel - page : 0; break;
    case XK_Page_Down: O.sel = O.sel + page < O.nch ? O.sel + page : O.nch - 1; break;
    case XK_Home:      O.sel = 0; break;
    case XK_End:       O.sel = O.nch - 1; break;
    case XK_Return: case XK_KP_Enter: case XK_Right:
        if (O.sel != O.cur) {
            *e = (struct osd_event){ OSD_CHANNEL, O.sel };
            *made = 1;
        }
        list_hide();
        return 1;
    case XK_Escape: case XK_BackSpace: case XF86XK_Back: case XK_Left: case XK_l:
        list_hide();
        return 1;
    default:
        return 0;
    }
    O.list_dirty = 1;
    return 1;
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
        if (b->x >= O.hit_list[0] && b->x < O.hit_list[1]) {
            list_show();
            return 0;
        }
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
            KeySym k = XLookupKeysym(&x.xkey, 0);
            int made;
            if (O.list_open && list_key(k, &ev[n < max ? n : max - 1], &made)) {
                n += made && n < max;
                break;
            }
            if (O.nch && !O.list_open && (k == XK_Return || k == XK_KP_Enter || k == XK_l)) {
                list_show();
                break;
            }
            if (n < max && key_action(k, repeat, &ev[n]))
                n++;
            break;
        }
        case KeyRelease:
            O.held[x.xkey.keycode & 255] = 0;
            break;
        case ButtonPress: {
            int was_visible = O.visible;
            show();
            if (O.list_open) {
                XButtonEvent *b = &x.xbutton;
                if (b->window == O.list && b->button == Button1) {   /* a channel */
                    int i = O.top_row + b->y / ROW_H;
                    if (i < O.nch && i != O.cur && n < max)
                        ev[n++] = (struct osd_event){ OSD_CHANNEL, i };
                    if (i < O.nch)
                        list_hide();
                } else if (b->window == O.list && (b->button == Button4 || b->button == Button5)) {
                    list_key(b->button == Button4 ? XK_Up : XK_Down, &ev[0], &(int){ 0 });
                } else if (b->window == O.win) {   /* the picture: just close the list */
                    list_hide();
                } else {
                    goto bars;
                }
                break;
            }
        bars:
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
            O.list_dirty = 1;
            break;
        }
    }
    if (O.visible) {
        draw(&O.shown);
        draw_list();
    }
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
