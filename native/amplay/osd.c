/*
 * On-screen controls for amplay.
 *
 * The video layer sits under the desktop OSD layer (fb0, where X draws), and fb0 has a hardware
 * color key: pixels of exactly that color are transparent. So the player is an ordinary X window
 * filled with the key color, and the bars are child windows on top of it. X redraws only the bars,
 * about once a second while they are visible, and nothing while they are hidden; the video itself
 * never passes through the CPU.
 *
 * The TV player adds a channel list on the left (Enter, or a click on "Channels" in the bottom
 * bar): the arrows move in it, Enter switches, Esc or Left closes it. Also an info panel on the
 * right (info), a message box in the middle (no signal, the PIN prompt...), the radio screen, and
 * pictures drawn straight on the screen window: subtitles and teletext pages.
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
#define DIGITS_AFTER 1500       /* ms after the last digit of a channel number before switching */
#define INFO_W      560         /* the info panel */
#define MSG_H       150

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
#define ICON_MUSIC  "\xef\x80\x81"   /* f001 */
#define ICON_LOCK   "\xef\x80\xa3"   /* f023 */
#define CHANNELS_HINT "Enter: channels"   /* in the bottom bar when there is a list */

enum { C_KEY, C_BG, C_TRACK, C_FILL, C_TEXT, C_SUB, C_RED, C_GREEN, C_YELLOW, NCOLORS };
static const char *COLOR_NAMES[NCOLORS] = { KEY_COLOR, "#11111b", "#45475a", "#cba6f7", "#cdd6f4", "#a6adc8",
                                            "#f38ba8", "#a6e3a1", "#f9e2af" };

/* clickable areas in the bottom bar: x from, x to */
enum { HIT_BACK, HIT_PLAY, HIT_FWD, HIT_VOL, NHITS };

static struct {
    Display *dpy;
    int scr, w, h, tv;
    Window win, top, bar, list, msg, info;
    Pixmap top_pm, bar_pm, list_pm, msg_pm, info_pm;
    XftDraw *top_d, *bar_d, *list_d, *msg_d, *info_d, *win_d;
    XftFont *font, *small, *icons, *big, *huge, *icons_big;
    XftColor col[NCOLORS];
    GC gc;
    Cursor arrow, blank;
    int visible, grabbed, hide_after;
    int64_t last_input;
    char held[256];            /* keys down, to tell autorepeat from a new press */
    int hit[NHITS][2];
    int hit_list[2];           /* "Channels" in the bottom bar */
    /* TV: the channel list */
    struct channel { char num[8], name[96], now[160]; int skip; } *ch;
    int nch, cur, sel, top_row, list_open, list_dirty, list_h;
    int digits;                /* a channel number being typed (the remote's digits), 0 if none */
    int64_t digits_at;
    /* TV: the message box, the PIN prompt, the info panel, radio, teletext */
    char msg_text[512];
    int64_t msg_until;         /* 0: until replaced */
    int msg_on, msg_dirty;
    char pin_prompt[256];
    char pin_typed[8];
    int pin_on;
    char info_text[4096], info_extra[1024];
    int info_on, info_dirty;
    char radio_name[256], radio_now[256];
    int radio;
    int ttx, ttx_page, ttx_typed, ttx_n;
    struct image { XImage *im; int x, y, w, h; } img[2];
    struct osd_state shown;    /* what the bars show now */
    double dur;
    char sig[512];
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
    O.digits = 0;
    if (!O.list_open)
        return;
    XUnmapWindow(O.dpy, O.list);
    O.list_open = 0;
}

static void info_hide(void)
{
    if (!O.info_on)
        return;
    XUnmapWindow(O.dpy, O.info);
    O.info_on = 0;
}

static void info_show(void)
{
    if (O.info_on)
        return;
    XMapRaised(O.dpy, O.info);
    O.info_on = O.info_dirty = 1;
}

static void hide(void)
{
    if (!O.visible)
        return;
    list_hide();
    info_hide();
    XUnmapWindow(O.dpy, O.top);
    XUnmapWindow(O.dpy, O.bar);
    XDefineCursor(O.dpy, O.win, O.blank);
    O.visible = 0;
}

