/* On-screen controls for amplay, drawn by X on the desktop layer above the video layer */
#ifndef OSD_H
#define OSD_H

#include <stdint.h>

struct osd_state {
    double pos, dur;          /* seconds; dur <= 0 if unknown */
    int paused, loading;
    int live;                 /* TV: no timeline, back/forward switch channels */
    double shift;             /* TV: seconds behind the broadcast (timeshift), 0 when live */
    int recording;            /* TV */
    const char *audio, *subs; /* TV: the tracks for the bottom bar, NULL if none */
    int signal;               /* TV: signal quality %, -1 if unknown */
    int volume, muted;        /* percent */
    const char *title;
};

enum osd_action {
    OSD_PAUSE, OSD_SEEK_REL, OSD_SEEK_ABS, OSD_VOLUME, OSD_MUTE, OSD_QUIT,
    /* TV */
    OSD_CHANNEL,              /* value: the index in the list */
    OSD_AUDIO, OSD_SUBS, OSD_ASPECT, OSD_RECORD, OSD_GROUP, OSD_RADIO,
    OSD_LIVE,                 /* back to the live picture */
    OSD_PIN,                  /* value: the four digits typed */
    OSD_TEXT,                 /* teletext on / off */
    OSD_TEXT_PAGE,            /* value: a page (BCD, 0x100-0x899) or one of TTX_* */
};
#define TTX_NEXT  (-1)
#define TTX_PREV  (-2)
#define TTX_MIX   (-3)        /* the picture under the page, or not */
#define TTX_LINK  (-10)       /* -10..-13: the red, green, yellow, blue link */

struct osd_event {
    enum osd_action action;
    double value;             /* seconds for seeks, percent for volume, the list index for a channel */
};

/* Opens a full-screen window filled with the transparent color; 0 on success. tv: the TV player's
   keys (channels, tracks, teletext...) */
int osd_open(int tv);
int osd_fd(void);
int osd_width(void);
int osd_height(void);
/* The transparent color as RGB565, for /sys/class/graphics/fb0/color_key */
unsigned osd_key565(void);
/* Turns pending X input into actions, returns how many were stored */
int osd_poll(struct osd_event *ev, int max);
/* Redraws the bars when what they show changed; hides them when idle */
void osd_update(const struct osd_state *s);
/* How long the bars stay up after the last key */
void osd_set_hide_after(int ms);
void osd_close(void);

/* TV: the channel list of the side panel (Enter opens it), one "number\tname\tnow on\tflags" per
   line (flags: s = skipped by the next/previous channel keys), and the one playing; choosing
   another one gives OSD_CHANNEL */
void osd_set_channels(char **lines, int n, int current);
void osd_set_current(int index);
/* "12  Name", for the title while a channel is being tuned; NULL if there is no such line */
const char *osd_channel_title(int index, char *buf, int size);
/* A message in the middle of the screen, for seconds (0: until replaced); NULL or "" clears it */
void osd_message(const char *text, int seconds);
/* A PIN prompt in the middle (digits go to it, OSD_PIN once four are typed); NULL closes it */
void osd_pin(const char *prompt);
/* The info panel (the info key): the text ozone-tv gives, and lines of amplay's own */
void osd_info(const char *text);
void osd_info_extra(const char *text);
int osd_info_open(void);
/* Radio: the screen is the station's, not the video layer; NULL for a TV channel */
void osd_radio(const char *name, const char *now);
/* Teletext is up: digits, arrows and colors go to it (OSD_TEXT_PAGE) */
void osd_teletext(int on, int page);
/* RGBA pictures on the screen itself: layer 0 subtitles, 1 teletext. Scaled from w x h to dw x
   dh at x, y; pixels with alpha below 128 are transparent (the video shows) */
void osd_image(int layer, const uint8_t *rgba, int w, int h, int x, int y, int dw, int dh);
void osd_image_clear(int layer);

#endif
