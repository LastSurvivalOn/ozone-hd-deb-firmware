/* On-screen controls for amplay, drawn by X on the desktop layer above the video layer */
#ifndef OSD_H
#define OSD_H

struct osd_state {
    double pos, dur;          /* seconds; dur <= 0 if unknown */
    int paused, loading;
    int volume, muted;        /* percent */
    const char *title;
};

enum osd_action { OSD_PAUSE, OSD_SEEK_REL, OSD_SEEK_ABS, OSD_VOLUME, OSD_MUTE, OSD_QUIT };

struct osd_event {
    enum osd_action action;
    double value;             /* seconds for seeks, percent for volume */
};

/* Opens a full-screen window filled with the transparent color; 0 on success */
int osd_open(void);
int osd_fd(void);
/* The transparent color as RGB565, for /sys/class/graphics/fb0/color_key */
unsigned osd_key565(void);
/* Turns pending X input into actions, returns how many were stored */
int osd_poll(struct osd_event *ev, int max);
/* Redraws the bars when what they show changed; hides them when idle */
void osd_update(const struct osd_state *s);
void osd_close(void);

#endif
