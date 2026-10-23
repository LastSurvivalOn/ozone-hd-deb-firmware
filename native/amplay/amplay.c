/*
 * amplay: video player for the S805 hardware decoder.
 *
 * The Meson8b video decoder is only reachable through Amlogic's amstream driver: the elementary
 * stream is written to /dev/amstream_vbuf (or _hevc) with a PTS checked in before each packet,
 * the decoder hands frames to the video layer by itself (vfm map "decoder ppmgr deinterlace
 * amvideo"), and the kernel's tsync shows each frame when its PTS reaches the system clock
 * (pcrscr). The CPU never touches a pixel, so 1080p plays at full frame rate.
 *
 * Demuxing and audio are done with FFmpeg: audio is decoded in software and played through ALSA,
 * and pcrscr is kept on the audio clock. The video layer sits under the desktop OSD layer (fb0):
 * under X, fb0's color key makes the player window transparent and osd.c draws the controls on
 * it; without X (DISPLAY unset) fb0 is blanked and keys come from the keyboards directly (evdev,
 * grabbed).
 *
 *   amplay [--audio URL] [--start SECONDS] [--title TEXT] [--program N] URL
 *                                                  URL: a file or anything FFmpeg opens; N: the
 *                                                  service of a broadcast TS (a recording)
 *   amplay --tv [--banner SECONDS]                 the TV player, see "TV" below
 *   amplay --control ... URL                       a file or stream played for another program (the
 *                                                  DLNA renderer): commands on stdin, one per line,
 *                                                  "pause", "play", "seek SECONDS", "quit"; once a
 *                                                  second "pos SECONDS DURATION playing|paused|loading"
 *                                                  on stdout
 *   amplay --reset                                 put the desktop back after a crash
 *
 * Keys: Space/P pause, Left/Right -/+10 s, PgDn/PgUp -/+60 s, Up/Down volume, M mute, Q/Esc quit.
 * Mouse (under X): click the progress bar to seek, the picture to pause, the wheel for volume.
 * Exit status 3 means the video codec has no hardware decoder (the caller can fall back to mpv).
 *
 * TV. amplay --tv stays up while ozone-tv switches channels, so a switch is the time to the next
 * keyframe, not a new player: no window, decoder or ALSA set up again, and no stream probing (2 MB
 * of the broadcast, 3 s). ozone-tv writes the tuned service's TS into a run of segment files
 * (SRC000000.ts, SRC000001.ts... SEG_BYTES each, the oldest deleted), and amplay reads them at
 * an offset, waiting at the end for more: the same files make the timeshift buffer, so live TV
 * can be paused and wound back. Before the data, amplay reads the service's PAT and PMT (given by
 * ozone-tv), so the streams are known at once; video starts at a keyframe.
 * Commands on stdin, one per line, fields separated by tabs:
 *   play  key=value...   a channel: src= off= (the segments and where the channel starts in them),
 *                        psi= (PAT and PMT sections in hex, comma between), pmt= (its PID),
 *                        v= (video PID, 0 for radio), a= (audio pid:lang,...), asel=, amode= (0
 *                        stereo, 1 left, 2 right), s= (subtitles pid:d|t:lang:page,...), ssel=
 *                        (-1 off), ttx= (teletext PID), ttxregion= (its default character
 *                        sets, see ttx.h), title=, now= (radio: what's on)
 *   stop TITLE           stop playing (a switch is on its way), show TITLE
 *   title TEXT | info TEXT (\n for new lines) | signal LOCKED STRENGTH QUALITY | rec 0|1
 *   message SECONDS TEXT (0: until replaced, empty text clears) | pin PROMPT (empty closes it)
 *   channels FILE INDEX  the list ("number\tname\tnow on\tflags" lines) | current INDEX
 *   buffer OFFSET        the oldest data still in the segments | aspect MODE | quit
 * What the viewer does goes to stdout: "channel N" (a switch, N from 0 in the list), "quit",
 * "audio PID MODE", "subs PID PAGE" (0 0: off), "aspect MODE", "pin NNNN", "record", "group",
 * "tvradio". Its keys are in osd.c.
 */
#define _GNU_SOURCE
#include <alsa/asoundlib.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>

#include "osd.h"
#include "ttx.h"

/* amstream ioctls, from Amlogic's include/linux/amlogic/amports/amstream.h (3.10 kernels) */
#define AMSTREAM_IOC_VFORMAT          _IOW('S', 0x04, int)
#define AMSTREAM_IOC_VID              _IOW('S', 0x06, int)
#define AMSTREAM_IOC_VB_STATUS        _IOR('S', 0x08, unsigned long)
#define AMSTREAM_IOC_SYSINFO          _IOW('S', 0x0a, int)
#define AMSTREAM_IOC_TSTAMP           _IOW('S', 0x0e, unsigned long)
#define AMSTREAM_IOC_PORT_INIT        _IO('S', 0x11)
#define AMSTREAM_IOC_TRICKMODE        _IOW('S', 0x12, unsigned long)
#define AMSTREAM_IOC_VPAUSE           _IOW('S', 0x17, int)
#define AMSTREAM_IOC_AVTHRESH         _IOW('S', 0x18, int)
#define AMSTREAM_IOC_SYNCTHRESH       _IOW('S', 0x19, int)
#define AMSTREAM_IOC_CLEAR_VIDEO      _IOW('S', 0x1f, int)
#define AMSTREAM_IOC_PCRSCR           _IOR('S', 0x42, unsigned long)
#define AMSTREAM_IOC_SET_PCRSCR       _IOW('S', 0x4a, unsigned long)
#define AMSTREAM_IOC_SET_FREERUN_MODE _IOW('S', 0x88, unsigned long)
#define AMSTREAM_IOC_SET_VSYNC_UPINT  _IOW('S', 0x89, unsigned long)

struct buf_status { int size, data_len, free_len; unsigned read_pointer, write_pointer; };
struct am_io_param { int data; int len; union { char buf[1]; struct buf_status status; }; };
struct dec_sysinfo {
    unsigned format, width, height, rate, extra, status, ratio;
    void *param;
    unsigned long long ratio64;
};

#define PTS_HZ          90000
#define AUDIO_RATE      48000
#define SYNC_TOLERANCE  (PTS_HZ / 25)   /* re-set pcrscr when it drifts more than 40 ms off audio */
#define PREROLL_BYTES   (512 * 1024)
#define PREROLL_PTS     (PTS_HZ / 2)    /* or half a second of video, whichever comes first */
#define I2S_BLOCK       16              /* frames: the I2S driver's buffer layout, see audio_thread */
#define BUFFER_LOW      (PTS_HZ / 2)    /* downloaded video ahead of the clock: hold the clock below */
#define BUFFER_HIGH     (3 * PTS_HZ)    /* and let it run again from here */
#define NET_CHUNK       (10 << 20)      /* bytes in one HTTP request, see struct chunked */
#define SEEK_SHORT      10
#define SEEK_LONG       60
#define SYS             "/sys/class/"
/* TV */
#define SEG_BYTES       (8 << 20)       /* the segment files of the live buffer */
#define AUDIO_CUSHION   (PTS_HZ * 3 / 10) /* broadcast audio queued before the clock starts */
#define NO_AUDIO_WAIT   1500000         /* us: audio that hasn't come by then is done without */
#define IDX_MAX         16384           /* timeshift: keyframes remembered, about 4 hours */
#define LIVE_SLACK      (5 * PTS_HZ / 2) /* nearer the broadcast than this counts as live */
#define MAX_TRACKS      8

struct codec_map {
    enum AVCodecID id;
    int vformat;              /* vformat_t */
    int dec_format;           /* VIDEO_DEC_FORMAT_* for SYSINFO */
    const char *dev, *bsf;
} CODECS[] = {
    { AV_CODEC_ID_H264,       2, 4, "/dev/amstream_vbuf", "h264_mp4toannexb" },
    { AV_CODEC_ID_HEVC,      11, 15, "/dev/amstream_hevc", "hevc_mp4toannexb" },
    { AV_CODEC_ID_MPEG2VIDEO, 0, 0, "/dev/amstream_vbuf", "null" },
    { AV_CODEC_ID_MPEG1VIDEO, 0, 0, "/dev/amstream_vbuf", "null" },
};

/* the video layer's aspect modes the TV key goes through, and their names */
static const struct { int mode; const char *name; } ASPECTS[] = {
    { 0, "Original" }, { 1, "Full screen" }, { 2, "4:3" }, { 3, "16:9" },
};

/* ---- packet queue -------------------------------------------------------------------------- */

struct pkt { AVPacket *p; int pid, codec; struct pkt *next; };
struct pq {
    pthread_mutex_t m;
    pthread_cond_t c;
    struct pkt *head, *tail;
    size_t bytes, max;
    int eof, abort;
};

static void pq_init(struct pq *q, size_t max)
{
    memset(q, 0, sizeof *q);
    pthread_mutex_init(&q->m, NULL);
    pthread_cond_init(&q->c, NULL);
    q->max = max;
}

/* pid and codec: what TV audio packets came from, for the decoder to follow a track change */
static int pq_put(struct pq *q, AVPacket *p, int pid, int codec)
{
    pthread_mutex_lock(&q->m);
    while (q->bytes >= q->max && !q->abort)
        pthread_cond_wait(&q->c, &q->m);
    if (q->abort) {
        pthread_mutex_unlock(&q->m);
        av_packet_free(&p);
        return -1;
    }
    struct pkt *n = malloc(sizeof *n);
    n->p = p;
    n->pid = pid;
    n->codec = codec;
    n->next = NULL;
    if (q->tail)
        q->tail->next = n;
    else
        q->head = n;
    q->tail = n;
    q->bytes += p->size + sizeof *n;
    pthread_cond_broadcast(&q->c);
    pthread_mutex_unlock(&q->m);
    return 0;
}

/* 1: got a packet, 0: end of stream, -1: aborted */
static int pq_get(struct pq *q, AVPacket **p, int *pid, int *codec)
{
    pthread_mutex_lock(&q->m);
    while (!q->head && !q->eof && !q->abort)
        pthread_cond_wait(&q->c, &q->m);
    int r = q->abort ? -1 : q->head ? 1 : 0;
    if (r == 1) {
        struct pkt *n = q->head;
        q->head = n->next;
        if (!q->head)
            q->tail = NULL;
        q->bytes -= n->p->size + sizeof *n;
        *p = n->p;
        if (pid)
            *pid = n->pid;
        if (codec)
            *codec = n->codec;
        free(n);
        pthread_cond_broadcast(&q->c);
    }
    pthread_mutex_unlock(&q->m);
    return r;
}

static void pq_set(struct pq *q, int *flag, int value)
{
    pthread_mutex_lock(&q->m);
    *flag = value;
    pthread_cond_broadcast(&q->c);
    pthread_mutex_unlock(&q->m);
}

static void pq_reset(struct pq *q)
{
    pthread_mutex_lock(&q->m);
    while (q->head) {
        struct pkt *n = q->head;
        q->head = n->next;
        av_packet_free(&n->p);
        free(n);
    }
    q->tail = NULL;
    q->bytes = 0;
    q->eof = q->abort = 0;
    pthread_mutex_unlock(&q->m);
}

static int pq_empty(struct pq *q)
{
    pthread_mutex_lock(&q->m);
    int e = !q->head;
    pthread_mutex_unlock(&q->m);
    return e;
}

/* ---- player state -------------------------------------------------------------------------- */

struct input {
    AVFormatContext *fmt;
    int vidx, aidx;           /* stream indexes routed from this input, -1 if none */
    pthread_t th;
};

/* TV: the live segments read from an offset, after the service's PAT and PMT (see the top) */
struct tsin {
    char src[400];
    int64_t base;             /* the offset this session started at */
    int64_t pos;              /* the next byte to read */
    int fd;
    int64_t seg;              /* the segment open on fd */
    int psi_at;               /* bytes of P.svc.psi read so far */
    int64_t edge_at;          /* us: when a read last had to wait for the writer */
};

struct track { int pid; char type; char lang[8]; int page; };

/* a decoded DVB subtitle, waiting for its time */
struct sub { int64_t start, end; uint8_t *rgba; int x, y, w, h, dispw, disph; char *text; int stream; };

/* files: a subtitle file next to the video, read whole at the start */
struct extsub { int64_t start, end; char *text; };
struct extfile { char name[64]; struct extsub *s; int n; };

static struct {
    struct input in[2];
    int nin;
    AVStream *vst, *ast;
    AVRational vtb, atb;      /* the video and audio timestamps' units */
    const struct codec_map *codec;
    AVBSFContext *bsf;
    AVCodecContext *actx;
    SwrContext *swr;
    snd_pcm_t *pcm;
    int can_pause, has_audio;

    struct pq vq, aq;
    pthread_t vth, ath;
    int vfd, cntl;            /* stream port, /dev/amvideo */

    /* session state, reset on every (re)start */
    volatile int stop, started, paused, video_done, audio_done;
    int user_paused, buffering; /* paused is either of them */
    int64_t first_vpts;       /* 90 kHz, -1 until the first packet */
    int64_t last_vpts;
    int64_t demux_vpts;       /* the latest video timestamp read from the input (under clk_m) */
    int64_t frame_dur;        /* 90 kHz, for streams without timestamps (raw .h264) */
    int no_pts;
    pthread_mutex_t clk_m;
    pthread_cond_t gate;
    int64_t aclock;           /* audio clock (90 kHz) when last measured, -1 if unknown */
    int64_t aclock_at;        /* CLOCK_MONOTONIC in us at that moment */
    int64_t wclock, wclock_at; /* the same for the wall clock used when there is no audio */
    int64_t ready_at, started_at, resumed_at;

    int64_t start_time;       /* 90 kHz: the stream's own start, so the clock shows 0:00 */
    double duration;          /* seconds, 0 if unknown */
    double last_pos;
    const char *title;
    int program;              /* --program (-1: any) */
    int exit_status;
    int x11;                  /* controls drawn under X, or evdev without */

    /* --tv */
    int tv, running;          /* a channel is playing (or starting) */
    struct {
        char src[400];
        int64_t off;
        int vpid, pmt;
        struct track a[MAX_TRACKS], s[MAX_TRACKS];
        int na, ns, asel, amode, ssel, ttx;
        uint8_t psi[188 * 24];
        int npsi;
        char title[512], now[256];
        volatile int apid;    /* the audio PID routed to the decoder */
    } svc;
    struct tsin tsin;
    pthread_t dth;
    int vgate;                /* the first keyframe came: from then on the session's packets go through */
    volatile int novideo;     /* the channel's video can't be decoded */
    int64_t gate_pts;
    int64_t first_apts, last_apts;   /* queued audio of the session (under clk_m) */
    int frame_out;            /* the kernel has the first frame */
    int64_t last_dpts;        /* the main stream's last timestamp read, for jumps in the timeline */
    int64_t jump_pts, jump_off; /* a timestamp far from it, where it was, and how many agree since */
    int jump_n;
    volatile int restart;     /* the timeline jumped: start again at restart_off */
    int64_t restart_off;
    int64_t play_at, audio_wait_since;
    struct { int64_t pts, off; } idx[IDX_MAX];   /* timeshift: keyframes and where they are */
    int nidx;
    int64_t live_pts, live_at;   /* the newest timestamp read at the live edge, and when (us) */
    volatile int64_t low_off;    /* the oldest offset still in the segments */
    int signal, recording, aspect;
    char audio_label[32], subs_label[32];
    AVCodecContext *sctx;     /* DVB subtitles */
    int spid_now;
    struct sub subq[64];
    int nsub, sub_shown;
    int64_t sub_end;
    pthread_mutex_t sub_m;
    int ttx_on, ttx_mode, ttx_pageno, ttx_drawn, ttx_links[4];

    /* sound output */
    int out_rate;             /* the device's frames a second: 48 kHz, or a passthrough stream's rate */
    int raw;                  /* passthrough: AC-3 / DTS to the receiver as IEC 61937, not decoded */
    AVFormatContext *spdif;   /* FFmpeg's spdif muxer makes the IEC 61937 bursts */
    uint8_t *rawbuf;
    int rawlen, rawcap;
    char *saved_mode;         /* the output mode before matching a video's frame rate */

    /* files: tracks, subtitles, chapters, repeat */
    struct input *ain;        /* the input the audio comes from */
    int aids[16], naids, aid; /* its audio streams, and the one playing */
    int switch_aid;           /* another audio track wanted at the next (re)start, -1 if none */
    int sids[16], nsids;      /* the video's subtitle streams */
    struct extfile ext[4];
    int next;                 /* subtitle files found */
    int ssel;                 /* -1 off, < nsids a stream, else ext[ssel - nsids] */
    volatile int sidx;        /* the subtitle stream decoded, -1 none */
    AVCodecContext *fsctx[16];   /* by place in sids: every subtitle stream is decoded, so a
                                  stream chosen while the demuxer is ahead has its lines */
    int ext_shown;            /* the subtitle file's line on the screen, -1 none */
    double chapters[256];
    int nchap;
    int repeat;               /* 0 off, 1 this file, 2 all the files (the caller plays the next) */
    int ended, report, control;
    char flags[48];

    volatile sig_atomic_t quit;
    int verbose;
} P = { .vfd = -1, .cntl = -1, .clk_m = PTHREAD_MUTEX_INITIALIZER, .gate = PTHREAD_COND_INITIALIZER,
        .sub_m = PTHREAD_MUTEX_INITIALIZER, .out_rate = AUDIO_RATE, .switch_aid = -1, .ssel = -1, .sidx = -1,
        .ext_shown = -1 };