static Window child(int x, int y, int w, int h, Pixmap *pm, XftDraw **d)
{
    XSetWindowAttributes a = {
        .background_pixel = O.col[C_BG].pixel,
        .event_mask = ButtonPressMask | PointerMotionMask | ExposureMask,
    };
    Window win = XCreateWindow(O.dpy, O.win, x, y, w, h, 0, CopyFromParent, InputOutput, CopyFromParent,
                               CWBackPixel | CWEventMask, &a);
    *pm = XCreatePixmap(O.dpy, win, w, h, DefaultDepth(O.dpy, O.scr));
    *d = XftDrawCreate(O.dpy, *pm, DefaultVisual(O.dpy, O.scr), DefaultColormap(O.dpy, O.scr));
    return win;
}

int osd_open(int tv)
{
    if (!(O.dpy = XOpenDisplay(NULL)))
        return -1;
    O.tv = tv;
    O.hide_after = HIDE_AFTER;
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
    O.big = XftFontOpenName(O.dpy, O.scr, "DejaVu Sans:pixelsize=26");
    O.huge = XftFontOpenName(O.dpy, O.scr, "DejaVu Sans:pixelsize=40:weight=bold");
    O.icons_big = XftFontOpenName(O.dpy, O.scr, "FontAwesome:pixelsize=96");
    if (!O.font || !O.small || !O.icons || !O.big || !O.huge || !O.icons_big)
        return -1;

    /* override-redirect: above the panel and outside the window manager's tiling */
    XSetWindowAttributes a = {
        .background_pixel = O.col[C_KEY].pixel,
        .override_redirect = True,
        .event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | PointerMotionMask | StructureNotifyMask |
                      ExposureMask,
    };
    O.win = XCreateWindow(O.dpy, RootWindow(O.dpy, O.scr), 0, 0, O.w, O.h, 0, CopyFromParent, InputOutput,
                          CopyFromParent, CWBackPixel | CWOverrideRedirect | CWEventMask, &a);
    XStoreName(O.dpy, O.win, "amplay");
    O.gc = XCreateGC(O.dpy, O.win, 0, NULL);
    O.win_d = XftDrawCreate(O.dpy, O.win, vis, cmap);
    O.top = child(0, 0, O.w, TOP_H, &O.top_pm, &O.top_d);
    O.bar = child(0, O.h - BAR_H, O.w, BAR_H, &O.bar_pm, &O.bar_d);
    /* between the bars, left and right; mapped only while open */
    O.list_h = O.h - TOP_H - BAR_H;
    O.list = child(0, TOP_H, LIST_W, O.list_h, &O.list_pm, &O.list_d);
    O.info = child(O.w - INFO_W, TOP_H, INFO_W, O.list_h, &O.info_pm, &O.info_d);
    O.msg = child(100, (O.h - MSG_H) / 2, O.w - 200, MSG_H, &O.msg_pm, &O.msg_d);

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

int osd_width(void)
{
    return O.w;
}

int osd_height(void)
{
    return O.h;
}

void osd_set_hide_after(int ms)
{
    O.hide_after = ms > 500 ? ms : 500;
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

/* ---- TV: the channel list ------------------------------------------------------------------ */

void osd_set_channels(char **lines, int n, int current)
{
    free(O.ch);
    O.ch = calloc(n > 0 ? n : 1, sizeof *O.ch);
    O.nch = O.ch ? n : 0;
    for (int i = 0; i < O.nch; i++) {
        char *f[4] = { lines[i], NULL, NULL, NULL };
        for (int k = 1; k < 4 && f[k - 1]; k++)
            if ((f[k] = strchr(f[k - 1], '\t')))
                *f[k]++ = 0;
        snprintf(O.ch[i].num, sizeof O.ch[i].num, "%s", f[0] ? f[0] : "");
        snprintf(O.ch[i].name, sizeof O.ch[i].name, "%s", f[1] ? f[1] : "");
        snprintf(O.ch[i].now, sizeof O.ch[i].now, "%s", f[2] ? f[2] : "");
        O.ch[i].skip = f[3] && strchr(f[3], 's');
    }
    O.cur = O.sel = current >= 0 && current < O.nch ? current : 0;
    O.list_dirty = 1;
}

void osd_set_current(int index)
{
    if (index >= 0 && index < O.nch)
        O.cur = index;
    O.list_dirty = 1;
}

const char *osd_channel_title(int index, char *buf, int size)
{
    if (index < 0 || index >= O.nch)
        return NULL;
    snprintf(buf, size, "%s  %s", O.ch[index].num, O.ch[index].name);
    return buf;
}

/* The next channel in a direction that isn't skipped (all skipped: the next one) */
static int channel_step(int dir)
{
    for (int k = 1; k <= O.nch; k++) {
        int i = ((O.cur + dir * k) % O.nch + O.nch) % O.nch;
        if (!O.ch[i].skip)
            return i;
    }
    return ((O.cur + dir) % O.nch + O.nch) % O.nch;
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
        text(O.list_d, O.font, O.ch[i].skip ? C_SUB : C_TEXT, PAD + 52, y + 25, t);
        snprintf(t, 256, "%s", O.ch[i].now);
        fit(t, O.small, LIST_W - PAD - 64 - 16);
        text(O.list_d, O.small, C_SUB, PAD + 52, y + 45, t);
    }
    XftDrawRect(O.list_d, &O.col[C_TRACK], LIST_W - 2, 0, 2, O.list_h);
    XCopyArea(O.dpy, O.list_pm, O.list, O.gc, 0, 0, LIST_W, O.list_h, 0, 0);
}

/* ---- TV: the message box, PIN prompt, info panel, radio screen ------------------------------ */

void osd_message(const char *t, int seconds)
{
    snprintf(O.msg_text, sizeof O.msg_text, "%s", t ? t : "");
    O.msg_until = seconds > 0 ? now_ms() + seconds * 1000LL : 0;
    O.msg_dirty = 1;
}

void osd_pin(const char *prompt)
{
    O.pin_on = prompt != NULL;
    snprintf(O.pin_prompt, sizeof O.pin_prompt, "%s", prompt ? prompt : "");
    O.pin_typed[0] = 0;
    O.msg_dirty = 1;
}

/* Lines of t, broken at spaces to fit max pixels, drawn from y on; returns the y after them */
static int draw_wrapped(XftDraw *d, XftFont *f, int color, int x, int y, int max, int bottom, const char *t)
{
    char line[512];
    while (*t && y < bottom) {
        size_t n = strcspn(t, "\n");
        if (!n) {   /* an empty line: half a line's space */
            y += (f->ascent + f->descent) / 2;
            t++;
            continue;
        }
        size_t take = n < sizeof line - 1 ? n : sizeof line - 1;
        /* the longest run of whole words that fits */
        for (;;) {
            memcpy(line, t, take);
            line[take] = 0;
            if (text_width(f, line) <= max)
                break;
            size_t sp = take;
            while (sp > 0 && line[sp] != ' ')
                sp--;
            if (sp == 0) {   /* one long word: cut it */
                do take--; while (take > 0 && ((unsigned char)t[take] & 0xc0) == 0x80);
            } else {
                take = sp;
            }
            if (!take)
                break;
        }
        if (!take)
            break;
        text(d, f, color, x, y + f->ascent, line);
        y += f->ascent + f->descent + 4;
        t += take;
        while (*t == ' ')
            t++;
        if (*t == '\n')
            t++;
    }
    return y;
}

static void draw_msg(void)
{
    int on = O.pin_on || (O.msg_text[0] && (!O.msg_until || now_ms() < O.msg_until));
    if (!on) {
        if (O.msg_on) {
            XUnmapWindow(O.dpy, O.msg);
            O.msg_on = 0;
        }
        return;
    }
    if (!O.msg_on) {
        XMapRaised(O.dpy, O.msg);
        O.msg_on = O.msg_dirty = 1;
    }
    if (!O.msg_dirty)
        return;
    O.msg_dirty = 0;
    int w = O.w - 200;
    XftDrawRect(O.msg_d, &O.col[C_BG], 0, 0, w, MSG_H);
    XftDrawRect(O.msg_d, &O.col[C_FILL], 0, 0, w, 3);
    if (O.pin_on) {
        char dots[64] = "";
        for (int i = 0; i < 4; i++)
            strcat(dots, i < (int)strlen(O.pin_typed) ? "●  " : "○  ");
        text(O.msg_d, O.icons, C_FILL, PAD, 50, ICON_LOCK);
        text(O.msg_d, O.big, C_TEXT, PAD + 40, 52, O.pin_prompt);
        text(O.msg_d, O.huge, C_TEXT, (w - text_width(O.huge, dots)) / 2, 115, dots);
    } else {
        int lines = 1;
        for (const char *p = O.msg_text; *p; p++)
            lines += *p == '\n';
        int lh = O.big->ascent + O.big->descent + 4;
        int y = (MSG_H - lines * lh) / 2;
        draw_wrapped(O.msg_d, O.big, C_TEXT, PAD, y < 8 ? 8 : y, w - 2 * PAD, MSG_H - 8, O.msg_text);
    }
    XCopyArea(O.dpy, O.msg_pm, O.msg, O.gc, 0, 0, w, MSG_H, 0, 0);
}

void osd_info(const char *t)
{
    snprintf(O.info_text, sizeof O.info_text, "%s", t ? t : "");
    O.info_dirty = 1;
}

void osd_info_extra(const char *t)
{
    if (strcmp(O.info_extra, t ? t : "")) {
        snprintf(O.info_extra, sizeof O.info_extra, "%s", t ? t : "");
        O.info_dirty = 1;
    }
}

int osd_info_open(void)
{
    return O.info_on;
}

static void draw_info(void)
{
    if (!O.info_on || !O.info_dirty)
        return;
    O.info_dirty = 0;
    XftDrawRect(O.info_d, &O.col[C_BG], 0, 0, INFO_W, O.list_h);
    XftDrawRect(O.info_d, &O.col[C_TRACK], 0, 0, 2, O.list_h);
    /* ozone-tv's text: the first line as a heading */
    const char *t = O.info_text;
    size_t n = strcspn(t, "\n");
    char head[260];
    snprintf(head, sizeof head, "%.*s", (int)(n < 255 ? n : 255), t);
    fit(head, O.font, INFO_W - 2 * PAD);
    text(O.info_d, O.font, C_TEXT, PAD, 34, head);
    int y = 48;
    if (t[n])
        y = draw_wrapped(O.info_d, O.small, C_SUB, PAD, y, INFO_W - 2 * PAD, O.list_h - 120, t + n + 1);
    XftDrawRect(O.info_d, &O.col[C_TRACK], PAD, y + 6, INFO_W - 2 * PAD, 1);
    draw_wrapped(O.info_d, O.small, C_TEXT, PAD, y + 16, INFO_W - 2 * PAD, O.list_h - 8, O.info_extra);
    XCopyArea(O.dpy, O.info_pm, O.info, O.gc, 0, 0, INFO_W, O.list_h, 0, 0);
}

static void draw_radio(void)
{
    if (!O.radio)
        return;
    char t[260];
    int cy = O.h / 2;
    text(O.win_d, O.icons_big, C_FILL, (O.w - text_width(O.icons_big, ICON_MUSIC)) / 2, cy - 60, ICON_MUSIC);
    snprintf(t, 256, "%s", O.radio_name);
    fit(t, O.huge, O.w - 2 * PAD);
    text(O.win_d, O.huge, C_TEXT, (O.w - text_width(O.huge, t)) / 2, cy + 40, t);
    snprintf(t, 256, "%s", O.radio_now);
    fit(t, O.big, O.w - 2 * PAD);
    text(O.win_d, O.big, C_SUB, (O.w - text_width(O.big, t)) / 2, cy + 90, t);
}

void osd_radio(const char *name, const char *now)
{
    int on = name != NULL;
    if (on && O.radio && !strcmp(name, O.radio_name) && !strcmp(now ? now : "", O.radio_now))
        return;
    O.radio = on;
    snprintf(O.radio_name, sizeof O.radio_name, "%s", name ? name : "");
    snprintf(O.radio_now, sizeof O.radio_now, "%s", now ? now : "");
    /* the screen opaque, or the key again for the video layer */
    XSetWindowBackground(O.dpy, O.win, O.col[on ? C_BG : C_KEY].pixel);
    XClearWindow(O.dpy, O.win);
    draw_radio();
    for (int i = 0; i < 2; i++)
        if (O.img[i].im)
            XPutImage(O.dpy, O.win, O.gc, O.img[i].im, 0, 0, O.img[i].x, O.img[i].y, O.img[i].w, O.img[i].h);
}

void osd_teletext(int on, int page)
{
    O.ttx = on;
    O.ttx_page = page;
    O.ttx_n = O.ttx_typed = 0;
    O.sig[0] = 0;
}

/* ---- pictures on the screen window: subtitles and teletext --------------------------------- */

static unsigned long rgb_pixel(Visual *v, unsigned r, unsigned g, unsigned b)
{
    unsigned long p = 0;
    unsigned long masks[3] = { v->red_mask, v->green_mask, v->blue_mask };
    unsigned vals[3] = { r, g, b };
    for (int i = 0; i < 3; i++) {
        unsigned long m = masks[i];
        int shift = 0, bits = 0;
        while (m && !(m & 1)) {
            m >>= 1;
            shift++;
        }
        while (m & 1) {
            m >>= 1;
            bits++;
        }
        p |= (unsigned long)(vals[i] >> (8 - bits)) << shift;
    }
    return p;
}

void osd_image_clear(int layer)
{
    struct image *m = &O.img[layer];
    if (!m->im)
        return;
    XClearArea(O.dpy, O.win, m->x, m->y, m->w, m->h, False);
    XDestroyImage(m->im);
    m->im = NULL;
    draw_radio();
    /* the other layer, if it was under this one */
    struct image *o = &O.img[!layer];
    if (o->im)
        XPutImage(O.dpy, O.win, O.gc, o->im, 0, 0, o->x, o->y, o->w, o->h);
    XFlush(O.dpy);
}

void osd_image(int layer, const uint8_t *rgba, int w, int h, int x, int y, int dw, int dh)
{
    osd_image_clear(layer);
    if (dw <= 0 || dh <= 0 || w <= 0 || h <= 0)
        return;
    Visual *vis = DefaultVisual(O.dpy, O.scr);
    XImage *im = XCreateImage(O.dpy, vis, DefaultDepth(O.dpy, O.scr), ZPixmap, 0, NULL, dw, dh, 32, 0);
    if (!im)
        return;
    im->data = malloc((size_t)im->bytes_per_line * dh);
    if (!im->data) {
        XDestroyImage(im);
        return;
    }
    unsigned long key = O.col[O.radio ? C_BG : C_KEY].pixel;
    int fast16 = im->bits_per_pixel == 16;
    for (int j = 0; j < dh; j++) {
        const uint8_t *row = rgba + (size_t)(j * h / dh) * w * 4;
        uint16_t *out16 = (uint16_t *)(im->data + (size_t)j * im->bytes_per_line);
        for (int i = 0; i < dw; i++) {
            const uint8_t *p = row + (i * w / dw) * 4;
            unsigned long px = p[3] < 128 ? key : rgb_pixel(vis, p[0], p[1], p[2]);
            if (px == O.col[C_KEY].pixel && p[3] >= 128)
                px ^= 1;   /* an opaque pixel of exactly the key color would show the video */
            if (fast16)
                out16[i] = px;
            else
                XPutPixel(im, i, j, px);
        }
    }
    struct image *m = &O.img[layer];
    *m = (struct image){ im, x, y, dw, dh };
    XPutImage(O.dpy, O.win, O.gc, im, 0, 0, x, y, dw, dh);
    XFlush(O.dpy);
}

/* ---- the bars ------------------------------------------------------------------------------ */

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
    char now[16], total[16], line[64], vol[8], ind[160] = "", title[256];
    int track_w = O.w - 2 * PAD;
    int fill = s->dur > 0 ? (int)(track_w * (s->pos / s->dur)) : 0;
    fill = fill < 0 ? 0 : fill > track_w ? track_w : fill;
    clock_text(now, sizeof now, s->pos);
    clock_text(total, sizeof total, s->dur);
    int shifted = s->live && s->shift >= 1;
    if (s->loading)
        snprintf(line, sizeof line, "Loading…");
    else if (s->live && shifted)
        snprintf(line, sizeof line, "−%s      %s", (clock_text(now, sizeof now, s->shift), now),
                 O.nch ? CHANNELS_HINT : "");
    else if (s->live)
        snprintf(line, sizeof line, O.nch ? "LIVE      " CHANNELS_HINT : "LIVE");
    else if (s->dur > 0)
        snprintf(line, sizeof line, "%s / %s", now, total);
    else
        snprintf(line, sizeof line, "%s", now);
    snprintf(vol, sizeof vol, "%d%%", s->volume);
    if (s->live) {
        if (s->audio)
            snprintf(ind + strlen(ind), sizeof ind - strlen(ind), "Audio %s   ", s->audio);
        if (s->subs)
            snprintf(ind + strlen(ind), sizeof ind - strlen(ind), "CC %s   ", s->subs);
        if (s->signal >= 0)
            snprintf(ind + strlen(ind), sizeof ind - strlen(ind), "Signal %d%%   ", s->signal);
    }
    if (O.ttx) {
        char typed[8] = "";
        for (int i = 0; i < 3; i++)
            typed[i] = i < O.ttx_n ? "0123456789"[(O.ttx_typed >> (4 * (O.ttx_n - 1 - i))) & 15] : '_';
        typed[3] = 0;
        snprintf(title, sizeof title, "Teletext  %s    %s", O.ttx_n ? typed : "", s->title ? s->title : "");
    } else {
        snprintf(title, sizeof title, "%s", s->title ? s->title : "");
    }

    /* redraw only when something visible changed */
    char sig[sizeof O.sig];
    snprintf(sig, sizeof sig, "%d|%s|%d|%d|%s|%d|%d|%d|%s|%s", fill, line, s->paused, s->muted, vol, O.w, s->live,
             s->recording, ind, title);
    if (!strcmp(sig, O.sig))
        return;
    strcpy(O.sig, sig);

    /* top: title, recording, close */
    XftDrawRect(O.top_d, &O.col[C_BG], 0, 0, O.w, TOP_H);
    int rec_w = s->recording ? text_width(O.font, "● REC") + PAD : 0;
    if (title[0]) {
        char t[260];
        snprintf(t, 256, "%s", title);
        fit(t, O.font, O.w - 3 * PAD - 24 - rec_w);
        text(O.top_d, O.font, C_TEXT, PAD, 28, t);
    }
    if (s->recording)
        text(O.top_d, O.font, C_RED, O.w - PAD - 40 - rec_w, 28, "● REC");
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
    text(O.bar_d, O.font, shifted ? C_YELLOW : C_TEXT, x + 8, y - 1, line);
    O.hit_list[0] = O.hit_list[1] = 0;
    if (s->live && O.nch && !s->loading) {   /* "Channels" after "LIVE", clickable */
        const char *hint = strstr(line, CHANNELS_HINT);
        if (hint) {
            char before[64];
            snprintf(before, sizeof before, "%.*s", (int)(hint - line), line);
            int lx = x + 8 + text_width(O.font, before);
            O.hit_list[0] = lx - 8;
            O.hit_list[1] = lx + text_width(O.font, CHANNELS_HINT) + 8;
        }
    }

    int vw = text_width(O.font, vol);
    int vx = O.w - PAD - vw;
    text(O.bar_d, O.font, C_SUB, vx, y - 1, vol);
    text(O.bar_d, O.icons, s->muted ? C_SUB : C_TEXT, vx - 32, y, s->muted ? ICON_MUTE : ICON_VOL);
    O.hit[HIT_VOL][0] = vx - 40;
    O.hit[HIT_VOL][1] = O.w;
    if (ind[0])
        text(O.bar_d, O.small, C_SUB, vx - 48 - text_width(O.small, ind), y - 2, ind);

    XCopyArea(O.dpy, O.top_pm, O.top, O.gc, 0, 0, O.w, TOP_H, 0, 0);
    XCopyArea(O.dpy, O.bar_pm, O.bar, O.gc, 0, 0, O.w, BAR_H, 0, 0);
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
    if (O.visible && !O.list_open && !O.info_on && !s->paused && !s->loading &&
        now_ms() - O.last_input > O.hide_after)
        hide();
    if (s->paused || s->loading)
        show();
    if (O.visible) {
        draw(s);
        draw_list();
        draw_info();
    }
    draw_msg();
    XFlush(O.dpy);
}

