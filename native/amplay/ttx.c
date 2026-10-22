/*
 * Teletext for amplay: the data units of DVB teletext PES packets (EN 300 472) go to libzvbi's
 * decoder, which keeps the pages; a page is rendered to RGBA when it is shown.
 */
#include "ttx.h"

#include <libzvbi.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static vbi_decoder *vbi;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static volatile unsigned char received[0x900];   /* by page number (BCD), since the last look */
static int seen;

static void on_page(vbi_event *ev, void *user)
{
    (void)user;
    unsigned pg = ev->ev.ttx_page.pgno;
    if (pg < 0x900)
        received[pg] = 1;
    seen = 1;
}

int ttx_init(void)
{
    vbi = vbi_decoder_new();
    if (!vbi)
        return -1;
    vbi_event_handler_register(vbi, VBI_EVENT_TTX_PAGE, on_page, NULL);
    return 0;
}

void ttx_region(int region)
{
    if (vbi)
        vbi_teletext_set_default_region(vbi, region);
}

void ttx_reset(void)
{
    if (!vbi)
        return;
    pthread_mutex_lock(&lock);
    vbi_channel_switched(vbi, 0);
    memset((void *)received, 0, sizeof received);
    seen = 0;
    pthread_mutex_unlock(&lock);
}

static unsigned char rev8(unsigned char b)
{
    b = (b & 0xf0) >> 4 | (b & 0x0f) << 4;
    b = (b & 0xcc) >> 2 | (b & 0x33) << 2;
    return (b & 0xaa) >> 1 | (b & 0x55) << 1;
}

void ttx_feed(const uint8_t *p, int len)
{
    /* data_identifier 0x10-0x1f: EBU data, then data units of 2 + 44 bytes: field parity and line,
       the framing code 0xe4, and the 42 bytes of a teletext packet, sent in reverse bit order */
    if (!vbi || len < 1 || p[0] < 0x10 || p[0] > 0x1f)
        return;
    vbi_sliced s[64];
    int n = 0;
    for (int i = 1; i + 2 <= len && n < 64;) {
        int id = p[i], ul = p[i + 1];
        if (i + 2 + ul > len)
            break;
        const uint8_t *u = p + i + 2;
        if ((id == 0x02 || id == 0x03) && ul == 0x2c && u[1] == 0xe4) {
            s[n].id = VBI_SLICED_TELETEXT_B;
            s[n].line = (u[0] & 0x1f) + (u[0] & 0x20 ? 0 : 313);
            for (int k = 0; k < 42; k++)
                s[n].data[k] = rev8(u[2 + k]);
            n++;
        }
        i += 2 + ul;
    }
    if (!n)
        return;
    /* time 0, as FFmpeg's decoder does: libzvbi takes calls further apart than a video frame for
       frames lost and starts over every time, and PES packets come a few frames' worth at once */
    pthread_mutex_lock(&lock);
    vbi_decode(vbi, s, n, 0);
    pthread_mutex_unlock(&lock);
}

int ttx_received(int page)
{
    if (page < 0 || page >= 0x900 || !received[page])
        return 0;
    received[page] = 0;
    return 1;
}

int ttx_any(void)
{
    return seen;
}

int ttx_page(int page, int mode, uint8_t **rgba, int *w, int *h, int links[4])
{
    if (!vbi)
        return 0;
    vbi_page pg;
    pthread_mutex_lock(&lock);
    int ok = vbi_fetch_vt_page(vbi, &pg, page, VBI_ANY_SUBNO, VBI_WST_LEVEL_2p5, 25, 1);
    pthread_mutex_unlock(&lock);
    if (!ok)
        return 0;
    if (mode == TTX_PAGE_MIX)   /* the text over the picture: no background */
        for (int i = 0; i < pg.rows * pg.columns; i++)
            if (pg.text[i].opacity == VBI_OPAQUE)
                pg.text[i].opacity = VBI_TRANSPARENT_FULL;
    *w = pg.columns * 12;
    *h = pg.rows * 10;
    *rgba = malloc((size_t)*w * *h * 4);
    if (*rgba)
        vbi_draw_vt_page_region(&pg, VBI_PIXFMT_RGBA32_LE, *rgba, *w * 4, 0, 0, pg.columns, pg.rows, 1, 1);
    for (int i = 0; i < 4; i++)
        links[i] = pg.nav_link[i].pgno;
    vbi_unref_page(&pg);
    return *rgba != NULL;
}

int ttx_step(int page, int dir)
{
    int dec = vbi_bcd2dec(page) + dir;
    if (dec > 899)
        dec = 100;
    if (dec < 100)
        dec = 899;
    return vbi_dec2bcd(dec);
}