static void logmsg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("amplay: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

/* TV: what the viewer did, for ozone-tv */
static void event(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    putchar('\n');
    fflush(stdout);
    va_end(ap);
}

static int64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

static int sysfs_write(const char *path, const char *value)
{
    FILE *f = fopen(path, "w");
    if (!f || fputs(value, f) < 0 || fclose(f)) {
        if (P.verbose)
            logmsg("%s: %s", path, strerror(errno));
        return -1;
    }
    return 0;
}

static int sysfs_int(const char *path)
{
    int v = -1;
    FILE *f = fopen(path, "r");
    if (f) {
        if (fscanf(f, "%d", &v) != 1)
            v = -1;
        fclose(f);
    }
    return v;
}

static int64_t to_pts90(int64_t ts, AVRational tb)
{
    return av_rescale_q(ts, tb, (AVRational){ 1, PTS_HZ });
}

/* ozone-display's settings (/etc/ozone/display: KEY=value lines) */
static void conf_get(const char *key, const char *def, char *out, size_t n)
{
    char line[256];
    size_t kl = strlen(key);
    snprintf(out, n, "%s", def);
    FILE *f = fopen("/etc/ozone/display", "r");
    while (f && fgets(line, sizeof line, f))
        if (!strncmp(line, key, kl) && line[kl] == '=') {
            line[strcspn(line, "\r\n")] = 0;
            snprintf(out, n, "%s", line + kl + 1);
        }
    if (f)
        fclose(f);
}

static int read_text(const char *path, char *buf, size_t n)
{
    FILE *f = fopen(path, "r");
    size_t got = f ? fread(buf, 1, n - 1, f) : 0;
    buf[got] = 0;
    if (f)
        fclose(f);
    while (got && (buf[got - 1] == '\n' || buf[got - 1] == ' '))
        buf[--got] = 0;
    return f ? 0 : -1;
}

/* ---- display ------------------------------------------------------------------------------- */

#define SET_MODE "/usr/local/lib/ozone/set-mode"

/* where the mode to go back to is kept, for amplay --reset after a crash */
static void saved_mode_path(char *buf, size_t n)
{
    snprintf(buf, n, "/tmp/amplay-mode-%u", (unsigned)getuid());
}

static void switch_mode(const char *mode)
{
    pid_t pid = fork();
    if (pid == 0) {
        int null = open("/dev/null", O_WRONLY);
        dup2(null, 1);
        execl(SET_MODE, SET_MODE, mode, (char *)NULL);
        _exit(127);
    }
    int st;
    if (pid > 0)
        waitpid(pid, &st, 0);
}

/* MATCH_RATE=1: the output's 50 Hz mode for 25 and 50 fps, its 60 Hz one for 30 and 60 (and 24 Hz
   if the TV has it), switched before the decoder opens and back at the end. Not the kernel's own
   frame-rate automation (policy_fr_auto), which oopses when a decoder closes. */
static void match_rate(double fps)
{
    char on[8], mode[32], cap[1024], want[40];
    conf_get("MATCH_RATE", "0", on, sizeof on);
    if (on[0] != '1' || fps <= 0 || read_text(SYS "display/mode", mode, sizeof mode) < 0)
        return;
    if (strncmp(mode, "1080", 4) && strncmp(mode, "720", 3))
        return;   /* analog, 576p, 480p: one rate each */
    char base[32];
    snprintf(base, sizeof base, "%s", mode);
    char *hz = strstr(base, "50hz");
    if (!hz)
        hz = strstr(base, "24hz");
    if (hz)
        *hz = 0;
    if ((fps > 24.5 && fps < 25.5) || (fps > 49 && fps < 51))
        snprintf(want, sizeof want, "%s50hz", base);
    else if ((fps > 29 && fps < 31) || (fps > 59 && fps < 61))
        snprintf(want, sizeof want, "%s", base);
    else if (fps > 23 && fps < 24.5)
        snprintf(want, sizeof want, "%s24hz", base);
    else
        return;
    read_text("/sys/class/amhdmitx/amhdmitx0/disp_cap", cap, sizeof cap);
    /* the TV's list, a mode a line, the native one with a "*" */
    int listed = 0;
    for (char *l = cap, *e; *l && !listed; l = e + (*e != 0)) {
        e = l + strcspn(l, "\n");
        int len = e - l;
        if (len && l[len - 1] == '*')
            len--;
        listed = len == (int)strlen(want) && !strncmp(l, want, len);
    }
    if (!listed || !strcmp(want, mode))
        return;
    char path[64];
    saved_mode_path(path, sizeof path);
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%s\n", mode);
        fclose(f);
    }
    P.saved_mode = strdup(mode);
    if (P.verbose)
        logmsg("%.3f fps: output %s -> %s", fps, mode, want);
    switch_mode(want);
}

/* the mode from before match_rate (also after a crash: amplay --reset) */
static void restore_mode(void)
{
    char path[64], mode[32];
    saved_mode_path(path, sizeof path);
    if (read_text(path, mode, sizeof mode) == 0 && mode[0]) {
        if (P.verbose)
            logmsg("output back to %s", mode);
        switch_mode(mode);
    }
    unlink(path);
    free(P.saved_mode);
    P.saved_mode = NULL;
}

static int fr_auto_off(void)
{
    char v[8] = "";
    FILE *f = fopen(SYS "tv/policy_fr_auto", "r");
    if (f) {
        if (!fgets(v, sizeof v, f))
            v[0] = 0;
        fclose(f);
    }
    return v[0] == '0' || (sysfs_write(SYS "tv/policy_fr_auto", "0") == 0);
}

static void set_aspect(int mode)
{
    char v[8];
    snprintf(v, sizeof v, "%d", mode);
    sysfs_write(SYS "video/screen_mode", v);
}

static void display_on(void)
{
    char mode[32] = "", axis[64], line[96] = "";
    FILE *f = fopen(SYS "display/mode", "r");
    if (f) {
        if (!fgets(mode, sizeof mode, f))
            mode[0] = 0;
        fclose(f);
    }
    int w = 1920, h = 1080;
    if (!strncmp(mode, "720p", 4))
        w = 1280, h = 720;
    else if (!strncmp(mode, "576", 3))
        w = 720, h = 576;
    else if (!strncmp(mode, "480", 3))
        w = 720, h = 480;
    snprintf(axis, sizeof axis, "0 0 %d %d", w - 1, h - 1);
    /* where the scaler puts the desktop: inside the edges on screens that cut them off
       (ozone-screen); the video goes into the same window. Reads "window axis is [x0 y0 x1 y1]". */
    f = fopen(SYS "graphics/fb0/window_axis", "r");
    if (f) {
        int x0, y0, x1, y1;
        const char *p = fgets(line, sizeof line, f) ? strchr(line, '[') : NULL;
        if (p && sscanf(p + 1, "%d %d %d %d", &x0, &y0, &x1, &y1) == 4 && x1 > x0 && y1 > y0)
            snprintf(axis, sizeof axis, "%d %d %d %d", x0, y0, x1, y1);
        fclose(f);
    }

    sysfs_write(SYS "video/axis", axis);
    set_aspect(P.aspect);
    /* TV: a channel's first picture on the screen as soon as it is decoded, not when the clock
       starts (the audio in hand, AUDIO_CUSHION), as the stock TV app does */
    sysfs_write(SYS "video/show_first_frame_nosync", P.tv ? "1" : "0");
    sysfs_write(SYS "tsync/enable", "1");
    if (P.x11) {
        /* the player window's color becomes transparent and the video layer shows through */
        char key[16];
        snprintf(key, sizeof key, "0x%x", osd_key565());
        sysfs_write(SYS "graphics/fb0/color_key", key);
        sysfs_write(SYS "graphics/fb0/enable_key", "1");
    } else {
        sysfs_write(SYS "graphics/fb0/blank", "1");   /* the desktop layer covers the video layer */
    }
}

static void display_off(void)
{
    sysfs_write(SYS "video/disable_video", "1");
    sysfs_write(SYS "video/screen_mode", "0");
    sysfs_write(SYS "video/show_first_frame_nosync", "0");
    sysfs_write(SYS "tsync/enable", "0");
    sysfs_write(SYS "graphics/fb0/enable_key", "0");
    sysfs_write(SYS "graphics/fb0/blank", "0");
}

/* ---- volume: the softvol "Master" control from /etc/asound.conf ---------------------------- */

static snd_mixer_t *mixer;
static snd_mixer_elem_t *master;
static int saved_volume = -1;   /* volume before mute; softvol has no mute switch */

static void mixer_open(void)
{
    snd_mixer_selem_id_t *id;
    snd_mixer_selem_id_alloca(&id);
    snd_mixer_selem_id_set_name(id, "Master");
    if (snd_mixer_open(&mixer, 0) < 0)
        return;
    if (snd_mixer_attach(mixer, "default") < 0 || snd_mixer_selem_register(mixer, NULL, NULL) < 0 ||
        snd_mixer_load(mixer) < 0 || !(master = snd_mixer_find_selem(mixer, id))) {
        snd_mixer_close(mixer);
        mixer = NULL;
        master = NULL;
    }
}

static int volume_get(void)
{
    long min, max, v;
    if (!master)
        return 0;
    snd_mixer_handle_events(mixer);
    snd_mixer_selem_get_playback_volume_range(master, &min, &max);
    snd_mixer_selem_get_playback_volume(master, 0, &v);
    return max > min ? (int)((v - min) * 100 / (max - min)) : 0;
}

static void volume_set(int pct)
{
    long min, max;
    if (!master)
        return;
    pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
    snd_mixer_selem_get_playback_volume_range(master, &min, &max);
    snd_mixer_selem_set_playback_volume_all(master, min + (max - min) * pct / 100);
}

static void volume_change(int delta)
{
    int v = saved_volume >= 0 ? saved_volume : volume_get();
    saved_volume = -1;
    volume_set(v + delta);
}

static void mute_toggle(void)
{
    if (saved_volume >= 0) {
        volume_set(saved_volume);
        saved_volume = -1;
    } else {
        saved_volume = volume_get();
        volume_set(0);
    }
}

/* ---- keyboards ----------------------------------------------------------------------------- */

#define MAX_KBD 16
static struct pollfd kbd[MAX_KBD + 1];   /* + stdin, for --control */
static int nkbd;

static int has_bit(const unsigned long *bits, int n)
{
    return bits[n / (8 * sizeof *bits)] >> (n % (8 * sizeof *bits)) & 1;
}

static void kbd_open(void)
{
    DIR *d = opendir("/dev/input");
    struct dirent *e;
    while (d && (e = readdir(d)) && nkbd < MAX_KBD) {
        if (strncmp(e->d_name, "event", 5))
            continue;
        char path[300], name[64] = "";
        unsigned long keys[KEY_CNT / (8 * sizeof(unsigned long)) + 1] = { 0 };
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        ioctl(fd, EVIOCGNAME(sizeof name), name);
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
        /* keyboards and remotes; ozone-kbd only mirrors the physical keyboards grabbed here */
        if (!strcmp(name, "ozone-kbd") || !(has_bit(keys, KEY_SPACE) || has_bit(keys, KEY_POWER) ||
                                            has_bit(keys, KEY_PLAYPAUSE) || has_bit(keys, KEY_LEFT))) {
            close(fd);
            continue;
        }
        ioctl(fd, EVIOCGRAB, 1);
        kbd[nkbd++] = (struct pollfd){ .fd = fd, .events = POLLIN };
    }
    if (d)
        closedir(d);
}

static void kbd_close(void)
{
    for (int i = 0; i < nkbd; i++)
        close(kbd[i].fd);   /* closing releases the grab */
    nkbd = 0;
}

/* ---- TV input: the live segments --------------------------------------------------------- */

static int tsin_read(void *opaque, uint8_t *buf, int len)
{
    struct tsin *t = opaque;
    if (t->psi_at < P.svc.npsi) {   /* the service's PAT and PMT first */
        int n = FFMIN(len, P.svc.npsi - t->psi_at);
        memcpy(buf, P.svc.psi + t->psi_at, n);
        t->psi_at += n;
        return n;
    }
    while (!P.stop && !P.quit) {
        int64_t seg = t->pos / SEG_BYTES;
        if (t->fd < 0 || t->seg != seg) {
            if (t->fd >= 0)
                close(t->fd);
            char path[440];
            snprintf(path, sizeof path, "%s%06" PRId64 ".ts", t->src, seg);
            t->fd = open(path, O_RDONLY | O_CLOEXEC);
            t->seg = seg;
            if (t->fd < 0) {
                if (t->pos < P.low_off) {   /* paused for longer than the buffer holds */
                    logmsg("timeshift: %.1f MB behind the buffer, on from its start", (P.low_off - t->pos) / 1e6);
                    t->pos = P.low_off;
                    continue;
                }
                t->edge_at = now_us();   /* not written yet */
                usleep(20000);
                continue;
            }
        }
        int64_t in = t->pos % SEG_BYTES;
        ssize_t n = pread(t->fd, buf, FFMIN(len, SEG_BYTES - in), in);
        if (n > 0) {
            t->pos += n;
            return n;
        }
        t->edge_at = now_us();   /* the newest data: wait for more */
        usleep(20000);
    }
    return AVERROR_EXIT;
}

/* A PSI section (bytes, hex) as TS packets on pid, appended to P.svc.psi */
static void psi_add(int pid, const char *hex)
{
    uint8_t sec[1024];
    int len = 0;
    for (; hex[0] && hex[1] && hex[0] != ',' && len < (int)sizeof sec; hex += 2) {
        unsigned b;
        if (sscanf(hex, "%2x", &b) != 1)
            break;
        sec[len++] = b;
    }
    int cc = 0, first = 1, at = 0;
    while ((at < len || first) && P.svc.npsi + 188 <= (int)sizeof P.svc.psi) {
        uint8_t *p = P.svc.psi + P.svc.npsi;
        p[0] = 0x47;
        p[1] = (first ? 0x40 : 0) | (pid >> 8 & 0x1f);
        p[2] = pid & 0xff;
        p[3] = 0x10 | (cc++ & 15);
        int h = 4;
        if (first)
            p[h++] = 0;   /* pointer_field */
        int k = FFMIN(len - at, 188 - h);
        memcpy(p + h, sec + at, k);
        memset(p + h + k, 0xff, 188 - h - k);
        at += k;
        P.svc.npsi += 188;
        first = 0;
    }
}

/* ---- demux, video and audio threads -------------------------------------------------------- */

static int interrupt_cb(void *arg)
{
    (void)arg;
    /* not on seek: an interrupted read leaves the AVIOContext in an error state (TV closes it) */
    return P.quit || (P.tv && P.stop);
}

static void file_sub_decode(AVStream *st, AVPacket *pkt);