/* ---- keys and clicks ----------------------------------------------------------------------- */

/* A digit of a channel number: the list opens on the channel with the number typed so far;
   osd_poll switches to it once no digit has come for DIGITS_AFTER */
static void digit_key(int d)
{
    if (now_ms() - O.digits_at > DIGITS_AFTER || O.digits >= 100)
        O.digits = 0;
    O.digits = O.digits * 10 + d;
    O.digits_at = now_ms();
    list_show();
    for (int i = 0; i < O.nch; i++)
        if (atoi(O.ch[i].num) == O.digits)
            O.sel = i;
    O.list_dirty = 1;
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

/* The PIN prompt takes digits and delete; Esc closes it. 1 if the key was its own */
static int pin_key(KeySym k, struct osd_event *e, int *made)
{
    *made = 0;
    size_t n = strlen(O.pin_typed);
    if (k >= XK_0 && k <= XK_9) {
        O.pin_typed[n++] = '0' + (k - XK_0);
        O.pin_typed[n] = 0;
        if (n == 4) {
            *e = (struct osd_event){ OSD_PIN, atoi(O.pin_typed) };
            *made = 1;
            O.pin_typed[0] = 0;
        }
    } else if (k == XK_BackSpace) {
        if (n)
            O.pin_typed[n - 1] = 0;
    } else if (k == XK_Escape || k == XF86XK_Back) {
        osd_pin(NULL);
    } else {
        return 0;
    }
    O.msg_dirty = 1;
    return 1;
}

/* Teletext: digits make a page number, the arrows step, the colors follow the page's links */
static int ttx_key(KeySym k, struct osd_event *e, int *made)
{
    *made = 1;
    if (k >= XK_0 && k <= XK_9) {
        int d = k - XK_0;
        if (O.ttx_n == 0 && (d < 1 || d > 8)) {   /* pages are 100-899 */
            *made = 0;
            return 1;
        }
        O.ttx_typed = O.ttx_typed << 4 | d;
        if (++O.ttx_n == 3) {
            *e = (struct osd_event){ OSD_TEXT_PAGE, O.ttx_typed };
            O.ttx_n = O.ttx_typed = 0;
        } else {
            *made = 0;
        }
        O.sig[0] = 0;
        show();
        return 1;
    }
    switch (k) {
    case XK_Up: case XK_Page_Up: case XK_Right: e->value = TTX_NEXT; break;
    case XK_Down: case XK_Page_Down: case XK_Left: e->value = TTX_PREV; break;
    case XK_Return: case XK_KP_Enter: e->value = TTX_MIX; break;
    case XF86XK_Launch1: case XK_F1: e->value = TTX_LINK; break;
    case XF86XK_Launch2: case XK_F2: e->value = TTX_LINK - 1; break;
    case XF86XK_Launch3: case XK_F3: e->value = TTX_LINK - 2; break;
    case XF86XK_Launch4: case XK_F4: e->value = TTX_LINK - 3; break;
    case XK_Escape: case XF86XK_Back: case XK_BackSpace: case XF86XK_Launch5: case XK_t: case XK_q:
        *e = (struct osd_event){ OSD_TEXT, 0 };
        return 1;
    default:
        *made = 0;
        return 0;
    }
    e->action = OSD_TEXT_PAGE;
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

/* The TV player's keys (the list, PIN prompt and teletext have had theirs) */
static int tv_key_action(KeySym k, int repeat, struct osd_event *e)
{
    int shifted = O.shown.shift >= 1;
    int step = 0;
    switch (k) {
    case XK_Left:  if (shifted) { *e = (struct osd_event){ OSD_SEEK_REL, -10 }; return 1; } step = -1; break;
    case XK_Right: if (shifted) { *e = (struct osd_event){ OSD_SEEK_REL, 10 }; return 1; } step = 1; break;
    case XK_Page_Up: case XF86XK_AudioNext: step = 1; break;
    case XK_Page_Down: case XF86XK_AudioPrev: step = -1; break;
    case XF86XK_AudioRewind: case XK_comma: *e = (struct osd_event){ OSD_SEEK_REL, -10 }; return 1;
    case XF86XK_AudioForward: case XK_period: *e = (struct osd_event){ OSD_SEEK_REL, 10 }; return 1;
    case XK_End: *e = (struct osd_event){ OSD_LIVE, 0 }; return !repeat;
    case XF86XK_AudioStop:
        *e = (struct osd_event){ shifted ? OSD_LIVE : OSD_QUIT, 0 };
        return !repeat;
    case XK_space: case XK_p: case XF86XK_AudioPlay: case XF86XK_AudioPause:
        *e = (struct osd_event){ OSD_PAUSE, 0 };
        return !repeat;
    case XK_i: case XF86XK_Tools:
        if (!repeat)
            O.info_on ? info_hide() : info_show();
        return 0;
    case XK_t: case XF86XK_Launch5: *e = (struct osd_event){ OSD_TEXT, 0 }; return !repeat;
    case XK_a: case XF86XK_Launch3: *e = (struct osd_event){ OSD_AUDIO, 0 }; return !repeat;
    case XK_s: case XF86XK_Launch2: *e = (struct osd_event){ OSD_SUBS, 0 }; return !repeat;
    case XK_z: case XF86XK_Launch4: *e = (struct osd_event){ OSD_ASPECT, 0 }; return !repeat;
    case XK_f: case XF86XK_Launch1: *e = (struct osd_event){ OSD_GROUP, 0 }; return !repeat;
    case XK_r: case XF86XK_AudioRecord: *e = (struct osd_event){ OSD_RECORD, 0 }; return !repeat;
    case XK_v: case XF86XK_Launch9: *e = (struct osd_event){ OSD_RADIO, 0 }; return !repeat;
    case XK_q: case XK_Escape: case XK_BackSpace: case XF86XK_Back:
        if (O.info_on) {
            info_hide();
            return 0;
        }
        *e = (struct osd_event){ OSD_QUIT, 0 };
        return !repeat;
    default:
        return key_action(k, repeat, e);
    }
    if (!O.nch)
        return 0;
    *e = (struct osd_event){ OSD_CHANNEL, channel_step(step) };
    return 1;
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
                if (O.tv && O.nch && (i == HIT_BACK || i == HIT_FWD))
                    *e = (struct osd_event){ OSD_CHANNEL, channel_step(i == HIT_FWD ? 1 : -1) };
                return 1;
            }
        return 0;
    }
    if (b->window == O.info) {
        info_hide();
        return 0;
    }
    /* a click on the picture itself */
    *e = (struct osd_event){ OSD_PAUSE, 0 };
    return 1;
}