static void *demux_thread(void *arg)
{
    struct input *in = arg;
    AVPacket *pkt = av_packet_alloc();
    while (!P.stop) {
        int r = av_read_frame(in->fmt, pkt);
        if (r == AVERROR(EAGAIN))
            continue;
        if (r < 0) {
            if (r != AVERROR_EOF && !P.stop)
                logmsg("read error: %s", av_err2str(r));
            break;
        }
        if (in == &P.in[0] && P.nsids && in->fmt->streams[pkt->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            file_sub_decode(in->fmt->streams[pkt->stream_index], pkt);
        }
        struct pq *q = pkt->stream_index == in->vidx ? &P.vq : pkt->stream_index == in->aidx ? &P.aq : NULL;
        int64_t ts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
        if (q == &P.vq && ts != AV_NOPTS_VALUE) {
            int64_t pts = to_pts90(ts, P.vtb);
            pthread_mutex_lock(&P.clk_m);
            if (pts > P.demux_vpts)
                P.demux_vpts = pts;
            pthread_mutex_unlock(&P.clk_m);
        }
        if (q && pq_put(q, av_packet_clone(pkt), 0, 0) < 0)
            break;
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    if (in->vidx >= 0)
        pq_set(&P.vq, &P.vq.eof, 1);
    if (in->aidx >= 0)
        pq_set(&P.aq, &P.aq.eof, 1);
    return NULL;
}

static int feed(AVPacket *pkt)
{
    int64_t ts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
    if (P.first_vpts < 0 && ts == AV_NOPTS_VALUE)
        P.no_pts = 1;
    if (ts != AV_NOPTS_VALUE || P.no_pts) {
        int64_t pts = P.no_pts ? (P.last_vpts < 0 ? 0 : P.last_vpts + P.frame_dur) : to_pts90(ts, P.vtb);
        if (P.first_vpts < 0)
            P.first_vpts = pts;
        P.last_vpts = pts;
        ioctl(P.vfd, AMSTREAM_IOC_TSTAMP, (unsigned long)(uint32_t)pts);
    }
    for (int off = 0; off < pkt->size && !P.stop;) {
        ssize_t n = write(P.vfd, pkt->data + off, pkt->size - off);
        if (n > 0) {
            off += n;
        } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
            logmsg("decoder write: %s", strerror(errno));
            return -1;
        } else {
            usleep(10000);   /* stream buffer full */
        }
    }
    return 0;
}

static void *video_thread(void *arg)
{
    (void)arg;
    AVPacket *pkt, *out = av_packet_alloc();
    int r;
    while ((r = pq_get(&P.vq, &pkt, NULL, NULL)) > 0) {
        if (P.tv) {   /* a broadcast's video is Annex B already */
            int bad = P.vfd < 0 || feed(pkt);
            av_packet_free(&pkt);
            if (bad)
                break;
            continue;
        }
        av_bsf_send_packet(P.bsf, pkt);
        av_packet_free(&pkt);
        while (av_bsf_receive_packet(P.bsf, out) == 0) {
            int bad = feed(out);
            av_packet_unref(out);
            if (bad)
                goto done;
        }
    }
done:
    av_packet_free(&out);
    P.video_done = 1;
    return NULL;
}

static void audio_clock_update(int64_t end_pts)
{
    snd_pcm_sframes_t delay = 0;
    if (snd_pcm_delay(P.pcm, &delay) < 0)
        delay = 0;
    pthread_mutex_lock(&P.clk_m);
    P.aclock = end_pts - (int64_t)delay * PTS_HZ / P.out_rate;
    P.aclock_at = now_us();
    pthread_mutex_unlock(&P.clk_m);
}

static int64_t audio_clock_now(void)
{
    pthread_mutex_lock(&P.clk_m);
    int64_t c = P.aclock;
    if (c >= 0 && !P.paused)
        c += (now_us() - P.aclock_at) * PTS_HZ / 1000000;
    pthread_mutex_unlock(&P.clk_m);
    return c;
}

/* What pcrscr should be now: the audio clock, or without audio (or once it ended) the wall clock
   since the start */
static int64_t master_clock(void)
{
    int64_t a = P.has_audio && !P.audio_done ? audio_clock_now() : -1;
    if (a >= 0) {
        P.wclock = a;
        P.wclock_at = now_us();
        return a;
    }
    if (P.wclock < 0)
        return -1;
    return P.wclock + (P.paused ? 0 : (now_us() - P.wclock_at) * PTS_HZ / 1000000);
}

static void audio_pause_wait(void)
{
    if (!P.paused)
        return;
    if (P.can_pause)
        snd_pcm_pause(P.pcm, 1);
    else
        snd_pcm_drop(P.pcm);
    pthread_mutex_lock(&P.clk_m);
    while (P.paused && !P.stop)
        pthread_cond_wait(&P.gate, &P.clk_m);
    P.aclock_at = now_us();
    pthread_mutex_unlock(&P.clk_m);
    if (P.can_pause)
        snd_pcm_pause(P.pcm, 0);
    else
        snd_pcm_prepare(P.pcm);
}

static int open_pcm_dev(const char *dev, int rate)
{
    /* Plain blocking writes, like aplay. Never ALSA's silence filling: with it the Amlogic HDMI
       audio driver hangs the kernel (the reason mpv uses ao=sdl here). */
    int r = snd_pcm_open(&P.pcm, dev, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if (r >= 0)
        r = snd_pcm_set_params(P.pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, rate,
                               strcmp(dev, "default") == 0, 200000);
    if (r < 0) {
        logmsg("audio device %s: %s", dev, snd_strerror(r));
        if (P.pcm)
            snd_pcm_close(P.pcm);
        P.pcm = NULL;
        return -1;
    }
    P.out_rate = rate;
    snd_pcm_hw_params_t *hw;
    snd_pcm_hw_params_alloca(&hw);
    P.can_pause = snd_pcm_hw_params_current(P.pcm, hw) == 0 && snd_pcm_hw_params_can_pause(hw);
    return 0;
}

static int open_pcm(void)
{
    return open_pcm_dev("default", AUDIO_RATE);
}

/* The decoder for a codec, and the resampler to 48 kHz stereo once its first frame says what it
   gives (TV: a track's sample rate is only known from its frames) */
static int audio_decoder(enum AVCodecID id, const AVCodecParameters *par)
{
    avcodec_free_context(&P.actx);
    swr_free(&P.swr);
    const AVCodec *dec = avcodec_find_decoder(id);
    P.actx = avcodec_alloc_context3(dec);
    if (!dec || (par && avcodec_parameters_to_context(P.actx, par) < 0) || avcodec_open2(P.actx, dec, NULL) < 0) {
        logmsg("no audio decoder for %s", avcodec_get_name(id));
        avcodec_free_context(&P.actx);
        return -1;
    }
    return 0;
}

static int resampler(const AVFrame *f)
{
    swr_free(&P.swr);
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO, in = f->ch_layout;
    if (!in.nb_channels)
        av_channel_layout_default(&in, 2);
    if (swr_alloc_set_opts2(&P.swr, &stereo, AV_SAMPLE_FMT_S16, AUDIO_RATE, &in, f->format, f->sample_rate,
                            0, NULL) < 0 || swr_init(P.swr) < 0) {
        logmsg("audio resampler setup failed");
        swr_free(&P.swr);
        return -1;
    }
    return 0;
}

/* The Amlogic I2S driver keeps the buffer in blocks of 16 frames (16 left samples, then 16 right)
   and converts each write in place: one that doesn't start and end on a block boundary lands
   shifted, and the TV gets noise or nothing (dmesg: "audio data unligned"). So whole blocks of
   buf's n frames only; returns how many frames that was, the rest waits for more. */
static int pcm_write_blocks(uint8_t *buf, int n)
{
    int whole = n / I2S_BLOCK * I2S_BLOCK;
    for (int off = 0; off < whole && !P.stop;) {
        audio_pause_wait();
        snd_pcm_sframes_t room = snd_pcm_avail_update(P.pcm);
        int len = whole - off;
        if (room >= 0 && room < I2S_BLOCK) {
            snd_pcm_wait(P.pcm, 100);
            continue;
        }
        if (room > 0 && len > room)
            len = room / I2S_BLOCK * I2S_BLOCK;
        snd_pcm_sframes_t w = snd_pcm_writei(P.pcm, buf + off * 4, len);
        if (w == -EAGAIN) {
            snd_pcm_wait(P.pcm, 100);
            continue;
        }
        if (w < 0) {
            if (snd_pcm_recover(P.pcm, w, 1) < 0) {
                logmsg("audio: %s", snd_strerror(w));
                break;
            }
            continue;
        }
        off += w;
    }
    return whole;
}

/* ---- passthrough: AC-3 / DTS as they are, for an AV receiver ---------------------------- */

static int raw_write(void *opaque, uint8_t *buf, int size)
{
    (void)opaque;
    if (P.rawlen + size > P.rawcap) {
        int cap = (P.rawlen + size) * 2;
        uint8_t *n = realloc(P.rawbuf, cap);
        if (!n)
            return AVERROR(ENOMEM);
        P.rawbuf = n;
        P.rawcap = cap;
    }
    memcpy(P.rawbuf + P.rawlen, buf, size);
    P.rawlen += size;
    return size;
}

/* PASSTHROUGH=on, or auto and the TV's (or receiver's) EDID lists the codec */
static int raw_wanted(enum AVCodecID id)
{
    char mode[16], cap[2048];
    if (id != AV_CODEC_ID_AC3 && id != AV_CODEC_ID_DTS)
        return 0;
    conf_get("PASSTHROUGH", "off", mode, sizeof mode);
    if (!strcmp(mode, "on"))
        return 1;
    if (strcmp(mode, "auto") || read_text("/sys/class/amhdmitx/amhdmitx0/aud_cap", cap, sizeof cap) < 0)
        return 0;
    return strstr(cap, id == AV_CODEC_ID_AC3 ? "AC-3" : "DTS") != NULL;
}

static void raw_close(void)
{
    if (!P.spdif)
        return;
    av_write_trailer(P.spdif);
    av_freep(&P.spdif->pb->buffer);
    avio_context_free(&P.spdif->pb);
    avformat_free_context(P.spdif);
    P.spdif = NULL;
    P.raw = 0;
    sysfs_write(SYS "audiodsp/digital_raw", "0");
    sysfs_write(SYS "audiodsp/digital_codec", "0");
}

/* The bursts go to the HDMI device itself: softvol and the equalizer would change the bits */
static int raw_open(void)
{
    const AVOutputFormat *of = av_guess_format("spdif", NULL, NULL);
    if (!of || avformat_alloc_output_context2(&P.spdif, of, NULL, NULL) < 0)
        return -1;
    AVStream *st = avformat_new_stream(P.spdif, NULL);
    if (!st || avcodec_parameters_copy(st->codecpar, P.ast->codecpar) < 0)
        goto fail;
    st->time_base = P.ast->time_base;
    P.spdif->pb = avio_alloc_context(av_malloc(4096), 4096, 1, NULL, NULL, raw_write, NULL);
    if (!P.spdif->pb || avformat_write_header(P.spdif, NULL) < 0)
        goto fail;
    /* before the device opens, when the HDMI transmitter sets up its audio: the stream, not PCM
       (digital_codec as Amlogic's audio HAL numbers them: 2 AC-3, 3 DTS) */
    sysfs_write(SYS "audiodsp/digital_codec", P.ast->codecpar->codec_id == AV_CODEC_ID_AC3 ? "2" : "3");
    sysfs_write(SYS "audiodsp/digital_raw", "2");
    if (open_pcm_dev("hw:0,1", P.ast->codecpar->sample_rate) < 0) {
        sysfs_write(SYS "audiodsp/digital_raw", "0");
        sysfs_write(SYS "audiodsp/digital_codec", "0");
        goto fail;
    }
    P.raw = 1;
    logmsg("audio %s passed through to HDMI", avcodec_get_name(P.ast->codecpar->codec_id));
    return 0;
fail:
    if (P.spdif && P.spdif->pb) {
        av_freep(&P.spdif->pb->buffer);
        avio_context_free(&P.spdif->pb);
    }
    avformat_free_context(P.spdif);
    P.spdif = NULL;
    return -1;
}

static void *audio_thread(void *arg)
{
    (void)arg;
    AVPacket *pkt;
    AVFrame *frame = av_frame_alloc();
    uint8_t *buf = NULL;
    int bufsamples = 0;
    int carry = 0;   /* frames at the start of buf left over from the last write, < I2S_BLOCK */
    int in_rate = 0, in_format = -1, in_channels = 0;
    int pid, codec, dec_pid = -1;

    /* hold back until the video decoder has a head start, so both begin together */
    pthread_mutex_lock(&P.clk_m);
    while (!P.started && !P.stop)
        pthread_cond_wait(&P.gate, &P.clk_m);
    pthread_mutex_unlock(&P.clk_m);

    while (!P.stop && pq_get(&P.aq, &pkt, &pid, &codec) > 0) {
        if (P.raw) {
            int64_t ts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
            int64_t pts = ts != AV_NOPTS_VALUE ? to_pts90(ts, P.atb) : -1;
            /* the muxer wants times that only go up, and after a seek they would go back: its own
               count instead (the bursts carry no times anyway) */
            static int64_t raw_ts;
            int64_t dur = pkt->duration > 0 ? pkt->duration : 1;
            av_packet_rescale_ts(pkt, P.atb, P.spdif->streams[0]->time_base);
            pkt->pts = pkt->dts = raw_ts;
            pkt->stream_index = 0;   /* the muxer's one stream */
            raw_ts += av_rescale_q(dur, P.atb, P.spdif->streams[0]->time_base) + 1;
            P.rawlen = 0;
            int wr = av_write_frame(P.spdif, pkt);
            if (wr < 0 && P.verbose)
                logmsg("passthrough: a packet lost (%s)", av_err2str(wr));
            avio_flush(P.spdif->pb);
            av_packet_free(&pkt);
            int frames = P.rawlen / 4;
            if (carry + frames > bufsamples) {
                uint8_t *nbuf = av_malloc((size_t)(carry + frames) * 4);
                if (buf)
                    memcpy(nbuf, buf, carry * 4);
                av_freep(&buf);
                buf = nbuf;
                bufsamples = carry + frames;
            }
            memcpy(buf + carry * 4, P.rawbuf, (size_t)frames * 4);
            int n = carry + frames, whole = pcm_write_blocks(buf, n);
            carry = n - whole;
            memmove(buf, buf + whole * 4, carry * 4);
            if (pts >= 0)
                audio_clock_update(pts + (int64_t)(frames - carry) * PTS_HZ / P.out_rate);
            continue;
        }
        if (P.tv) {
            if (pid != P.svc.apid) {   /* the track it was on before a change */
                av_packet_free(&pkt);
                continue;
            }
            if (pid != dec_pid) {
                dec_pid = pid;
                in_rate = 0;
                if (audio_decoder(codec, NULL) < 0)
                    dec_pid = -1;
            }
            if (!P.actx) {
                av_packet_free(&pkt);
                continue;
            }
        }
        if (avcodec_send_packet(P.actx, pkt) < 0 && P.verbose)
            logmsg("audio decode error");
        av_packet_free(&pkt);
        while (!P.stop && avcodec_receive_frame(P.actx, frame) == 0) {
            if (!P.swr || frame->sample_rate != in_rate || frame->format != in_format ||
                frame->ch_layout.nb_channels != in_channels) {
                in_rate = frame->sample_rate;
                in_format = frame->format;
                in_channels = frame->ch_layout.nb_channels;
                if (!in_rate || resampler(frame) < 0) {
                    av_frame_unref(frame);
                    continue;
                }
            }
            int64_t ts = frame->best_effort_timestamp;
            int64_t pts = ts != AV_NOPTS_VALUE ? to_pts90(ts, P.atb) : P.aclock;
            int max = swr_get_out_samples(P.swr, frame->nb_samples);
            if (carry + max > bufsamples) {
                uint8_t *nbuf = NULL;
                av_samples_alloc(&nbuf, NULL, 2, carry + max, AV_SAMPLE_FMT_S16, 0);
                if (buf)
                    memcpy(nbuf, buf, carry * 4);
                av_freep(&buf);
                buf = nbuf;
                bufsamples = carry + max;
            }
            uint8_t *out = buf + carry * 4;
            int got = swr_convert(P.swr, &out, max, (const uint8_t **)frame->extended_data, frame->nb_samples);
            if (got > 0 && P.tv && P.svc.amode) {   /* one side on both: dual-mono broadcasts */
                int16_t *s = (int16_t *)out;
                for (int i = 0; i < got; i++)
                    s[2 * i + (P.svc.amode == 1)] = s[2 * i + (P.svc.amode == 2)];
            }
            int n = carry + (got > 0 ? got : 0);
            int64_t end = pts + (int64_t)frame->nb_samples * PTS_HZ / frame->sample_rate;
            int whole = pcm_write_blocks(buf, n);
            carry = n - whole;
            memmove(buf, buf + whole * 4, carry * 4);
            audio_clock_update(end - (int64_t)carry * PTS_HZ / P.out_rate);
            av_frame_unref(frame);
        }
    }
    /* let the device play out (the pcm is non-blocking, so no snd_pcm_drain) */
    snd_pcm_sframes_t left;
    while (!P.stop && snd_pcm_delay(P.pcm, &left) == 0 && left > 0) {
        usleep(50000);
        audio_clock_update(P.aclock + (int64_t)left * PTS_HZ / P.out_rate);
    }
    av_freep(&buf);
    av_frame_free(&frame);
    P.audio_done = 1;
    return NULL;
}

/* ---- session: one continuous run of the decoder, restarted on every seek ------------------- */

static int decoder_open(const struct codec_map *c, int width, int height, AVRational fr)
{
    P.vfd = open(c->dev, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    P.cntl = open("/dev/amvideo", O_RDWR | O_CLOEXEC);
    if (P.vfd < 0 || P.cntl < 0) {
        logmsg("%s: %s", P.vfd < 0 ? c->dev : "/dev/amvideo", strerror(errno));
        if (P.vfd >= 0)
            close(P.vfd);
        if (P.cntl >= 0)
            close(P.cntl);
        P.vfd = P.cntl = -1;
        return -1;
    }
    struct dec_sysinfo si = {
        .format = c->dec_format,
        .width = width,
        .height = height,
        /* frame duration in 1/96000 s */
        .rate = fr.num ? (unsigned)av_rescale(96000, fr.den, fr.num) : 0,
    };
    P.frame_dur = fr.num ? av_rescale(PTS_HZ, fr.den, fr.num) : PTS_HZ / 25;
    P.no_pts = 0;
    if (ioctl(P.vfd, AMSTREAM_IOC_VFORMAT, c->vformat) || ioctl(P.vfd, AMSTREAM_IOC_VID, 1) ||
        ioctl(P.vfd, AMSTREAM_IOC_SYSINFO, (unsigned long)&si) || ioctl(P.vfd, AMSTREAM_IOC_PORT_INIT, 0)) {
        logmsg("decoder init: %s", strerror(errno));
        close(P.vfd);
        close(P.cntl);
        P.vfd = P.cntl = -1;
        return -1;
    }
    /* Port init puts tsync in audio master mode (writing tsync/mode doesn't stick), and the
       audio DSP that would drive it isn't used: sync_clock() keeps pcrscr on our own clock.
       The start-up slow sync only gets in the way of that. */
    sysfs_write(SYS "tsync/startsync_mode", "0");
    /* what the stock Android player sets on the video layer */
    ioctl(P.cntl, AMSTREAM_IOC_SET_FREERUN_MODE, 0);
    ioctl(P.cntl, AMSTREAM_IOC_SET_VSYNC_UPINT, 0);
    ioctl(P.cntl, AMSTREAM_IOC_TRICKMODE, 0);
    ioctl(P.cntl, AMSTREAM_IOC_AVTHRESH, 15 * PTS_HZ);
    ioctl(P.cntl, AMSTREAM_IOC_SYNCTHRESH, 1);
    return 0;
}

static void session_reset(void)
{
    P.stop = P.started = P.paused = P.user_paused = P.buffering = P.video_done = 0;
    P.audio_done = !P.has_audio;
    P.first_vpts = P.last_vpts = P.demux_vpts = -1;
    P.aclock = P.wclock = -1;
    P.ready_at = 0;
    sysfs_write(SYS "video/disable_video", "2");  /* off until the first new frame */
    pq_reset(&P.vq);
    pq_reset(&P.aq);
}

static int session_start(void)
{
    AVCodecParameters *par = P.vst->codecpar;
    session_reset();
    if (decoder_open(P.codec, par->width, par->height, av_guess_frame_rate(P.in[0].fmt, P.vst, NULL)) < 0)
        return -1;
    for (int i = 0; i < P.nin; i++)
        pthread_create(&P.in[i].th, NULL, demux_thread, &P.in[i]);
    pthread_create(&P.vth, NULL, video_thread, NULL);
    if (P.ast)
        pthread_create(&P.ath, NULL, audio_thread, NULL);
    return 0;
}

static void on_wake(int sig)
{
    (void)sig;   /* nothing: it only cuts a waiting read() short (see join_demuxer) */
}

/* A demuxer waiting in read() for data that may not come (a pipe): FFmpeg looks at the interrupt
   callback only between reads, so stopping would wait for the data. SIGUSR1 (no SA_RESTART) ends
   the read with EINTR, then the callback ends the demuxer. */
static void join_demuxer(pthread_t th)
{
    for (;;) {
        if (P.quit)
            pthread_kill(th, SIGUSR1);
        struct timespec t;
        clock_gettime(CLOCK_REALTIME, &t);
        t.tv_nsec += 100000000;
        if (t.tv_nsec >= 1000000000) {
            t.tv_sec++;
            t.tv_nsec -= 1000000000;
        }
        if (pthread_timedjoin_np(th, NULL, &t) == 0)
            return;
    }
}

static void session_stop(void)
{
    pthread_mutex_lock(&P.clk_m);
    P.stop = 1;
    pthread_cond_broadcast(&P.gate);
    pthread_mutex_unlock(&P.clk_m);
    pq_set(&P.vq, &P.vq.abort, 1);
    pq_set(&P.aq, &P.aq.abort, 1);
    if (P.tv) {
        join_demuxer(P.dth);
        if (P.svc.vpid)
            pthread_join(P.vth, NULL);
    } else {
        for (int i = 0; i < P.nin; i++)
            join_demuxer(P.in[i].th);
        pthread_join(P.vth, NULL);
    }
    if (P.has_audio) {
        pthread_join(P.ath, NULL);
        snd_pcm_drop(P.pcm);
        snd_pcm_prepare(P.pcm);
        if (P.actx)
            avcodec_flush_buffers(P.actx);
    }
    if (P.bsf)
        av_bsf_flush(P.bsf);
    if (P.vfd >= 0)
        close(P.vfd);
    if (P.cntl >= 0) {
        ioctl(P.cntl, AMSTREAM_IOC_VPAUSE, 0);
        close(P.cntl);
    }
    P.vfd = P.cntl = -1;
}

static int vbuf_level(void)
{
    struct am_io_param p = { 0 };
    if (P.vfd < 0 || ioctl(P.vfd, AMSTREAM_IOC_VB_STATUS, (unsigned long)&p))
        return -1;
    return p.status.data_len;
}

static int64_t position(void)
{
    int64_t c = master_clock();
    return c < 0 ? P.first_vpts : c;
}

/* The kernel has shown the first frame: it then sets pcrscr 3 s behind it (its audio-master
   start-up hold). Starting the clock before that makes the first frames late, and they get
   dropped. Given up on 2 s after the decoder had enough. */
static int first_frame_out(void)
{
    if (!P.ready_at)
        P.ready_at = now_us();
    unsigned long pcr = 0;
    ioctl(P.vfd, AMSTREAM_IOC_PCRSCR, (unsigned long)&pcr);
    int32_t d = (int32_t)((uint32_t)pcr - (uint32_t)P.first_vpts);
    if (d < -2 * PTS_HZ && d > -4 * PTS_HZ)
        return 1;
    if (now_us() - P.ready_at < 2000000)
        return 0;
    if (P.verbose)
        logmsg("no sign of the first frame in 2 s (pcrscr %+.3f s from it)", d / (double)PTS_HZ);
    return 1;
}

static void clock_start(int64_t at)
{
    if (P.vfd >= 0)
        ioctl(P.vfd, AMSTREAM_IOC_SET_PCRSCR, (unsigned long)(uint32_t)at);
    P.wclock = at;
    P.wclock_at = P.started_at = now_us();
    pthread_mutex_lock(&P.clk_m);
    P.started = 1;
    pthread_cond_broadcast(&P.gate);
    pthread_mutex_unlock(&P.clk_m);
    if (P.verbose)
        logmsg(P.tv ? "start at %.3f s, %.2f s after the switch" : "start at %.3f s", at / (double)PTS_HZ,
               (now_us() - P.play_at) / 1e6);
}

/* TV: the clock starts once the keyframe is on the screen and AUDIO_CUSHION of audio is queued
   (broadcast audio arrives in real time: without some in hand the device would run dry) */
static void tv_start(void)
{
    if (P.svc.vpid && !P.novideo) {
        if (P.vfd < 0 || P.first_vpts < 0)
            return;   /* no decoder or no keyframe yet */
        if (!P.frame_out && !(P.frame_out = first_frame_out()))
            return;
        if (P.verbose && !P.audio_wait_since)
            logmsg("first frame out after %.2f s", (now_us() - P.play_at) / 1e6);
    }
    int64_t fa = -1, la = -1;
    if (P.has_audio) {
        pthread_mutex_lock(&P.clk_m);
        fa = P.first_apts;
        la = P.last_apts;
        pthread_mutex_unlock(&P.clk_m);
        if (!P.audio_wait_since)
            P.audio_wait_since = now_us();
        if (fa < 0 || la - fa < AUDIO_CUSHION) {
            if (now_us() - P.audio_wait_since < NO_AUDIO_WAIT)
                return;
            if (fa < 0)
                logmsg("no audio after %.1f s", NO_AUDIO_WAIT / 1e6);
        }
    }
    clock_start(fa >= 0 ? fa : P.first_vpts >= 0 ? P.first_vpts : 0);
}

/* Called every 20-100 ms from the main loop */
static void sync_clock(void)
{
    if (!P.started) {
        if (P.tv) {
            tv_start();
            return;
        }
        int ready = P.video_done || vbuf_level() >= PREROLL_BYTES || P.last_vpts - P.first_vpts >= PREROLL_PTS;
        if (!ready || P.first_vpts < 0 || !first_frame_out())
            return;
        clock_start(P.first_vpts);
        return;
    }
    /* the kernel advances pcrscr every vsync; correct the drift against the audio clock */
    int64_t a = master_clock();
    if (a < 0 || P.paused || P.vfd < 0)
        return;
    unsigned long pcr = 0;
    ioctl(P.vfd, AMSTREAM_IOC_PCRSCR, (unsigned long)&pcr);
    int32_t diff = (int32_t)((uint32_t)pcr - (uint32_t)a);   /* pcrscr is 32 bit and may sit just below 0 */
    if (abs(diff) > SYNC_TOLERANCE) {
        ioctl(P.vfd, AMSTREAM_IOC_SET_PCRSCR, (unsigned long)(uint32_t)a);
        if (P.verbose)
            logmsg("pcrscr %+.3f s off, reset", diff / (double)PTS_HZ);
    }
}

static int finished(void)
{
    static int64_t idle_since;
    if (!P.video_done || !P.audio_done || !pq_empty(&P.vq) || vbuf_level() > 1024) {
        idle_since = 0;
        return 0;
    }
    /* the stream buffer is empty; give the decoder time to show what it still holds */
    if (!idle_since)
        idle_since = now_us();
    return now_us() - idle_since > 1500000;
}

/* Seconds since the start of the stream, for the controls; holds still while (re)starting */
static double play_time(void)
{
    int64_t pos = P.started ? position() : -1;
    if (pos >= 0)
        P.last_pos = (double)(pos - P.start_time) / PTS_HZ;
    return P.last_pos;
}

static void choose_audio(int i);
static void subs_clear(void);
static void chapter_step(int dir);
static void file_action(const struct osd_event *e);

/* to an absolute position in seconds from the start of the stream */
static void seek_to(double seconds, int backward)
{
    if (P.duration > 0 && seconds > P.duration - 1)
        seconds = P.duration - 1;
    if (seconds < 0)
        seconds = 0;
    P.last_pos = seconds;
    int64_t target = av_rescale(P.start_time, AV_TIME_BASE, PTS_HZ) + (int64_t)(seconds * AV_TIME_BASE);
    session_stop();
    if (P.switch_aid >= 0) {   /* another audio track: its decoder (or passthrough) between the two */
        choose_audio(P.switch_aid);
        P.switch_aid = -1;
    }
    subs_clear();
    for (int i = 0; i < P.nin; i++) {
        AVFormatContext *f = P.in[i].fmt;
        if (av_seek_frame(f, -1, target, backward ? AVSEEK_FLAG_BACKWARD : 0) < 0)
            av_seek_frame(f, -1, target, AVSEEK_FLAG_BACKWARD);
    }
    if (P.verbose)
        logmsg("seek to %.1f s", target / (double)AV_TIME_BASE);
    if (session_start() < 0)
        P.quit = 1;
}

static void seek(int seconds)
{
    seek_to(play_time() + seconds, seconds < 0);
}

static void set_paused(int paused)
{
    if (paused == P.paused)
        return;
    if (P.wclock >= 0) {   /* freeze or restart the wall clock */
        P.wclock = master_clock();
        P.wclock_at = now_us();
    }
    pthread_mutex_lock(&P.clk_m);
    P.paused = paused;
    pthread_cond_broadcast(&P.gate);
    pthread_mutex_unlock(&P.clk_m);
    if (P.cntl >= 0)
        ioctl(P.cntl, AMSTREAM_IOC_VPAUSE, P.paused);
    int64_t a = master_clock();
    if (!P.paused && a >= 0 && P.vfd >= 0)
        ioctl(P.vfd, AMSTREAM_IOC_SET_PCRSCR, (unsigned long)(uint32_t)a);
    /* a moment later the kernel moves pcrscr on by the time spent paused; sync_clock takes it
       back, and should do so before many frames go by */
    if (!P.paused)
        P.resumed_at = now_us();
}

static void toggle_pause(void)
{
    P.user_paused = !P.user_paused;
    set_paused(P.user_paused || P.buffering);
}

/* A network stream can fall behind the clock: YouTube sends video and audio over separate
   connections, and the video needs several Mbit/s of a WiFi link that does ~6. Frames that reach
   the decoder after their time are all dropped and the picture never comes back, so instead the
   clock is held until a few seconds are downloaded, as other players do. Not for TV, which
   arrives in real time. */
static void check_buffer(void)
{
    int on = P.buffering;
    if (!P.started || P.tv || P.vq.eof) {
        on = 0;
    } else {
        int64_t c = master_clock();
        pthread_mutex_lock(&P.clk_m);
        int64_t lead = P.demux_vpts - c;
        pthread_mutex_unlock(&P.clk_m);
        if (c >= 0 && P.demux_vpts >= 0)
            on = on ? lead < BUFFER_HIGH : lead < BUFFER_LOW;
    }
    if (on == P.buffering)
        return;
    P.buffering = on;
    if (P.verbose)
        logmsg(on ? "buffering" : "buffered");
    set_paused(P.user_paused || P.buffering);
}

static void do_action(const struct osd_event *e)
{
    switch (e->action) {
    case OSD_PAUSE: toggle_pause(); break;
    case OSD_SEEK_REL:
        /* PgUp / PgDn (the remote's page+ / page-): the chapters of a file that has some */
        if (!P.tv && P.nchap > 1 && abs((int)e->value) == SEEK_LONG)
            chapter_step(e->value > 0 ? 1 : -1);
        else
            seek((int)e->value);
        break;
    case OSD_SEEK_ABS: seek_to(e->value, e->value < play_time()); break;
    case OSD_VOLUME: volume_change((int)e->value); break;
    case OSD_MUTE: mute_toggle(); break;
    case OSD_QUIT: P.quit = 1; break;
    default:
        if (!P.tv)
            file_action(e);
        break;
    }
}

/* evdev keys, when there is no X */
static void handle_key(int code, int repeat)
{
    struct osd_event e;
    switch (code) {
    case KEY_SPACE: case KEY_P: case KEY_PLAYPAUSE: case KEY_PLAY: case KEY_PAUSE: case KEY_ENTER: case KEY_OK:
        e = (struct osd_event){ OSD_PAUSE, 0 }; break;
    case KEY_LEFT: case KEY_REWIND: e = (struct osd_event){ OSD_SEEK_REL, -SEEK_SHORT }; break;
    case KEY_RIGHT: case KEY_FASTFORWARD: e = (struct osd_event){ OSD_SEEK_REL, SEEK_SHORT }; break;
    case KEY_PAGEDOWN: case KEY_PREVIOUSSONG: e = (struct osd_event){ OSD_SEEK_REL, -SEEK_LONG }; break;
    case KEY_PAGEUP: case KEY_NEXTSONG: e = (struct osd_event){ OSD_SEEK_REL, SEEK_LONG }; break;
    case KEY_UP: case KEY_VOLUMEUP: e = (struct osd_event){ OSD_VOLUME, 5 }; break;
    case KEY_DOWN: case KEY_VOLUMEDOWN: e = (struct osd_event){ OSD_VOLUME, -5 }; break;
    case KEY_M: case KEY_MUTE: e = (struct osd_event){ OSD_MUTE, 0 }; break;
    case KEY_Q: case KEY_ESC: case KEY_BACK: case KEY_STOP: case KEY_POWER: case KEY_HOMEPAGE: case KEY_BACKSPACE:
        e = (struct osd_event){ OSD_QUIT, 0 }; break;
    default:
        return;
    }
    if (repeat && e.action != OSD_SEEK_REL && e.action != OSD_VOLUME)
        return;
    do_action(&e);
}

/* ---- network files ------------------------------------------------------------------------- */

/* A file over HTTP read as a run of requests for NET_CHUNK bytes each, as yt-dlp does. YouTube
 * sends one long request at about the video's own bit rate (1 MB/s for 1080p60: the buffer ran dry
 * every few seconds), and a request for a range at the full line rate (7.5 MB/s on the box). */
struct chunked {
    char *url;
    AVIOContext *cur;         /* the current request, NULL between requests */
    int64_t pos;              /* the next byte to read */
    int64_t end;              /* where the current request ends */
    int64_t size;             /* the whole file, from the first response; -1 if unknown */
};

static int chunked_request(struct chunked *c)
{
    char off[24], end[24];
    c->end = c->pos + NET_CHUNK;
    if (c->size >= 0 && c->end > c->size)
        c->end = c->size;
    snprintf(off, sizeof off, "%" PRId64, c->pos);
    snprintf(end, sizeof end, "%" PRId64, c->end);
    AVDictionary *opts = NULL;
    av_dict_set(&opts, "offset", off, 0);
    av_dict_set(&opts, "end_offset", end, 0);   /* Range: bytes=offset-(end_offset - 1) */
    AVIOInterruptCB icb = { interrupt_cb, NULL };
    int r = avio_open2(&c->cur, c->url, AVIO_FLAG_READ, &icb, &opts);
    av_dict_free(&opts);
    if (r >= 0 && c->size < 0) {
        c->size = avio_size(c->cur);   /* from Content-Range */
        if (c->size >= 0 && c->end > c->size)
            c->end = c->size;
    }
    return r;
}

static int chunked_read(void *opaque, uint8_t *buf, int len)
{
    struct chunked *c = opaque;
    for (int tries = 0;; tries++) {
        if (c->size >= 0 && c->pos >= c->size)
            return AVERROR_EOF;
        int r = 0;
        if (!c->cur || c->pos >= c->end) {
            avio_closep(&c->cur);
            r = chunked_request(c);
        }
        if (r >= 0) {
            r = avio_read_partial(c->cur, buf, FFMIN(len, c->end - c->pos));
            if (r > 0) {
                c->pos += r;
                return r;
            }
            if (r == 0 || r == AVERROR_EOF) {
                if (c->size < 0)
                    return AVERROR_EOF;   /* no size to go by: the end of the file */
                r = AVERROR(EIO);         /* the request ended early */
            }
        }
        avio_closep(&c->cur);
        /* again from here, as the reconnect option did for one long request */
        if (r == AVERROR_EXIT || tries == 5)
            return r;
        logmsg("network read: %s, again", av_err2str(r));
        av_usleep(500000 << FFMIN(tries, 3));
    }
}

static int64_t chunked_seek(void *opaque, int64_t off, int whence)
{
    struct chunked *c = opaque;
    if (whence & AVSEEK_SIZE)
        return c->size;
    whence &= ~AVSEEK_FORCE;
    if (whence == SEEK_CUR)
        off += c->pos;
    else if (whence == SEEK_END && c->size >= 0)
        off += c->size;
    else if (whence != SEEK_SET)
        return AVERROR(EINVAL);
    if (off < 0)
        return AVERROR(EINVAL);
    if (off != c->pos) {
        avio_closep(&c->cur);   /* the next read asks for a range from here */
        c->pos = off;
    }
    return off;
}

/* The input's AVIOContext on chunked requests; the first one is made here, for the file size */
static int chunked_open(AVFormatContext *fmt, const char *url)
{
    struct chunked *c = av_mallocz(sizeof *c);
    c->url = av_strdup(url);
    c->size = -1;
    int r = chunked_request(c);
    if (r < 0)
        return r;
    int bufsize = 64 * 1024;
    fmt->pb = avio_alloc_context(av_malloc(bufsize), bufsize, 0, c, chunked_read, NULL, chunked_seek);
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    return 0;
}

/* ---- setup --------------------------------------------------------------------------------- */

static int open_input(struct input *in, const char *url)
{
    in->fmt = avformat_alloc_context();
    in->fmt->interrupt_callback = (AVIOInterruptCB){ interrupt_cb, NULL };
    AVDictionary *opts = NULL;
    int r = 0;
    if (!strncmp(url, "http://", 7) || !strncmp(url, "https://", 8))
        r = chunked_open(in->fmt, url);
    else {
        av_dict_set(&opts, "reconnect", "1", 0);
        av_dict_set(&opts, "reconnect_streamed", "1", 0);
        av_dict_set(&opts, "reconnect_delay_max", "5", 0);
    }
    if (r >= 0)
        r = avformat_open_input(&in->fmt, url, NULL, &opts);
    av_dict_free(&opts);
    if (r < 0 || avformat_find_stream_info(in->fmt, NULL) < 0) {
        logmsg("%s: %s", url, av_err2str(r));
        return -1;
    }
    in->vidx = in->aidx = -1;
    return 0;
}

static int open_audio(void)
{
    if (audio_decoder(P.ast->codecpar->codec_id, P.ast->codecpar) < 0)
        return -1;
    if (!P.actx->ch_layout.nb_channels)
        av_channel_layout_default(&P.actx->ch_layout, 2);
    return open_pcm();
}

static void on_signal(int sig)
{
    (void)sig;
    P.quit = 1;
}

/* ---- TV ------------------------------------------------------------------------------------ */

static AVStream *stream_by_pid(AVFormatContext *f, int pid)
{
    for (unsigned i = 0; i < f->nb_streams; i++)
        if (f->streams[i]->id == pid)
            return f->streams[i];
    return NULL;
}

/* timeshift: a keyframe and where its PES starts, for winding back to it */
static void idx_add(int64_t pts, int64_t off)
{
    pthread_mutex_lock(&P.clk_m);
    if (P.nidx && pts <= P.idx[P.nidx - 1].pts) {
        /* winding back plays known ground again; anything else is a new timeline */
        if (pts >= P.idx[0].pts) {
            pthread_mutex_unlock(&P.clk_m);
            return;
        }
        P.nidx = 0;
    }
    if (P.nidx == IDX_MAX) {
        memmove(P.idx, P.idx + IDX_MAX / 4, sizeof P.idx[0] * (IDX_MAX - IDX_MAX / 4));
        P.nidx -= IDX_MAX / 4;
    }
    P.idx[P.nidx].pts = pts;
    P.idx[P.nidx].off = off;
    P.nidx++;
    pthread_mutex_unlock(&P.clk_m);
}

/* The broadcast's time now: the newest timestamp read at the live edge, moved on by the wall clock */
static int64_t live_now(void)
{
    pthread_mutex_lock(&P.clk_m);
    int64_t t = P.live_pts < 0 ? -1 : P.live_pts + (now_us() - P.live_at) * PTS_HZ / 1000000;
    pthread_mutex_unlock(&P.clk_m);
    return t;
}

/* Seconds behind the broadcast, 0 when live (always a little behind: the audio in hand, the
   decoder, the buffers) */
static double tv_shift(void)
{
    int64_t l = live_now(), p = P.started ? position() : -1;
    if (l < 0 || p < 0 || l - p < LIVE_SLACK)
        return 0;
    return (l - p) / (double)PTS_HZ;
}

static void sub_queue_clear(void)
{
    pthread_mutex_lock(&P.sub_m);
    for (int i = 0; i < P.nsub; i++) {
        av_free(P.subq[i].rgba);
        av_free(P.subq[i].text);
    }
    P.nsub = 0;
    pthread_mutex_unlock(&P.sub_m);
}

/* An ASS event's text without its fields and override codes: "ReadOrder,Layer,Style,Name,
   MarginL,MarginR,MarginV,Effect,Text", {\i1} and the like out, \N a new line */
static char *ass_text(const char *ass)
{
    const char *t = ass;
    for (int commas = 0; *t && commas < 8; t++)
        commas += *t == ',';
    if (!strncmp(ass, "Dialogue:", 9))   /* an older FFmpeg's full line: one more field */
        for (int commas = 0; *t && commas < 1; t++)
            commas += *t == ',';
    char *out = av_malloc(strlen(t) + 1), *o = out;
    if (!out)
        return NULL;
    int tag = 0;
    for (; *t; t++) {
        if (*t == '{')
            tag = 1;
        else if (*t == '}')
            tag = 0;
        else if (tag)
            continue;
        else if (t[0] == '\\' && (t[1] == 'N' || t[1] == 'n')) {
            *o++ = '\n';
            t++;
        } else if (t[0] == '\\' && t[1] == 'h') {
            *o++ = ' ';
            t++;
        } else if (*t != '\r') {
            *o++ = *t;
        }
    }
    *o = 0;
    while (o > out && (o[-1] == '\n' || o[-1] == ' '))
        *--o = 0;
    return out;
}

/* the text events of a subtitle, one under the other; 0 if it has none */
static int subtitle_text(const AVSubtitle *s, char *text, size_t n)
{
    text[0] = 0;
    for (unsigned i = 0; i < s->num_rects; i++) {
        AVSubtitleRect *rc = s->rects[i];
        char *t = rc->type == SUBTITLE_ASS && rc->ass ? ass_text(rc->ass) :
                  rc->type == SUBTITLE_TEXT && rc->text ? av_strdup(rc->text) : NULL;
        if (t && *t)
            snprintf(text + strlen(text), n - strlen(text), "%s%s", text[0] ? "\n" : "", t);
        av_free(t);
    }
    return text[0] != 0;
}

/* A subtitle packet, decoded with ctx (in the demuxer's thread) into the queue the main loop
   shows on time: a picture (DVB, PGS, DVD), or text (SubRip, ASS, mov_text...) */
static void sub_push(AVCodecContext *ctx, AVStream *st, AVPacket *pkt)
{

    AVSubtitle s;
    int got = 0;
    int r = avcodec_decode_subtitle2(ctx, &s, &got, pkt);
    if (r < 0 || !got) {
        if (P.verbose && r < 0)
            logmsg("subtitle decoding: %s", av_err2str(r));
        return;
    }
    int64_t base = s.pts != AV_NOPTS_VALUE ? av_rescale(s.pts, PTS_HZ, AV_TIME_BASE)
                                           : to_pts90(pkt->pts, st->time_base);
    struct sub n = { .start = base + s.start_display_time * 90LL,
                     .end = s.end_display_time ? base + s.end_display_time * 90LL :
                            pkt->duration > 0 ? base + to_pts90(pkt->duration, st->time_base) : -1,
                     .dispw = ctx->width > 0 ? ctx->width : 720,
                     .disph = ctx->height > 0 ? ctx->height : 576, .stream = st->index };
    char text[1024];
    if (subtitle_text(&s, text, sizeof text))
        n.text = av_strdup(text);
    /* pictures: all regions in one over their bounding box */
    int x0 = INT32_MAX, y0 = INT32_MAX, x1 = 0, y1 = 0;
    for (unsigned i = 0; i < s.num_rects; i++) {
        AVSubtitleRect *r = s.rects[i];
        if (r->type != SUBTITLE_BITMAP || r->w <= 0 || r->h <= 0)
            continue;
        x0 = FFMIN(x0, r->x);
        y0 = FFMIN(y0, r->y);
        x1 = FFMAX(x1, r->x + r->w);
        y1 = FFMAX(y1, r->y + r->h);
    }
    if (x1 > x0 && y1 > y0) {
        n.x = x0;
        n.y = y0;
        n.w = x1 - x0;
        n.h = y1 - y0;
        n.rgba = av_mallocz((size_t)n.w * n.h * 4);
        for (unsigned i = 0; n.rgba && i < s.num_rects; i++) {
            AVSubtitleRect *r = s.rects[i];
            if (r->type != SUBTITLE_BITMAP)
                continue;
            const uint32_t *pal = (const uint32_t *)r->data[1];
            for (int y = 0; y < r->h; y++)
                for (int x = 0; x < r->w; x++) {
                    uint32_t c = pal[r->data[0][y * r->linesize[0] + x]];   /* ARGB */
                    uint8_t *o = n.rgba + ((size_t)(r->y - y0 + y) * n.w + (r->x - x0 + x)) * 4;
                    o[0] = c >> 16;
                    o[1] = c >> 8;
                    o[2] = c;
                    o[3] = c >> 24;
                }
        }
    }
    if (P.verbose)
        logmsg("subtitle: %u region(s), %dx%d%s, at %.3f s for %.1f s", s.num_rects, n.w, n.h, n.text ? ", text" : "",
               n.start / (double)PTS_HZ, n.end >= 0 ? (n.end - n.start) / (double)PTS_HZ : -1.0);
    avsubtitle_free(&s);
    pthread_mutex_lock(&P.sub_m);
    if (P.nsub == 64) {   /* not shown in time: the oldest goes (a file's demuxer reads seconds ahead) */
        av_free(P.subq[0].rgba);
        av_free(P.subq[0].text);
        memmove(P.subq, P.subq + 1, sizeof P.subq[0] * 63);
        P.nsub--;
    }
    P.subq[P.nsub++] = n;
    pthread_mutex_unlock(&P.sub_m);
}

/* A DVB subtitle packet of the TV */
static void sub_decode(AVStream *st, AVPacket *pkt)
{
    if (!P.sctx || P.spid_now != st->id) {
        avcodec_free_context(&P.sctx);
        const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
        P.sctx = dec ? avcodec_alloc_context3(dec) : NULL;
        if (!P.sctx || avcodec_parameters_to_context(P.sctx, st->codecpar) < 0 ||
            avcodec_open2(P.sctx, dec, NULL) < 0) {
            logmsg("no subtitle decoder for %s", avcodec_get_name(st->codecpar->codec_id));
            avcodec_free_context(&P.sctx);
            P.spid_now = st->id;
            return;
        }
        P.sctx->pkt_timebase = st->time_base;
        P.spid_now = st->id;
    }
    if (P.sctx)
        sub_push(P.sctx, st, pkt);
}

/* The DVB subtitle due now, or the last one off when its time is over */
static void subs_update(void)
{
    int64_t c = P.started ? position() : -1;
    if (c < 0)
        return;
    struct sub cur = { .start = -1 };
    pthread_mutex_lock(&P.sub_m);
    while (P.nsub && P.subq[0].start <= c) {
        av_free(cur.rgba);
        av_free(cur.text);
        cur = P.subq[0];
        memmove(P.subq, P.subq + 1, sizeof P.subq[0] * (P.nsub - 1));
        P.nsub--;
        if (!P.tv && cur.stream != P.sidx) {   /* another of the file's streams */
            av_freep(&cur.rgba);
            av_freep(&cur.text);
            cur.start = -1;
        }
    }
    pthread_mutex_unlock(&P.sub_m);
    if (cur.start >= 0) {
        osd_subtitle(cur.text);
        if (cur.text) {
            osd_image_clear(0);
            P.sub_shown = 1;
            P.sub_end = cur.end;
            av_free(cur.text);
            av_free(cur.rgba);
        } else if (cur.rgba) {
            int W = osd_width(), H = osd_height();
            osd_image(0, cur.rgba, cur.w, cur.h, cur.x * W / cur.dispw, cur.y * H / cur.disph, cur.w * W / cur.dispw,
                      cur.h * H / cur.disph);
            P.sub_shown = 1;
            P.sub_end = cur.end;
            av_free(cur.rgba);
        } else {
            osd_image_clear(0);
            P.sub_shown = 0;
        }
    } else if (P.sub_shown && P.sub_end >= 0 && c >= P.sub_end) {
        osd_image_clear(0);
        osd_subtitle(NULL);
        P.sub_shown = 0;
    }
}

static void subs_clear(void)
{
    sub_queue_clear();
    osd_image_clear(0);
    osd_subtitle(NULL);
    P.sub_shown = 0;
    P.ext_shown = -1;
}

static const struct track *sub_track(void)
{
    return P.svc.ssel >= 0 && P.svc.ssel < P.svc.ns ? &P.svc.s[P.svc.ssel] : NULL;
}

static void *tv_demux_thread(void *arg)
{
    (void)arg;
    AVFormatContext *fmt = avformat_alloc_context();
    int bufsize = 64 * 1024;
    AVIOContext *pb = avio_alloc_context(av_malloc(bufsize), bufsize, 0, &P.tsin, tsin_read, NULL, NULL);
    fmt->pb = pb;
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    fmt->interrupt_callback = (AVIOInterruptCB){ interrupt_cb, NULL };
    /* the PSI read first is all the probing needed (see psi_add) */
    AVDictionary *o = NULL;
    av_dict_set(&o, "probesize", "1504", 0);
    int r = avformat_open_input(&fmt, NULL, av_find_input_format("mpegts"), &o);
    av_dict_free(&o);
    if (r < 0) {
        if (!P.stop)
            logmsg("live input: %s", av_err2str(r));
        av_freep(&pb->buffer);   /* the context itself went with the failure */
        avio_context_free(&pb);
        P.in[0].fmt = NULL;
        P.novideo = 1;
        goto out;
    }
    P.in[0].fmt = fmt;
    if (P.svc.vpid) {
        AVStream *v = stream_by_pid(fmt, P.svc.vpid);
        const struct codec_map *c = NULL;
        for (size_t i = 0; v && i < sizeof CODECS / sizeof *CODECS; i++)
            if (CODECS[i].id == v->codecpar->codec_id)
                c = &CODECS[i];
        /* without a picture the channel plays as radio */
        if (!c) {
            logmsg("no hardware decoder for video PID %d (%s)", P.svc.vpid,
                   v ? avcodec_get_name(v->codecpar->codec_id) : "not in the PMT");
            P.novideo = 1;
            event("error video");
        } else if (decoder_open(c, 0, 0, (AVRational){ 25, 1 }) < 0) {
            P.novideo = 1;
            event("error decoder");
        }
    }

    AVPacket *pkt = av_packet_alloc();
    static char seen[8192];
    memset(seen, 0, sizeof seen);
    while (!P.stop) {
        r = av_read_frame(fmt, pkt);
        if (r == AVERROR(EAGAIN))
            continue;
        if (r < 0) {
            if (r != AVERROR_EOF && !P.stop)
                logmsg("read error: %s", av_err2str(r));
            break;
        }
        AVStream *st = fmt->streams[pkt->stream_index];
        int pid = st->id;
        if (P.verbose && pid < 8192 && !seen[pid]) {
            seen[pid] = 1;
            logmsg("PID %d: %s, first at %.2f s", pid, avcodec_get_name(st->codecpar->codec_id),
                   (now_us() - P.play_at) / 1e6);
        }
        int64_t ts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
        int64_t pts = ts != AV_NOPTS_VALUE ? to_pts90(ts, st->time_base) : -1;
        int64_t off = pkt->pos >= 0 ? P.tsin.base + pkt->pos - P.svc.npsi : -1;
        int at_edge = now_us() - P.tsin.edge_at < 1000000;
        /* A timestamp far off the timeline is either a damaged packet (a weak signal: they come
           alone, at random) or the timeline itself jumping (a broadcaster's encoder restarting, a
           test file looping: then every packet after agrees). A damaged one is dropped, or loses
           its timestamp: the video layer would hold every frame after it until then. A jump the
           clocks can't follow: the session starts again there, as after a switch. */
        int main_stream = P.svc.vpid && !P.novideo ? pid == P.svc.vpid : pid == P.svc.apid;
        if (pts >= 0 && P.last_dpts >= 0 && llabs(pts - P.last_dpts) > 10 * PTS_HZ) {
            if (main_stream && off >= 0) {
                if (P.jump_n && llabs(pts - P.jump_pts) < 2 * PTS_HZ) {
                    P.jump_n++;
                } else {
                    P.jump_n = 1;
                    P.jump_off = off;
                }
                P.jump_pts = pts;
                if (P.jump_n >= 25) {
                    logmsg("the timestamps jump %+.1f s: starting again there", (pts - P.last_dpts) / (double)PTS_HZ);
                    P.restart_off = P.jump_off;
                    P.restart = 1;
                    av_packet_unref(pkt);
                    break;
                }
            }
            if (P.verbose)
                logmsg("PID %d: a timestamp %+.1f s off, dropped", pid, (pts - P.last_dpts) / (double)PTS_HZ);
            if (pid != P.svc.vpid) {
                av_packet_unref(pkt);
                continue;
            }
            pkt->pts = pkt->dts = AV_NOPTS_VALUE;
            pts = -1;
        } else if (main_stream && pts >= 0) {
            P.last_dpts = pts;
            P.jump_n = 0;
        }
        if (pid == P.svc.vpid && P.vfd >= 0) {
            int key = pkt->flags & AV_PKT_FLAG_KEY;
            if (!P.vgate) {   /* from a keyframe on: the decoder can do nothing before one */
                if (!key || pts < 0) {
                    av_packet_unref(pkt);
                    continue;
                }
                P.vgate = 1;
                P.gate_pts = pts;
                if (P.verbose)
                    logmsg("keyframe after %.2f s", (now_us() - P.play_at) / 1e6);
            }
            if (key && pts >= 0 && off >= 0)
                idx_add(pts, off);
            if (pts >= 0) {
                pthread_mutex_lock(&P.clk_m);
                if (pts > P.demux_vpts)
                    P.demux_vpts = pts;
                if (at_edge) {
                    P.live_pts = pts;
                    P.live_at = now_us();
                }
                pthread_mutex_unlock(&P.clk_m);
            }
            if (pq_put(&P.vq, av_packet_clone(pkt), pid, 0) < 0)
                break;
        } else if (pid == P.svc.apid && P.has_audio) {
            if (P.svc.vpid && P.vfd >= 0 && (!P.vgate || pts < P.gate_pts)) {   /* before the picture */
                av_packet_unref(pkt);
                continue;
            }
            if (pts >= 0) {
                pthread_mutex_lock(&P.clk_m);
                if (P.first_apts < 0)
                    P.first_apts = pts;
                if (pts > P.last_apts)
                    P.last_apts = pts;
                if (!P.svc.vpid && at_edge) {   /* radio: the audio is the timeline */
                    P.live_pts = pts;
                    P.live_at = now_us();
                }
                pthread_mutex_unlock(&P.clk_m);
                if (!P.svc.vpid && off >= 0 && (!P.nidx || pts - P.idx[P.nidx - 1].pts >= PTS_HZ))
                    idx_add(pts, off);
            }
            if (pq_put(&P.aq, av_packet_clone(pkt), pid, st->codecpar->codec_id) < 0)
                break;
        } else if (pid == P.svc.ttx && pid) {
            ttx_feed(pkt->data, pkt->size);
        } else if (sub_track() && pid == sub_track()->pid && sub_track()->type == 'd') {
            sub_decode(st, pkt);
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
out:
    pq_set(&P.vq, &P.vq.eof, 1);
    pq_set(&P.aq, &P.aq.eof, 1);
    return NULL;
}

static void tv_session_start(int64_t off)
{
    session_reset();
    P.vgate = 0;
    P.gate_pts = -1;
    P.first_apts = P.last_apts = -1;
    P.frame_out = P.novideo = 0;
    P.audio_wait_since = 0;
    P.last_dpts = -1;
    P.jump_n = 0;
    P.restart = 0;
    P.play_at = now_us();
    P.tsin.base = P.tsin.pos = off;
    P.tsin.fd = -1;
    P.tsin.psi_at = 0;
    P.tsin.edge_at = 0;
    snprintf(P.tsin.src, sizeof P.tsin.src, "%s", P.svc.src);
    sub_queue_clear();
    pthread_create(&P.dth, NULL, tv_demux_thread, NULL);
    if (P.svc.vpid)
        pthread_create(&P.vth, NULL, video_thread, NULL);
    if (P.has_audio)
        pthread_create(&P.ath, NULL, audio_thread, NULL);
    P.running = 1;
}

static void tv_session_stop(void)
{
    if (!P.running)
        return;
    session_stop();
    if (P.tsin.fd >= 0)
        close(P.tsin.fd);
    P.tsin.fd = -1;
    if (P.in[0].fmt) {
        AVIOContext *pb = P.in[0].fmt->pb;
        avformat_close_input(&P.in[0].fmt);
        if (pb)
            av_freep(&pb->buffer);
        avio_context_free(&pb);
    }
    avcodec_free_context(&P.sctx);
    P.spid_now = 0;
    P.running = 0;
    if (P.user_paused)
        P.user_paused = 0;
}

static void tv_labels(void)
{
    static const char *modes[] = { "", " L", " R" };
    P.audio_label[0] = P.subs_label[0] = 0;
    if (P.svc.na)
        snprintf(P.audio_label, sizeof P.audio_label, "%s%s", P.svc.a[P.svc.asel].lang[0] ? P.svc.a[P.svc.asel].lang : "1",
                 modes[P.svc.amode]);
    const struct track *s = sub_track();
    if (s)
        snprintf(P.subs_label, sizeof P.subs_label, "%s", s->lang[0] ? s->lang : "on");
}

static void ttx_off(void)
{
    if (!P.ttx_on)
        return;
    P.ttx_on = 0;
    osd_image_clear(1);
    osd_teletext(0, 0);
    osd_message(NULL, 0);   /* "page ... not in the cycle yet" */
}

static void tv_stop(void)
{
    tv_session_stop();
    ttx_off();
    osd_image_clear(0);
    P.sub_shown = 0;
    sysfs_write(SYS "video/disable_video", "1");
}

/* "pid:lang,..." or "pid:type:lang:page,..." */
static int parse_tracks(char *v, struct track *t, int subs)
{
    int n = 0;
    for (char *save = NULL, *e = strtok_r(v, ",", &save); e && n < MAX_TRACKS; e = strtok_r(NULL, ",", &save)) {
        char *f[4] = { e, NULL, NULL, NULL };
        for (int k = 1; k < 4 && f[k - 1]; k++)
            if ((f[k] = strchr(f[k - 1], ':')))
                *f[k]++ = 0;
        memset(&t[n], 0, sizeof t[n]);
        t[n].pid = atoi(f[0]);
        if (subs) {
            t[n].type = f[1] ? f[1][0] : 'd';
            snprintf(t[n].lang, sizeof t[n].lang, "%s", f[2] ? f[2] : "");
            t[n].page = f[3] ? (int)strtol(f[3], NULL, 16) : 0;
        } else {
            snprintf(t[n].lang, sizeof t[n].lang, "%s", f[1] ? f[1] : "");
        }
        if (t[n].pid > 0)
            n++;
    }
    return n;
}

static void tv_play(char **f, int nf)
{
    tv_stop();
    memset(&P.svc, 0, sizeof P.svc);
    P.svc.ssel = -1;
    char *psi = NULL;
    for (int i = 0; i < nf; i++) {
        char *v = strchr(f[i], '=');
        if (!v)
            continue;
        *v++ = 0;
        const char *k = f[i];
        if (!strcmp(k, "src")) snprintf(P.svc.src, sizeof P.svc.src, "%s", v);
        else if (!strcmp(k, "off")) P.svc.off = atoll(v);
        else if (!strcmp(k, "v")) P.svc.vpid = atoi(v);
        else if (!strcmp(k, "pmt")) P.svc.pmt = atoi(v);
        else if (!strcmp(k, "a")) P.svc.na = parse_tracks(v, P.svc.a, 0);
        else if (!strcmp(k, "asel")) P.svc.asel = atoi(v);
        else if (!strcmp(k, "amode")) P.svc.amode = atoi(v);
        else if (!strcmp(k, "s")) P.svc.ns = parse_tracks(v, P.svc.s, 1);
        else if (!strcmp(k, "ssel")) P.svc.ssel = atoi(v);
        else if (!strcmp(k, "ttx")) P.svc.ttx = atoi(v);
        else if (!strcmp(k, "ttxregion")) ttx_region(atoi(v));
        else if (!strcmp(k, "psi")) psi = v;
        else if (!strcmp(k, "title")) snprintf(P.svc.title, sizeof P.svc.title, "%s", v);
        else if (!strcmp(k, "now")) snprintf(P.svc.now, sizeof P.svc.now, "%s", v);
    }
    if (P.svc.asel < 0 || P.svc.asel >= P.svc.na)
        P.svc.asel = 0;
    if (P.svc.amode < 0 || P.svc.amode > 2)
        P.svc.amode = 0;
    if (P.svc.ssel >= P.svc.ns)
        P.svc.ssel = -1;
    P.svc.apid = P.svc.na ? P.svc.a[P.svc.asel].pid : 0;
    P.has_audio = P.svc.na > 0 && P.pcm != NULL;
    if (psi) {
        char *pmt = strchr(psi, ',');
        psi_add(0, psi);
        if (pmt)
            psi_add(P.svc.pmt, pmt + 1);
    }
    /* enough packets for FFmpeg to tell the packet size without waiting for the broadcast */
    while (P.svc.npsi < 12 * 188) {
        uint8_t *p = P.svc.psi + P.svc.npsi;
        memset(p, 0xff, 188);
        p[0] = 0x47, p[1] = 0x1f, p[2] = 0xff, p[3] = 0x10;
        P.svc.npsi += 188;
    }
    pthread_mutex_lock(&P.clk_m);
    P.nidx = 0;
    P.live_pts = -1;
    pthread_mutex_unlock(&P.clk_m);
    P.low_off = 0;
    ttx_reset();
    osd_radio(P.svc.vpid ? NULL : P.svc.title, P.svc.now);
    tv_labels();
    P.ttx_pageno = 0x100;
    tv_session_start(P.svc.off);
}

/* Timeshift: to the keyframe at or before `to` (90 kHz), or the live picture */
static void tv_seek_to(int64_t to)
{
    int64_t live = live_now();
    int64_t off = -1;
    if (live >= 0 && to < live - LIVE_SLACK) {
        /* the newest keyframe not after it, or the oldest still in the buffer */
        pthread_mutex_lock(&P.clk_m);
        for (int i = P.nidx - 1; i >= 0 && P.idx[i].off >= P.low_off; i--) {
            off = P.idx[i].off;
            if (P.idx[i].pts <= to)
                break;
        }
        pthread_mutex_unlock(&P.clk_m);
    }
    if (off < 0) {   /* live: from the newest data on */
        int64_t end = P.tsin.pos;
        for (int64_t seg = end / SEG_BYTES;; seg++) {
            char path[440];
            snprintf(path, sizeof path, "%s%06" PRId64 ".ts", P.svc.src, seg);
            FILE *f = fopen(path, "rb");
            if (!f)
                break;
            fseek(f, 0, SEEK_END);
            end = seg * SEG_BYTES + ftell(f);
            fclose(f);
        }
        off = end - end % 188;
    }
    if (P.verbose)
        logmsg("timeshift to offset %" PRId64, off);
    tv_session_stop();
    osd_image_clear(0);
    P.sub_shown = 0;
    tv_session_start(off);
}

static void ttx_show(void)
{
    if (!P.ttx_on)
        return;
    uint8_t *rgba;
    int w, h;
    char t[64];
    if (!ttx_page(P.ttx_pageno, P.ttx_mode, &rgba, &w, &h, P.ttx_links)) {
        snprintf(t, sizeof t, "Teletext: page %x %s", P.ttx_pageno, ttx_any() ? "is not in the cycle yet…" : "…");
        osd_message(t, 0);
        return;
    }
    osd_message(NULL, 0);
    if (P.verbose && !P.ttx_drawn) {
        int opaque = 0;
        for (int i = 0; i < w * h; i++)
            opaque += rgba[i * 4 + 3] >= 128;
        logmsg("teletext page %x: %dx%d, %d%% opaque", P.ttx_pageno, w, h, opaque * 100 / (w * h));
    }
    int H = osd_height() * 9 / 10, W = H * 4 / 3;
    osd_image(1, rgba, w, h, (osd_width() - W) / 2, (osd_height() - H) / 2, W, H);
    free(rgba);
    P.ttx_drawn = 1;
}

/* Teletext subtitles: their page whenever it comes in */
static void ttx_subs_update(void)
{
    const struct track *s = sub_track();
    if (!s || s->type != 't' || P.ttx_on || !ttx_received(s->page))
        return;
    uint8_t *rgba;
    int w, h, links[4];
    if (ttx_page(s->page, TTX_PAGE_SUBTITLE, &rgba, &w, &h, links)) {
        osd_image(0, rgba, w, h, 0, 0, osd_width(), osd_height());
        free(rgba);
        P.sub_shown = 1;
        P.sub_end = -1;
    }
}

/* the next of the video layer's aspect modes; its name */
static const char *aspect_next(void)
{
    int n = sizeof ASPECTS / sizeof *ASPECTS, k = 0;
    for (int i = 0; i < n; i++)
        if (ASPECTS[i].mode == P.aspect)
            k = (i + 1) % n;
    P.aspect = ASPECTS[k].mode;
    set_aspect(P.aspect);
    return ASPECTS[k].name;
}

static void tv_action(const struct osd_event *e)
{
    char t[128];
    switch (e->action) {
    case OSD_CHANNEL: {
        int i = (int)e->value;
        const char *name = osd_channel_title(i, t, sizeof t);
        tv_stop();
        osd_set_current(i);
        osd_radio(NULL, NULL);
        snprintf(P.svc.title, sizeof P.svc.title, "%s", name ? name : "");
        event("channel %d", i);
        break;
    }
    case OSD_PAUSE:
        if (P.running)
            toggle_pause();
        break;
    case OSD_SEEK_REL:
        if (P.running && P.started)
            tv_seek_to(position() + (int64_t)e->value * PTS_HZ);
        break;
    case OSD_LIVE:
        if (P.running)
            tv_seek_to(INT64_MAX);
        break;
    case OSD_AUDIO: {
        if (!P.svc.na)
            break;
        int k = (P.svc.asel * 3 + P.svc.amode + 1) % (P.svc.na * 3);
        P.svc.asel = k / 3;
        P.svc.amode = k % 3;
        P.svc.apid = P.svc.a[P.svc.asel].pid;
        tv_labels();
        static const char *modes[] = { "stereo", "left channel", "right channel" };
        snprintf(t, sizeof t, "Audio %d/%d: %s, %s", P.svc.asel + 1, P.svc.na,
                 P.svc.a[P.svc.asel].lang[0] ? P.svc.a[P.svc.asel].lang : "-", modes[P.svc.amode]);
        osd_message(t, 2);
        event("audio %d %d", P.svc.apid, P.svc.amode);
        break;
    }
    case OSD_SUBS: {
        if (!P.svc.ns) {
            osd_message("No subtitles on this channel", 2);
            break;
        }
        P.svc.ssel = P.svc.ssel + 1 < P.svc.ns ? P.svc.ssel + 1 : -1;
        osd_image_clear(0);
        P.sub_shown = 0;
        sub_queue_clear();
        tv_labels();
        const struct track *s = sub_track();
        if (s) {
            snprintf(t, sizeof t, "Subtitles: %s%s", s->lang[0] ? s->lang : "on", s->type == 't' ? " (teletext)" : "");
            if (s->type == 't')
                ttx_received(s->page);   /* drawn from the next time it comes in */
        }
        osd_message(s ? t : "Subtitles off", 2);
        event("subs %d %x", s ? s->pid : 0, s ? s->page : 0);
        break;
    }
    case OSD_ASPECT:
        snprintf(t, sizeof t, "Picture: %s", aspect_next());
        osd_message(t, 2);
        event("aspect %d", P.aspect);
        break;
    case OSD_RECORD: event("record"); break;
    case OSD_GROUP: event("group"); break;
    case OSD_RADIO: event("tvradio"); break;
    case OSD_PIN: event("pin %04d", (int)e->value); break;
    case OSD_TEXT:
        if (P.ttx_on) {
            ttx_off();
        } else if (!P.svc.ttx) {
            osd_message("No teletext on this channel", 2);
        } else {
            P.ttx_on = 1;
            P.ttx_mode = TTX_PAGE_FULL;
            osd_image_clear(0);
            P.sub_shown = 0;
            osd_teletext(1, P.ttx_pageno);
            ttx_show();
        }
        break;
    case OSD_TEXT_PAGE: {
        int v = (int)e->value;
        if (v == TTX_NEXT || v == TTX_PREV)
            P.ttx_pageno = ttx_step(P.ttx_pageno, v == TTX_NEXT ? 1 : -1);
        else if (v == TTX_MIX)
            P.ttx_mode = P.ttx_mode == TTX_PAGE_FULL ? TTX_PAGE_MIX : TTX_PAGE_FULL;
        else if (v <= TTX_LINK && v > TTX_LINK - 4) {
            int to = P.ttx_links[TTX_LINK - v];
            if (to >= 0x100 && to <= 0x899)
                P.ttx_pageno = to;
        } else if (v >= 0x100 && v <= 0x899)
            P.ttx_pageno = v;
        osd_teletext(1, P.ttx_pageno);
        ttx_show();
        break;
    }
    case OSD_QUIT:
        P.quit = 1;
        event("quit");
        break;
    default:
        do_action(e);
    }
}

/* "\n" in a command's text is a new line, "\t" a tab */
static void unescape(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '\\' && (s[1] == 'n' || s[1] == 't' || s[1] == '\\')) {
            s++;
            *o++ = *s == 'n' ? '\n' : *s == 't' ? '\t' : '\\';
        } else {
            *o++ = *s;
        }
    }
    *o = 0;
}

/* --channels FILE / "channels FILE INDEX": the list for the OSD */
static void load_channels(const char *path, int current)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        logmsg("%s: %s", path, strerror(errno));
        return;
    }
    char *lines[1024], buf[512];
    int n = 0;
    while (n < 1024 && fgets(buf, sizeof buf, f)) {
        buf[strcspn(buf, "\n")] = 0;
        lines[n++] = strdup(buf);
    }
    fclose(f);
    osd_set_channels(lines, n, current);
    while (n)
        free(lines[--n]);
}

static void tv_command(char *line)
{
    char *f[24];
    int n = 0;
    for (char *p = line; p && n < 24;) {
        f[n++] = p;
        if ((p = strchr(p, '\t')))
            *p++ = 0;
    }
    const char *c = f[0];
    const char *a1 = n > 1 ? f[1] : "";
    if (!strcmp(c, "play")) {
        tv_play(f + 1, n - 1);
    } else if (!strcmp(c, "stop")) {
        tv_stop();
        osd_radio(NULL, NULL);
        snprintf(P.svc.title, sizeof P.svc.title, "%s", a1);
    } else if (!strcmp(c, "title")) {
        snprintf(P.svc.title, sizeof P.svc.title, "%s", a1);
        if (!P.svc.vpid && P.running && n > 2)
            osd_radio(P.svc.title, f[2]);
    } else if (!strcmp(c, "info")) {
        unescape(f[1]);
        osd_info(a1);
    } else if (!strcmp(c, "signal")) {
        int l = 0, s = 0, q = 0;
        sscanf(a1, "%d %d %d", &l, &s, &q);
        P.signal = l ? q : 0;
    } else if (!strcmp(c, "message") && n > 2) {
        unescape(f[2]);
        osd_message(f[2], atoi(a1));
    } else if (!strcmp(c, "pin")) {
        osd_pin(a1[0] ? a1 : NULL);
    } else if (!strcmp(c, "channels") && n > 2) {
        load_channels(a1, atoi(f[2]));
    } else if (!strcmp(c, "current")) {
        osd_set_current(atoi(a1));
    } else if (!strcmp(c, "rec")) {
        P.recording = atoi(a1);
    } else if (!strcmp(c, "buffer")) {
        P.low_off = atoll(a1);
    } else if (!strcmp(c, "aspect")) {
        P.aspect = atoi(a1);
        set_aspect(P.aspect);
    } else if (!strcmp(c, "quit")) {
        P.quit = 1;
    } else {
        logmsg("unknown command %s", c);
    }
}

/* Lines of its own for the info panel: the picture, the sound, the buffer */
static void tv_info_extra(void)
{
    char t[512];
    int w = sysfs_int(SYS "video/frame_width"), h = sysfs_int(SYS "video/frame_height");
    int n = 0;
    if (P.svc.vpid && w > 0 && h > 0)
        n += snprintf(t + n, sizeof t - n, "Picture: %dx%d, %s\n", w, h,
                      ASPECTS[P.aspect >= 0 && P.aspect < 4 ? P.aspect : 0].name);
    if (P.actx && P.actx->sample_rate)
        n += snprintf(t + n, sizeof t - n, "Sound: %s, %d Hz, %d ch%s%s\n", avcodec_get_name(P.actx->codec_id),
                      P.actx->sample_rate, P.actx->ch_layout.nb_channels, P.audio_label[0] ? ", " : "", P.audio_label);
    n += snprintf(t + n, sizeof t - n, "Signal: %d%%\n", P.signal);
    double s = tv_shift();
    if (s > 0)
        snprintf(t + n, sizeof t - n, "Timeshift: %d:%02d behind the broadcast\n", (int)s / 60, (int)s % 60);
    osd_info_extra(t);
}

static int tv_main(void)
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    signal(SIGPIPE, SIG_IGN);
    sigaction(SIGUSR1, &(struct sigaction){ .sa_handler = on_wake }, NULL);
    if (!fr_auto_off()) {
        logmsg("can't turn off " SYS "tv/policy_fr_auto, refusing to start");
        return 1;
    }
    if (!getenv("DISPLAY") || osd_open(1) < 0) {
        logmsg("the TV player needs X");
        return 1;
    }
    P.x11 = 1;
    P.vtb = P.atb = (AVRational){ 1, PTS_HZ };
    pq_init(&P.vq, 16 << 20);
    pq_init(&P.aq, 2 << 20);
    if (open_pcm() < 0)
        P.pcm = NULL;
    if (ttx_init() < 0)
        logmsg("no teletext decoder");
    mixer_open();
    match_rate(25);   /* the broadcasts here are all 25 fps */
    display_on();
    sysfs_write(SYS "video/disable_video", "1");
    fcntl(0, F_SETFL, O_NONBLOCK);
    struct pollfd fds[2] = { { .fd = osd_fd(), .events = POLLIN }, { .fd = 0, .events = POLLIN } };
    static char cmd[32768];
    size_t ncmd = 0;
    int64_t info_at = 0;
    while (!P.quit) {
        int fast = P.running && (!P.started || now_us() - P.started_at < 2000000 || now_us() - P.resumed_at < 1000000);
        poll(fds, 2, fast ? 20 : 100);
        if (fds[1].revents & (POLLIN | POLLHUP)) {
            ssize_t r = read(0, cmd + ncmd, sizeof cmd - 1 - ncmd);
            if (r == 0 || (r < 0 && errno != EAGAIN && errno != EINTR)) {
                P.quit = 1;   /* ozone-tv is gone */
                break;
            }
            if (r > 0) {
                ncmd += r;
                cmd[ncmd] = 0;
                char *nl, *p = cmd;
                while ((nl = strchr(p, '\n'))) {
                    *nl = 0;
                    if (*p)
                        tv_command(p);
                    p = nl + 1;
                }
                ncmd -= p - cmd;
                memmove(cmd, p, ncmd);
                if (ncmd == sizeof cmd - 1)
                    ncmd = 0;   /* a line too long: dropped */
            }
        }
        struct osd_event ev[8];
        int n = osd_poll(ev, 8);
        for (int i = 0; i < n && !P.quit; i++)
            tv_action(&ev[i]);
        if (P.running && P.restart) {
            int64_t off = P.restart_off;
            tv_session_stop();
            pthread_mutex_lock(&P.clk_m);
            P.live_pts = -1;
            pthread_mutex_unlock(&P.clk_m);
            osd_image_clear(0);
            P.sub_shown = 0;
            tv_session_start(off);
        }
        if (P.running) {
            sync_clock();
            subs_update();
            ttx_subs_update();
            if (P.ttx_on && ttx_received(P.ttx_pageno))
                ttx_show();
        }
        if (osd_info_open() && now_us() - info_at > 1000000) {
            tv_info_extra();
            info_at = now_us();
        }
        struct osd_state st = {
            .pos = 0, .dur = 0, .paused = P.user_paused, .loading = P.running && !P.started,
            .live = 1, .shift = P.running ? tv_shift() : 0, .recording = P.recording,
            .audio = P.audio_label[0] ? P.audio_label : NULL, .subs = P.subs_label[0] ? P.subs_label : NULL,
            .signal = P.signal, .volume = saved_volume >= 0 ? saved_volume : volume_get(), .muted = saved_volume >= 0,
            .title = P.svc.title,
        };
        osd_update(&st);
    }
    tv_stop();
    osd_close();
    display_off();
    restore_mode();
    return 0;
}