int osd_poll(struct osd_event *ev, int max)
{
    int n = 0;
    if (O.digits && now_ms() - O.digits_at > DIGITS_AFTER) {
        int typed = O.digits;
        for (int i = 0; i < O.nch && n < max; i++)
            if (atoi(O.ch[i].num) == typed && i != O.cur)
                ev[n++] = (struct osd_event){ OSD_CHANNEL, i };
        list_hide();
    }
    if (O.msg_text[0] && O.msg_until && now_ms() >= O.msg_until) {
        O.msg_text[0] = 0;
        O.msg_until = 0;
    }
    while (XPending(O.dpy)) {
        XEvent x;
        XNextEvent(O.dpy, &x);
        switch (x.type) {
        case KeyPress: {
            unsigned kc = x.xkey.keycode & 255;
            int repeat = O.held[kc];
            O.held[kc] = 1;
            if (!O.ttx)
                show();
            KeySym k = XLookupKeysym(&x.xkey, 0);
            if (k >= XK_KP_0 && k <= XK_KP_9)
                k = XK_0 + (k - XK_KP_0);
            struct osd_event *e = &ev[n < max ? n : max - 1];
            int made;
            if (O.pin_on && !repeat && pin_key(k, e, &made)) {
                n += made && n < max;
                break;
            }
            if (O.ttx && ttx_key(k, e, &made)) {
                n += made && n < max && !(repeat && e->action == OSD_TEXT);
                break;
            }
            if (O.nch && !repeat && k >= XK_0 && k <= XK_9) {
                digit_key(k - XK_0);
                break;
            }
            if (O.list_open && list_key(k, e, &made)) {
                n += made && n < max;
                break;
            }
            if (O.nch && !O.list_open && (k == XK_Return || k == XK_KP_Enter || k == XK_l || k == XF86XK_Launch6)) {
                list_show();
                break;
            }
            if (n < max && (O.tv ? tv_key_action(k, repeat, &ev[n]) : key_action(k, repeat, &ev[n])))
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
            if (x.xexpose.window == O.win) {
                if (x.xexpose.count == 0) {
                    draw_radio();
                    for (int i = 0; i < 2; i++)
                        if (O.img[i].im)
                            XPutImage(O.dpy, O.win, O.gc, O.img[i].im, 0, 0, O.img[i].x, O.img[i].y,
                                      O.img[i].w, O.img[i].h);
                }
                break;
            }
            O.sig[0] = 0;
            O.list_dirty = O.info_dirty = O.msg_dirty = 1;
            break;
        }
    }
    if (O.visible) {
        draw(&O.shown);
        draw_list();
        draw_info();
    }
    draw_msg();
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