/* ---- files: tracks, subtitles, chapters, repeat, info --------------------------------------- */

static const char *lang_of(AVStream *st)
{
    AVDictionaryEntry *e = av_dict_get(st->metadata, "language", NULL, 0);
    return e && strcmp(e->value, "und") ? e->value : NULL;
}

/* "eng, ac3 5.1, Commentary" */
static void track_name(AVStream *st, char *buf, size_t n)
{
    const AVCodecParameters *c = st->codecpar;
    AVDictionaryEntry *t = av_dict_get(st->metadata, "title", NULL, 0);
    const char *lang = lang_of(st);
    int ch = c->ch_layout.nb_channels;
    snprintf(buf, n, "%s%s%s%s%s%s", lang ? lang : "", lang ? ", " : "", avcodec_get_name(c->codec_id),
             c->codec_type != AVMEDIA_TYPE_AUDIO ? "" : ch == 1 ? " mono" : ch == 6 ? " 5.1" : ch == 8 ? " 7.1" : "",
             t ? ", " : "", t ? t->value : "");
}

static void choose_audio(int i)
{
    if (P.pcm)
        snd_pcm_close(P.pcm);
    P.pcm = NULL;
    raw_close();
    avcodec_free_context(&P.actx);
    swr_free(&P.swr);
    P.ast = NULL;
    P.has_audio = 0;
    P.ain->aidx = -1;
    P.out_rate = AUDIO_RATE;
    if (i < 0 || i >= P.naids)
        return;
    P.aid = i;
    P.ain->aidx = P.aids[i];
    P.ast = P.ain->fmt->streams[P.aids[i]];
    P.atb = P.ast->time_base;
    if ((raw_wanted(P.ast->codecpar->codec_id) && raw_open() == 0) || open_audio() == 0) {
        P.has_audio = 1;
        return;
    }
    P.ast = NULL;
    P.ain->aidx = -1;
}

static void file_sub_decode(AVStream *st, AVPacket *pkt)
{
    int k = 0;
    while (k < P.nsids && P.sids[k] != st->index)
        k++;
    if (k == P.nsids)
        return;
    if (!P.fsctx[k]) {
        const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
        AVCodecContext *ctx = dec ? avcodec_alloc_context3(dec) : NULL;
        if (!ctx || avcodec_parameters_to_context(ctx, st->codecpar) < 0 || avcodec_open2(ctx, dec, NULL) < 0) {
            logmsg("no subtitle decoder for %s", avcodec_get_name(st->codecpar->codec_id));
            avcodec_free_context(&ctx);
            P.sids[k] = -2;   /* not again */
            return;
        }
        ctx->pkt_timebase = st->time_base;
        P.fsctx[k] = ctx;
    }
    sub_push(P.fsctx[k], st, pkt);
}

static int valid_utf8(const unsigned char *s, size_t n)
{
    for (size_t i = 0; i < n;) {
        int k = s[i] < 0x80 ? 0 : (s[i] & 0xe0) == 0xc0 ? 1 : (s[i] & 0xf0) == 0xe0 ? 2 : (s[i] & 0xf8) == 0xf0 ? 3 : -1;
        if (k < 0 || i + k >= n + (k == 0))
            return 0;
        for (int j = 1; j <= k; j++)
            if (i + j >= n || (s[i + j] & 0xc0) != 0x80)
                return 0;
        i += k + 1;
    }
    return 1;
}

/* A subtitle file, read whole: its lines with their times (from 0, the video's start) */
static void ext_load(const char *path, const char *name)
{
    if (P.next == 4)
        return;
    /* Ukrainian and Russian .srt files are often Windows-1251, which the decoders take for broken
       UTF-8: then it is converted */
    unsigned char head[65536];
    FILE *fh = fopen(path, "rb");
    size_t got = fh ? fread(head, 1, sizeof head, fh) : 0;
    if (fh)
        fclose(fh);
    while (got && (head[got - 1] & 0xc0) == 0x80)   /* a character cut by the 64 KB */
        got--;
    if (got && (head[got - 1] & 0x80))
        got--;
    AVFormatContext *f = NULL;
    if (avformat_open_input(&f, path, NULL, NULL) < 0)
        return;
    avformat_find_stream_info(f, NULL);
    int si = av_find_best_stream(f, AVMEDIA_TYPE_SUBTITLE, -1, -1, NULL, 0);
    const AVCodec *dec = si >= 0 ? avcodec_find_decoder(f->streams[si]->codecpar->codec_id) : NULL;
    AVCodecContext *ctx = dec ? avcodec_alloc_context3(dec) : NULL;
    AVDictionary *o = NULL;
    if (!valid_utf8(head, got))
        av_dict_set(&o, "sub_charenc", "CP1251", 0);
    if (!ctx || avcodec_parameters_to_context(ctx, f->streams[si]->codecpar) < 0 || avcodec_open2(ctx, dec, &o) < 0) {
        av_dict_free(&o);
        avcodec_free_context(&ctx);
        avformat_close_input(&f);
        return;
    }
    av_dict_free(&o);
    ctx->pkt_timebase = f->streams[si]->time_base;
    struct extfile *e = &P.ext[P.next];
    memset(e, 0, sizeof *e);
    snprintf(e->name, sizeof e->name, "%s", name);
    AVPacket *pkt = av_packet_alloc();
    int cap = 0;
    while (av_read_frame(f, pkt) >= 0) {
        AVSubtitle sub;
        int ok = 0;
        char text[1024];
        if (pkt->stream_index == si && avcodec_decode_subtitle2(ctx, &sub, &ok, pkt) >= 0 && ok) {
            if (subtitle_text(&sub, text, sizeof text)) {
                if (e->n == cap) {
                    cap = cap ? cap * 2 : 256;
                    struct extsub *ns = realloc(e->s, cap * sizeof *ns);
                    if (!ns)
                        break;
                    e->s = ns;
                }
                AVRational tb = f->streams[si]->time_base;
                int64_t start = to_pts90(pkt->pts, tb) + sub.start_display_time * 90LL;
                int64_t end = sub.end_display_time ? to_pts90(pkt->pts, tb) + sub.end_display_time * 90LL :
                              start + (pkt->duration > 0 ? to_pts90(pkt->duration, tb) : 4 * PTS_HZ);
                e->s[e->n++] = (struct extsub){ start, end, strdup(text) };
            }
            avsubtitle_free(&sub);
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    avformat_close_input(&f);
    if (e->n) {
        if (P.verbose)
            logmsg("subtitles %s: %d lines", name, e->n);
        P.next++;
    }
}

/* FILE.srt, FILE.ukr.srt, FILE.en.ass... next to the video */
static void ext_find(const char *video)
{
    if (strstr(video, "://"))
        return;
    char dir[1024], base[512];
    const char *slash = strrchr(video, '/');
    snprintf(dir, sizeof dir, "%.*s", slash ? (int)(slash - video) : 1, slash ? video : ".");
    snprintf(base, sizeof base, "%s", slash ? slash + 1 : video);
    char *dot = strrchr(base, '.');
    if (dot)
        *dot = 0;
    size_t bl = strlen(base);
    DIR *d = opendir(dir);
    struct dirent *de;
    while (d && (de = readdir(d))) {
        const char *ext = strrchr(de->d_name, '.');
        if (!ext || strncmp(de->d_name, base, bl) || de->d_name[bl] != '.' ||
            (strcasecmp(ext, ".srt") && strcasecmp(ext, ".ass") && strcasecmp(ext, ".ssa") && strcasecmp(ext, ".vtt")))
            continue;
        char path[1600];
        snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        ext_load(path, de->d_name + bl + 1);   /* "ukr.srt" */
    }
    if (d)
        closedir(d);
}

static void ext_update(void)
{
    struct extfile *e = &P.ext[P.ssel - P.nsids];
    int64_t c = P.started ? position() - P.start_time : -1;
    if (c < 0)
        return;
    int k = -1, lo = 0, hi = e->n - 1;
    while (lo <= hi) {   /* the last line that has begun */
        int mid = (lo + hi) / 2;
        if (e->s[mid].start <= c) {
            k = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    if (k >= 0 && c >= e->s[k].end)
        k = -1;
    if (k != P.ext_shown) {
        osd_subtitle(k >= 0 ? e->s[k].text : NULL);
        P.ext_shown = k;
    }
}

/* the subtitles: -1 off, a stream of the video, or a file */
static void subs_select(int k)
{
    if (k < -1 || k >= P.nsids + P.next)
        k = -1;
    P.ssel = k;
    P.sidx = k >= 0 && k < P.nsids ? P.sids[k] : -1;
    osd_image_clear(0);
    osd_subtitle(NULL);
    P.sub_shown = 0;
    P.ext_shown = -1;
}

static void subs_name(int k, char *buf, size_t n)
{
    if (k < 0)
        snprintf(buf, n, "off");
    else if (k < P.nsids)
        track_name(P.in[0].fmt->streams[P.sids[k]], buf, n);
    else
        snprintf(buf, n, "%s", P.ext[k - P.nsids].name);
}

static void chapter_step(int dir)
{
    double t = play_time();
    int c = 0;
    for (int i = 0; i < P.nchap; i++)
        if (P.chapters[i] <= t + 0.5)
            c = i;
    int to = dir > 0 ? c + 1 : t - P.chapters[c] > 3 ? c : c - 1;
    if (to < 0 || to >= P.nchap)
        return;
    char msg[160];
    AVDictionaryEntry *title = av_dict_get(P.in[0].fmt->chapters[to]->metadata, "title", NULL, 0);
    snprintf(msg, sizeof msg, "Chapter %d/%d%s%s", to + 1, P.nchap, title ? ": " : "", title ? title->value : "");
    osd_message(msg, 2);
    seek_to(P.chapters[to] + 0.01, dir < 0);
}

static void set_flags(void)
{
    static const char *names[] = { "", "Repeat this one", "Repeat all" };
    snprintf(P.flags, sizeof P.flags, "%s", names[P.repeat]);
}

static void file_action(const struct osd_event *e)
{
    char msg[256], name[160];
    switch (e->action) {
    case OSD_AUDIO:
        if (P.naids < 2) {
            osd_message(P.naids ? "This video has one audio track" : "This video has no sound", 2);
            break;
        }
        P.switch_aid = (P.aid + 1) % P.naids;
        track_name(P.ain->fmt->streams[P.aids[P.switch_aid]], name, sizeof name);
        snprintf(msg, sizeof msg, "Audio %d/%d: %s", P.switch_aid + 1, P.naids, name);
        osd_message(msg, 3);
        seek_to(play_time(), 1);   /* the new track from here */
        break;
    case OSD_SUBS:
        if (!P.nsids && !P.next) {
            osd_message("No subtitles: a .srt file with the video's name next to it is shown too", 3);
            break;
        }
        subs_select(P.ssel + 1 < P.nsids + P.next ? P.ssel + 1 : -1);
        subs_name(P.ssel, name, sizeof name);
        snprintf(msg, sizeof msg, "Subtitles %s", name);
        osd_message(msg, 2);
        break;
    case OSD_ASPECT:
        snprintf(msg, sizeof msg, "Picture: %s", aspect_next());
        osd_message(msg, 2);
        break;
    case OSD_REPEAT: {
        static const char *names[] = { "Repeat off", "Repeat this video", "Repeat all the videos of the folder" };
        P.repeat = (P.repeat + 1) % 3;
        set_flags();
        osd_message(names[P.repeat], 2);
        break;
    }
    default:
        break;
    }
}

static void clock_str(char *buf, size_t n, double t)
{
    int s = t > 0 ? (int)t : 0;
    if (s >= 3600)
        snprintf(buf, n, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    else
        snprintf(buf, n, "%d:%02d", s / 60, s % 60);
}

/* the info panel: what the file is (the heading is the title) */
static void file_info(const char *url)
{
    char t[4096], a[160], d[32];
    AVFormatContext *f = P.in[0].fmt;
    int n = snprintf(t, sizeof t, "%s\n", P.title ? P.title : url);
    const char *slash = strrchr(url, '/');
    struct stat stt;
    clock_str(d, sizeof d, P.duration);
    n += snprintf(t + n, sizeof t - n, "%s\n%s, %s", strstr(url, "://") ? "(network)" : slash ? slash + 1 : url,
                  f->iformat->long_name ? f->iformat->long_name : f->iformat->name, d);
    if (!strstr(url, "://") && stat(url, &stt) == 0)
        n += snprintf(t + n, sizeof t - n, ", %.1f %s", stt.st_size >= 1 << 30 ? stt.st_size / 1073741824.0 : stt.st_size / 1048576.0,
                      stt.st_size >= 1 << 30 ? "GB" : "MB");
    if (f->bit_rate > 0)
        n += snprintf(t + n, sizeof t - n, ", %.1f Mbit/s", f->bit_rate / 1e6);
    AVCodecParameters *v = P.vst->codecpar;
    AVRational fr = av_guess_frame_rate(f, P.vst, NULL);
    n += snprintf(t + n, sizeof t - n, "\n\nVideo: %s %dx%d, %.3g fps", avcodec_get_name(v->codec_id), v->width, v->height,
                  fr.num ? av_q2d(fr) : 0);
    for (int i = 0; i < P.naids; i++) {
        track_name(P.ain->fmt->streams[P.aids[i]], a, sizeof a);
        n += snprintf(t + n, sizeof t - n, "\n%s %d: %s", i ? "      " : "Audio", i + 1, a);
    }
    for (int i = 0; i < P.nsids + P.next; i++) {
        subs_name(i, a, sizeof a);
        n += snprintf(t + n, sizeof t - n, "\n%s %d: %s", i ? "          " : "Subtitles", i + 1, a);
    }
    if (P.nchap > 1)
        snprintf(t + n, sizeof t - n, "\nChapters: %d (page+ / page-)", P.nchap);
    osd_info(t);
}

static void file_info_extra(void)
{
    char t[512], mode[32] = "", a[160] = "none", sub[160];
    int w = sysfs_int(SYS "video/frame_width"), h = sysfs_int(SYS "video/frame_height");
    read_text(SYS "display/mode", mode, sizeof mode);
    if (P.ast)
        track_name(P.ast, a, sizeof a);
    subs_name(P.ssel, sub, sizeof sub);
    snprintf(t, sizeof t, "Playing: %dx%d, %s\nSound: %s%s\nSubtitles: %s\nOutput: %s%s\n%s", w, h,
             ASPECTS[P.aspect >= 0 && P.aspect < 4 ? P.aspect : 0].name, a,
             P.raw ? " (passed through)" : "", sub, mode, P.saved_mode ? " (matched to the video)" : "", P.flags);
    osd_info_extra(t);
}

/* --probe: what a file is, as JSON in ffprobe's shape (the box has no ffprobe; Files shows it) */
static void json_str(const char *v)
{
    putchar('"');
    for (const unsigned char *c = (const unsigned char *)(v ? v : ""); *c; c++) {
        if (*c == '"' || *c == '\\')
            printf("\\%c", *c);
        else if (*c < 0x20)
            printf("\\u%04x", *c);
        else
            putchar(*c);
    }
    putchar('"');
}

static void json_tags(AVDictionary *d)
{
    AVDictionaryEntry *e = NULL;
    int first = 1;
    printf("{");
    while ((e = av_dict_get(d, "", e, AV_DICT_IGNORE_SUFFIX))) {
        printf("%s", first ? "" : ",");
        json_str(e->key);
        putchar(':');
        json_str(e->value);
        first = 0;
    }
    printf("}");
}

static int probe(const char *url)
{
    AVFormatContext *f = NULL;
    if (avformat_open_input(&f, url, NULL, NULL) < 0 || avformat_find_stream_info(f, NULL) < 0) {
        printf("{}\n");
        return 1;
    }
    printf("{\"format\":{\"format_name\":");
    json_str(f->iformat->name);
    printf(",\"format_long_name\":");
    json_str(f->iformat->long_name);
    printf(",\"duration\":\"%.3f\",\"bit_rate\":\"%" PRId64 "\",\"tags\":", f->duration > 0 ? f->duration / (double)AV_TIME_BASE : 0,
           f->bit_rate);
    json_tags(f->metadata);
    printf("},\"streams\":[");
    for (unsigned i = 0; i < f->nb_streams; i++) {
        AVStream *st = f->streams[i];
        AVCodecParameters *c = st->codecpar;
        const AVCodecDescriptor *d = avcodec_descriptor_get(c->codec_id);
        printf("%s{\"index\":%u,\"codec_type\":", i ? "," : "", i);
        json_str(av_get_media_type_string(c->codec_type));
        printf(",\"codec_name\":");
        json_str(avcodec_get_name(c->codec_id));
        printf(",\"codec_long_name\":");
        json_str(d ? d->long_name : NULL);
        const char *prof = avcodec_profile_name(c->codec_id, c->profile);
        printf(",\"profile\":");
        json_str(prof);
        if (c->bit_rate > 0)
            printf(",\"bit_rate\":\"%" PRId64 "\"", c->bit_rate);
        if (c->codec_type == AVMEDIA_TYPE_VIDEO) {
            AVRational fr = av_guess_frame_rate(f, st, NULL);
            AVRational sar = av_guess_sample_aspect_ratio(f, st, NULL);
            int dw = c->width, dh = c->height;
            if (sar.num && sar.den)
                av_reduce(&dw, &dh, (int64_t)c->width * sar.num, (int64_t)c->height * sar.den, 1 << 20);
            else
                av_reduce(&dw, &dh, c->width, c->height, 1 << 20);
            printf(",\"width\":%d,\"height\":%d,\"avg_frame_rate\":\"%d/%d\",\"display_aspect_ratio\":\"%d:%d\",\"pix_fmt\":",
                   c->width, c->height, fr.num, fr.den ? fr.den : 1, dw, dh);
            json_str(av_get_pix_fmt_name(c->format));
        } else if (c->codec_type == AVMEDIA_TYPE_AUDIO) {
            char layout[64] = "";
            av_channel_layout_describe(&c->ch_layout, layout, sizeof layout);
            printf(",\"sample_rate\":\"%d\",\"channels\":%d,\"channel_layout\":", c->sample_rate, c->ch_layout.nb_channels);
            json_str(layout);
        }
        printf(",\"tags\":");
        json_tags(st->metadata);
        printf("}");
    }
    printf("],\"chapters\":[");
    for (unsigned i = 0; i < f->nb_chapters; i++) {
        AVChapter *ch = f->chapters[i];
        printf("%s{\"start_time\":\"%.3f\",\"tags\":", i ? "," : "", ch->start * av_q2d(ch->time_base));
        json_tags(ch->metadata);
        printf("}");
    }
    printf("]}\n");
    avformat_close_input(&f);
    return 0;
}

/* --control: a command line from stdin */
static void control_command(char *line)
{
    if (!strcmp(line, "pause") || !strcmp(line, "play")) {
        if (P.user_paused != (line[1] == 'a'))
            toggle_pause();
    } else if (!strncmp(line, "seek ", 5)) {
        double to = atof(line + 5);
        seek_to(to, to < play_time());
    } else if (!strcmp(line, "quit")) {
        P.quit = 1;
    }
}

/* --control: what came on stdin; its end (the caller gone) quits */
static void control_read(void)
{
    static char cmd[4096];
    static size_t ncmd;
    ssize_t r = read(0, cmd + ncmd, sizeof cmd - 1 - ncmd);
    if (r == 0 || (r < 0 && errno != EAGAIN && errno != EINTR)) {
        P.quit = 1;
        return;
    }
    if (r < 0)
        return;
    ncmd += r;
    cmd[ncmd] = 0;
    char *nl, *p = cmd;
    while ((nl = strchr(p, '\n')) && !P.quit) {
        *nl = 0;
        if (*p)
            control_command(p);
        p = nl + 1;
    }
    ncmd -= p - cmd;
    memmove(cmd, p, ncmd);
    if (ncmd == sizeof cmd - 1)
        ncmd = 0;
}

static void usage(void)
{
    fputs("usage: amplay [-v] [--audio URL] [--start SECONDS] [--title TEXT] [--program N]\n"
          "              [--aid N] [--sid N] [--repeat 0|1|2] [--report] [--control] URL\n"
          "       amplay --tv [--banner SECONDS]\n"
          "       amplay --probe URL                (what it is, as JSON in ffprobe's shape)\n"
          "       amplay --reset\n", stderr);
    exit(2);
}

int main(int argc, char **argv)
{
    static const struct option opts[] = {
        { "audio", required_argument, NULL, 'a' },
        { "start", required_argument, NULL, 's' },
        { "reset", no_argument, NULL, 'r' },
        { "title", required_argument, NULL, 't' },
        { "tv", no_argument, NULL, 'T' },
        { "banner", required_argument, NULL, 'b' },
        { "program", required_argument, NULL, 'p' },
        { "verbose", no_argument, NULL, 'v' },
        { "aid", required_argument, NULL, 'A' },
        { "sid", required_argument, NULL, 'S' },
        { "repeat", required_argument, NULL, 'R' },
        { "report", no_argument, NULL, 'P' },
        { "probe", required_argument, NULL, 'Q' },
        { "control", no_argument, NULL, 'C' },
        { 0 }
    };
    const char *audio_url = NULL;
    double start = 0;
    int c, banner = 0, aid = -1, sid = -1;
    P.program = -1;
    while ((c = getopt_long(argc, argv, "a:s:rt:vTb:p:A:S:R:PQ:C", opts, NULL)) != -1) {
        switch (c) {
        case 'a': audio_url = optarg; break;
        case 's': start = atof(optarg); break;
        case 'r': display_off(); restore_mode(); return 0;
        case 'A': aid = atoi(optarg); break;
        case 'S': sid = atoi(optarg); break;
        case 'R': P.repeat = atoi(optarg) % 3; break;
        case 'P': P.report = 1; break;
        case 'C': P.control = 1; break;
        case 'Q': av_log_set_level(AV_LOG_QUIET); return probe(optarg);
        case 't': P.title = optarg; break;
        case 'T': P.tv = 1; break;
        case 'b': banner = (int)(atof(optarg) * 1000); break;
        case 'p': P.program = atoi(optarg); break;
        case 'v': P.verbose = 1; break;
        default: usage();
        }
    }
    /* the H.264 parser complains about the cut packets right after a seek, and before the first
       keyframe of a channel; harmless */
    av_log_set_level(P.verbose && (!P.tv || getenv("AMPLAY_DEBUG")) ? AV_LOG_WARNING : AV_LOG_FATAL);
    if (P.tv) {
        if (optind != argc)
            usage();
        if (banner)
            osd_set_hide_after(banner);
        return tv_main();
    }
    if (optind != argc - 1)
        usage();
    avformat_network_init();

    if (open_input(&P.in[0], argv[optind]) < 0)
        return 1;
    P.nin = 1;
    int v = -1;
    if (P.program >= 0) {   /* a service of a multiplex: its first video stream */
        for (unsigned i = 0; i < P.in[0].fmt->nb_programs && v < 0; i++) {
            AVProgram *prg = P.in[0].fmt->programs[i];
            for (unsigned j = 0; prg->id == P.program && j < prg->nb_stream_indexes && v < 0; j++)
                if (P.in[0].fmt->streams[prg->stream_index[j]]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                    v = prg->stream_index[j];
        }
        if (v < 0) {
            logmsg("no video in program %d", P.program);
            return 3;
        }
    } else {
        v = av_find_best_stream(P.in[0].fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    }
    if (v < 0) {
        logmsg("no video stream");
        return 3;
    }
    P.in[0].vidx = v;
    P.vst = P.in[0].fmt->streams[v];
    P.vtb = P.vst->time_base;
    for (size_t i = 0; i < sizeof CODECS / sizeof *CODECS; i++)
        if (CODECS[i].id == P.vst->codecpar->codec_id)
            P.codec = &CODECS[i];
    if (!P.codec) {
        logmsg("no hardware decoder for %s", avcodec_get_name(P.vst->codecpar->codec_id));
        return 3;
    }

    struct input *ain = &P.in[0];
    if (audio_url) {
        if (open_input(&P.in[1], audio_url) < 0)
            return 1;
        ain = &P.in[P.nin++];
    }
    P.ain = ain;
    /* the audio tracks (of the same program as the video), the best one by default; the video's
       subtitle streams */
    AVProgram *prog = av_find_program_from_stream(P.in[0].fmt, NULL, v);
    int best = av_find_best_stream(ain->fmt, AVMEDIA_TYPE_AUDIO, -1, ain == &P.in[0] ? v : -1, NULL, 0);
    for (unsigned i = 0; i < ain->fmt->nb_streams; i++) {
        AVStream *st = ain->fmt->streams[i];
        int in_prog = ain != &P.in[0] || !prog;
        for (unsigned j = 0; !in_prog && j < prog->nb_stream_indexes; j++)
            in_prog = prog->stream_index[j] == i;
        if (!in_prog || st->disposition & AV_DISPOSITION_ATTACHED_PIC)
            continue;
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && P.naids < 16) {
            if ((int)i == best && aid < 0)
                aid = P.naids;
            P.aids[P.naids++] = i;
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE && ain == &P.in[0] && P.nsids < 16 &&
                   avcodec_find_decoder(st->codecpar->codec_id)) {
            P.sids[P.nsids++] = i;
        }
    }
    for (unsigned i = 0; ain != &P.in[0] && i < P.in[0].fmt->nb_streams && P.nsids < 16; i++) {
        AVStream *st = P.in[0].fmt->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE && avcodec_find_decoder(st->codecpar->codec_id))
            P.sids[P.nsids++] = i;
    }
    choose_audio(aid >= 0 && aid < P.naids ? aid : 0);
    ext_find(argv[optind]);
    subs_select(sid);
    AVFormatContext *fc = P.in[0].fmt;
    for (unsigned i = 0; i < fc->nb_chapters && P.nchap < 256; i++)
        P.chapters[P.nchap++] = fc->chapters[i]->start * av_q2d(fc->chapters[i]->time_base) -
                                (fc->start_time != AV_NOPTS_VALUE ? fc->start_time / (double)AV_TIME_BASE : 0);
    set_flags();

    const AVBitStreamFilter *bsf = av_bsf_get_by_name(P.codec->bsf);
    if (av_bsf_alloc(bsf, &P.bsf) < 0 || avcodec_parameters_copy(P.bsf->par_in, P.vst->codecpar) < 0) {
        logmsg("bitstream filter setup failed");
        return 1;
    }
    P.bsf->time_base_in = P.vst->time_base;
    av_bsf_init(P.bsf);

    AVFormatContext *f0 = P.in[0].fmt;
    P.start_time = f0->start_time != AV_NOPTS_VALUE ? av_rescale(f0->start_time, PTS_HZ, AV_TIME_BASE) : 0;
    P.duration = f0->duration > 0 ? f0->duration / (double)AV_TIME_BASE : 0;
    if (!P.title) {
        AVDictionaryEntry *t = av_dict_get(f0->metadata, "title", NULL, 0);
        const char *slash = strrchr(argv[optind], '/');
        P.title = t ? t->value : strstr(argv[optind], "://") ? NULL : slash ? slash + 1 : argv[optind];
    }

    if (P.verbose)
        logmsg("video %s %dx%d, audio %s", avcodec_get_name(P.vst->codecpar->codec_id), P.vst->codecpar->width,
               P.vst->codecpar->height, P.ast ? avcodec_get_name(P.ast->codecpar->codec_id) : "none");

    for (int i = 0; i < P.nin; i++)
        if (start > 0)
            av_seek_frame(P.in[i].fmt, -1, (int64_t)(start * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);

    pq_init(&P.vq, 16 << 20);
    pq_init(&P.aq, 2 << 20);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    signal(SIGPIPE, SIG_IGN);
    sigaction(SIGUSR1, &(struct sigaction){ .sa_handler = on_wake }, NULL);

    /* With frame-rate automation on, closing the decoder crashes the kernel (see ozone-hw) */
    if (!fr_auto_off()) {
        logmsg("can't turn off " SYS "tv/policy_fr_auto, refusing to start");
        return 1;
    }
    P.x11 = getenv("DISPLAY") && osd_open(0) == 0;
    if (!P.x11)
        kbd_open();
    else
        kbd[nkbd++] = (struct pollfd){ .fd = osd_fd(), .events = POLLIN };
    if (P.x11)
        file_info(argv[optind]);
    mixer_open();
    AVRational vfr = av_guess_frame_rate(P.in[0].fmt, P.vst, NULL);
    match_rate(vfr.num ? av_q2d(vfr) : 0);
    display_on();
    if (start > 0)
        P.last_pos = start;
    int status = session_start() < 0;
    if (P.control) {
        fcntl(0, F_SETFL, O_NONBLOCK);
        kbd[nkbd] = (struct pollfd){ .fd = 0, .events = POLLIN };
    }
    int64_t told_at = 0;
    while (!P.quit && !status) {
        /* right after the start or a pause the kernel moves pcrscr (see sync_clock, set_paused),
           so catch that quickly */
        int fast = !P.started || now_us() - P.started_at < 2000000 || now_us() - P.resumed_at < 1000000;
        int ready = poll(kbd, nkbd + P.control, fast ? 20 : 100) > 0;
        if (P.control) {
            if (kbd[nkbd].revents & (POLLIN | POLLHUP))
                control_read();
            if (now_us() - told_at > 1000000) {
                printf("pos %.1f %.1f %s\n", play_time(), P.duration,
                       P.user_paused ? "paused" : !P.started || P.buffering ? "loading" : "playing");
                fflush(stdout);
                told_at = now_us();
            }
        }
        if (P.x11) {
            struct osd_event ev[8];
            int n = osd_poll(ev, 8);
            for (int i = 0; i < n && !P.quit; i++)
                do_action(&ev[i]);
            if (P.ssel >= P.nsids)
                ext_update();
            if (P.nsids)   /* the queue goes on even with another choice, or none */
                subs_update();
            static int64_t info_at;
            if (osd_info_open() && now_us() - info_at > 1000000) {
                file_info_extra();
                info_at = now_us();
            }
            struct osd_state st = {
                .pos = play_time(), .dur = P.duration, .paused = P.user_paused, .loading = !P.started || P.buffering,
                .volume = P.raw ? 100 : saved_volume >= 0 ? saved_volume : volume_get(), .muted = saved_volume >= 0,
                .title = P.title, .signal = -1, .marks = P.chapters, .nmarks = P.nchap > 1 ? P.nchap : 0,
                .flags = P.flags[0] ? P.flags : NULL,
            };
            osd_update(&st);
        } else if (ready) {
            for (int i = 0; i < nkbd; i++) {
                struct input_event ev[16];
                ssize_t n = (kbd[i].revents & POLLIN) ? read(kbd[i].fd, ev, sizeof ev) : 0;
                for (int j = 0; j < n / (ssize_t)sizeof *ev; j++)
                    if (ev[j].type == EV_KEY && ev[j].value != 0)   /* press or autorepeat */
                        handle_key(ev[j].code, ev[j].value == 2);
            }
        }
        sync_clock();
        check_buffer();
        if (finished()) {
            if (P.repeat == 1) {   /* this one again */
                seek_to(0, 1);
                continue;
            }
            P.ended = 1;
            break;
        }
    }
    double at = play_time();
    session_stop();
    raw_close();
    if (P.x11) {
        nkbd = 0;
        osd_close();
    } else {
        kbd_close();
    }
    display_off();
    restore_mode();
    if (P.report) {   /* for the caller: where to resume, and what was chosen */
        printf("position %.1f\nduration %.1f\nended %d\naid %d\nsid %d\nrepeat %d\n", at, P.duration, P.ended,
               P.aid, P.ssel, P.repeat);
        fflush(stdout);
    }
    return status ? status : P.exit_status;
}
