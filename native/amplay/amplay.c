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
 *   amplay [--audio URL] [--start SECONDS] [--title TEXT] URL     URL: a file or anything FFmpeg opens
 *   amplay --live [--program N] [--title TEXT] URL       broadcast TS, e.g. pipe:0 from ozone-tv
 *   amplay --reset                                 put the desktop back after a crash
 *
 * Keys: Space/P pause, Left/Right -/+10 s, PgDn/PgUp -/+60 s, Up/Down volume, M mute, Q/Esc quit.
 * Mouse (under X): click the progress bar to seek, the picture to pause, the wheel for volume.
 * Live: Left/Right and PgDn/PgUp (and the remote's channel keys) ask for the previous/next channel:
 * amplay exits with status 11/10 and the caller retunes.
 * Exit status 3 means the video codec has no hardware decoder (the caller can fall back to mpv).
 */
#define _GNU_SOURCE
#include <alsa/asoundlib.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
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
#include <time.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>

#include "osd.h"

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
#define SEEK_SHORT      10
#define SEEK_LONG       60
#define SYS             "/sys/class/"

struct codec_map {
    enum AVCodecID id;
    int vformat;              /* vformat_t */
    int dec_format;           /* VIDEO_DEC_FORMAT_* for SYSINFO */
    const char *dev, *bsf;
} CODECS[] = {
    { AV_CODEC_ID_H264,       2, 4, "/dev/amstream_vbuf", "h264_mp4toannexb" },
    { AV_CODEC_ID_HEVC,      11, 0, "/dev/amstream_hevc", "hevc_mp4toannexb" },
    { AV_CODEC_ID_MPEG2VIDEO, 0, 0, "/dev/amstream_vbuf", "null" },
    { AV_CODEC_ID_MPEG1VIDEO, 0, 0, "/dev/amstream_vbuf", "null" },
};

/* ---- packet queue -------------------------------------------------------------------------- */

struct pkt { AVPacket *p; struct pkt *next; };
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

static int pq_put(struct pq *q, AVPacket *p)
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
static int pq_get(struct pq *q, AVPacket **p)
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

static struct {
    struct input in[2];
    int nin;
    AVStream *vst, *ast;
    const struct codec_map *codec;
    AVBSFContext *bsf;
    AVCodecContext *actx;
    SwrContext *swr;
    snd_pcm_t *pcm;
    int can_pause;

    struct pq vq, aq;
    pthread_t vth, ath;
    int vfd, cntl;            /* stream port, /dev/amvideo */

    /* session state, reset on every (re)start */
    volatile int stop, started, paused, video_done, audio_done;
    int64_t first_vpts;       /* 90 kHz, -1 until the first packet */
    int64_t last_vpts;
    int64_t frame_dur;        /* 90 kHz, for streams without timestamps (raw .h264) */
    int no_pts;
    pthread_mutex_t clk_m;
    pthread_cond_t gate;
    int64_t aclock;           /* audio clock (90 kHz) when last measured, -1 if unknown */
    int64_t aclock_at;        /* CLOCK_MONOTONIC in us at that moment */
    int64_t wclock, wclock_at; /* the same for the wall clock used when there is no audio */
    int64_t ready_at, started_at;

    int64_t start_time;       /* 90 kHz: the stream's own start, so the clock shows 0:00 */
    double duration;          /* seconds, 0 if unknown */
    double last_pos;
    const char *title;
    int live, program;        /* --live, --program (-1: any) */
    int exit_status;
    int x11;                  /* controls drawn under X, or evdev without */

    volatile sig_atomic_t quit;
    int verbose;
} P = { .vfd = -1, .cntl = -1, .clk_m = PTHREAD_MUTEX_INITIALIZER, .gate = PTHREAD_COND_INITIALIZER };

static void logmsg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("amplay: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
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

static int64_t to_pts90(int64_t ts, AVRational tb)
{
    return av_rescale_q(ts, tb, (AVRational){ 1, PTS_HZ });
}

/* ---- display ------------------------------------------------------------------------------- */

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

static void display_on(void)
{
    char mode[32] = "", axis[64];
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

    sysfs_write(SYS "video/axis", axis);
    sysfs_write(SYS "video/screen_mode", "0");    /* keep the aspect ratio */
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
static struct pollfd kbd[MAX_KBD];
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

/* ---- demux, video and audio threads -------------------------------------------------------- */

static int interrupt_cb(void *arg)
{
    (void)arg;
    return P.quit;   /* not on seek: an interrupted read leaves the AVIOContext in an error state */
}

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
        struct pq *q = pkt->stream_index == in->vidx ? &P.vq : pkt->stream_index == in->aidx ? &P.aq : NULL;
        if (q && pq_put(q, av_packet_clone(pkt)) < 0)
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
        int64_t pts = P.no_pts ? (P.last_vpts < 0 ? 0 : P.last_vpts + P.frame_dur) : to_pts90(ts, P.vst->time_base);
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
    while ((r = pq_get(&P.vq, &pkt)) > 0) {
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
    P.aclock = end_pts - (int64_t)delay * PTS_HZ / AUDIO_RATE;
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
    int64_t a = P.ast && !P.audio_done ? audio_clock_now() : -1;
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

static void *audio_thread(void *arg)
{
    (void)arg;
    AVPacket *pkt;
    AVFrame *frame = av_frame_alloc();
    uint8_t *buf = NULL;
    int bufsamples = 0;

    /* hold back until the video decoder has a head start, so both begin together */
    pthread_mutex_lock(&P.clk_m);
    while (!P.started && !P.stop)
        pthread_cond_wait(&P.gate, &P.clk_m);
    pthread_mutex_unlock(&P.clk_m);

    while (!P.stop && pq_get(&P.aq, &pkt) > 0) {
        if (avcodec_send_packet(P.actx, pkt) < 0)
            logmsg("audio decode error");
        av_packet_free(&pkt);
        while (!P.stop && avcodec_receive_frame(P.actx, frame) == 0) {
            int64_t ts = frame->best_effort_timestamp;
            int64_t pts = ts != AV_NOPTS_VALUE ? to_pts90(ts, P.ast->time_base) : P.aclock;
            int max = swr_get_out_samples(P.swr, frame->nb_samples);
            if (max > bufsamples) {
                av_freep(&buf);
                av_samples_alloc(&buf, NULL, 2, max, AV_SAMPLE_FMT_S16, 0);
                bufsamples = max;
            }
            int n = swr_convert(P.swr, &buf, max, (const uint8_t **)frame->extended_data, frame->nb_samples);
            int64_t end = pts + (int64_t)frame->nb_samples * PTS_HZ / frame->sample_rate;
            for (int off = 0; off < n && !P.stop;) {
                audio_pause_wait();
                snd_pcm_sframes_t w = snd_pcm_writei(P.pcm, buf + off * 4, n - off);
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
            audio_clock_update(end);
            av_frame_unref(frame);
        }
    }
    /* let the device play out (the pcm is non-blocking, so no snd_pcm_drain) */
    snd_pcm_sframes_t left;
    while (!P.stop && snd_pcm_delay(P.pcm, &left) == 0 && left > 0) {
        usleep(50000);
        audio_clock_update(P.aclock + (int64_t)left * PTS_HZ / AUDIO_RATE);
    }
    av_freep(&buf);
    av_frame_free(&frame);
    P.audio_done = 1;
    return NULL;
}

/* ---- session: one continuous run of the decoder, restarted on every seek ------------------- */

static int session_start(void)
{
    const struct codec_map *c = P.codec;
    AVCodecParameters *par = P.vst->codecpar;
    AVRational fr = av_guess_frame_rate(P.in[0].fmt, P.vst, NULL);

    P.stop = P.started = P.paused = P.video_done = 0;
    P.audio_done = !P.ast;
    P.first_vpts = P.last_vpts = -1;
    P.aclock = P.wclock = -1;
    P.ready_at = 0;
    sysfs_write(SYS "video/disable_video", "2");  /* off until the first new frame */
    pq_reset(&P.vq);
    pq_reset(&P.aq);

    P.vfd = open(c->dev, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    P.cntl = open("/dev/amvideo", O_RDWR | O_CLOEXEC);
    if (P.vfd < 0 || P.cntl < 0) {
        logmsg("%s: %s", P.vfd < 0 ? c->dev : "/dev/amvideo", strerror(errno));
        return -1;
    }
    struct dec_sysinfo si = {
        .format = c->dec_format,
        .width = par->width,
        .height = par->height,
        /* frame duration in 1/96000 s */
        .rate = fr.num ? (unsigned)av_rescale(96000, fr.den, fr.num) : 0,
    };
    P.frame_dur = fr.num ? av_rescale(PTS_HZ, fr.den, fr.num) : PTS_HZ / 25;
    P.no_pts = 0;
    if (ioctl(P.vfd, AMSTREAM_IOC_VFORMAT, c->vformat) || ioctl(P.vfd, AMSTREAM_IOC_VID, 1) ||
        ioctl(P.vfd, AMSTREAM_IOC_SYSINFO, (unsigned long)&si) || ioctl(P.vfd, AMSTREAM_IOC_PORT_INIT, 0)) {
        logmsg("decoder init: %s", strerror(errno));
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

    for (int i = 0; i < P.nin; i++)
        pthread_create(&P.in[i].th, NULL, demux_thread, &P.in[i]);
    pthread_create(&P.vth, NULL, video_thread, NULL);
    if (P.ast)
        pthread_create(&P.ath, NULL, audio_thread, NULL);
    return 0;
}

static void session_stop(void)
{
    pthread_mutex_lock(&P.clk_m);
    P.stop = 1;
    pthread_cond_broadcast(&P.gate);
    pthread_mutex_unlock(&P.clk_m);
    pq_set(&P.vq, &P.vq.abort, 1);
    pq_set(&P.aq, &P.aq.abort, 1);
    for (int i = 0; i < P.nin; i++)
        pthread_join(P.in[i].th, NULL);
    pthread_join(P.vth, NULL);
    if (P.ast) {
        pthread_join(P.ath, NULL);
        snd_pcm_drop(P.pcm);
        snd_pcm_prepare(P.pcm);
        avcodec_flush_buffers(P.actx);
    }
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
    if (ioctl(P.vfd, AMSTREAM_IOC_VB_STATUS, (unsigned long)&p))
        return -1;
    return p.status.data_len;
}

static int64_t position(void)
{
    int64_t c = master_clock();
    return c < 0 ? P.first_vpts : c;
}

/* Called every 20-100 ms from the main loop */
static void sync_clock(void)
{
    if (!P.started) {
        int ready = P.video_done || vbuf_level() >= PREROLL_BYTES || P.last_vpts - P.first_vpts >= PREROLL_PTS;
        if (!ready || P.first_vpts < 0)
            return;
        /* Wait for the first frame to reach the video layer: the kernel then sets pcrscr 3 s
           behind it (its audio-master start-up hold). Starting the clock earlier makes the first
           frames late, and they get dropped. */
        if (!P.ready_at)
            P.ready_at = now_us();
        unsigned long pcr = 0;
        ioctl(P.vfd, AMSTREAM_IOC_PCRSCR, (unsigned long)&pcr);
        int32_t d = (int32_t)((uint32_t)pcr - (uint32_t)P.first_vpts);
        if (!(d < -2 * PTS_HZ && d > -4 * PTS_HZ) && now_us() - P.ready_at < 2000000)
            return;
        ioctl(P.vfd, AMSTREAM_IOC_SET_PCRSCR, (unsigned long)(uint32_t)P.first_vpts);
        P.wclock = P.first_vpts;
        P.wclock_at = P.started_at = now_us();
        pthread_mutex_lock(&P.clk_m);
        P.started = 1;
        pthread_cond_broadcast(&P.gate);
        pthread_mutex_unlock(&P.clk_m);
        if (P.verbose)
            logmsg("start at %.3f s", P.first_vpts / (double)PTS_HZ);
        return;
    }
    /* the kernel advances pcrscr every vsync; correct the drift against the audio clock */
    int64_t a = master_clock();
    if (a < 0 || P.paused)
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

static void toggle_pause(void)
{
    if (P.wclock >= 0) {   /* freeze or restart the wall clock */
        P.wclock = master_clock();
        P.wclock_at = now_us();
    }
    pthread_mutex_lock(&P.clk_m);
    P.paused = !P.paused;
    pthread_cond_broadcast(&P.gate);
    pthread_mutex_unlock(&P.clk_m);
    ioctl(P.cntl, AMSTREAM_IOC_VPAUSE, P.paused);
    int64_t a = master_clock();
    if (!P.paused && a >= 0)
        ioctl(P.vfd, AMSTREAM_IOC_SET_PCRSCR, (unsigned long)(uint32_t)a);
}

static void do_action(const struct osd_event *e)
{
    switch (e->action) {
    case OSD_PAUSE: toggle_pause(); break;
    case OSD_SEEK_REL:
        if (P.live) {   /* no timeline to move along: change the channel */
            P.exit_status = e->value > 0 ? 10 : 11;
            P.quit = 1;
        } else {
            seek((int)e->value);
        }
        break;
    case OSD_SEEK_ABS:
        if (!P.live)
            seek_to(e->value, e->value < play_time());
        break;
    case OSD_VOLUME: volume_change((int)e->value); break;
    case OSD_MUTE: mute_toggle(); break;
    case OSD_QUIT: P.quit = 1; break;
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
    case KEY_CHANNELUP: e = (struct osd_event){ OSD_SEEK_REL, SEEK_LONG }; break;
    case KEY_CHANNELDOWN: e = (struct osd_event){ OSD_SEEK_REL, -SEEK_LONG }; break;
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

/* ---- setup --------------------------------------------------------------------------------- */

static int open_input(struct input *in, const char *url)
{
    in->fmt = avformat_alloc_context();
    in->fmt->interrupt_callback = (AVIOInterruptCB){ interrupt_cb, NULL };
    AVDictionary *opts = NULL;
    av_dict_set(&opts, "reconnect", "1", 0);
    av_dict_set(&opts, "reconnect_streamed", "1", 0);
    av_dict_set(&opts, "reconnect_delay_max", "5", 0);
    if (P.live) {   /* zapping: two seconds of broadcast are enough to find the streams */
        av_dict_set(&opts, "analyzeduration", "2000000", 0);
        av_dict_set(&opts, "probesize", "2000000", 0);
    }
    int r = avformat_open_input(&in->fmt, url, NULL, &opts);
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
    const AVCodec *dec = avcodec_find_decoder(P.ast->codecpar->codec_id);
    P.actx = avcodec_alloc_context3(dec);
    if (!dec || avcodec_parameters_to_context(P.actx, P.ast->codecpar) < 0 || avcodec_open2(P.actx, dec, NULL) < 0) {
        logmsg("no audio decoder for %s", avcodec_get_name(P.ast->codecpar->codec_id));
        return -1;
    }
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    if (!P.actx->ch_layout.nb_channels)
        av_channel_layout_default(&P.actx->ch_layout, 2);
    if (swr_alloc_set_opts2(&P.swr, &stereo, AV_SAMPLE_FMT_S16, AUDIO_RATE, &P.actx->ch_layout,
                            P.actx->sample_fmt, P.actx->sample_rate, 0, NULL) < 0 || swr_init(P.swr) < 0) {
        logmsg("audio resampler setup failed");
        return -1;
    }
    /* Plain blocking writes, like aplay. Never ALSA's silence filling: with it the Amlogic HDMI
       audio driver hangs the kernel (the reason mpv uses ao=sdl here). */
    int r = snd_pcm_open(&P.pcm, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if (r >= 0)
        r = snd_pcm_set_params(P.pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, AUDIO_RATE, 1, 200000);
    if (r < 0) {
        logmsg("audio device: %s", snd_strerror(r));
        return -1;
    }
    snd_pcm_hw_params_t *hw;
    snd_pcm_hw_params_alloca(&hw);
    P.can_pause = snd_pcm_hw_params_current(P.pcm, hw) == 0 && snd_pcm_hw_params_can_pause(hw);
    return 0;
}

static void on_signal(int sig)
{
    (void)sig;
    P.quit = 1;
}

static void usage(void)
{
    fputs("usage: amplay [-v] [--audio URL] [--start SECONDS] [--title TEXT] URL\n"
          "       amplay --live [--program N] [--title TEXT] URL\n"
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
        { "live", no_argument, NULL, 'l' },
        { "program", required_argument, NULL, 'p' },
        { "verbose", no_argument, NULL, 'v' },
        { 0 }
    };
    const char *audio_url = NULL;
    double start = 0;
    int c;
    P.program = -1;
    while ((c = getopt_long(argc, argv, "a:s:rt:vlp:", opts, NULL)) != -1) {
        switch (c) {
        case 'a': audio_url = optarg; break;
        case 's': start = atof(optarg); break;
        case 'r': display_off(); return 0;
        case 't': P.title = optarg; break;
        case 'l': P.live = 1; break;
        case 'p': P.program = atoi(optarg); break;
        case 'v': P.verbose = 1; break;
        default: usage();
        }
    }
    if (optind != argc - 1)
        usage();

    /* the H.264 parser complains about the cut packets right after a seek; harmless */
    av_log_set_level(P.verbose ? AV_LOG_WARNING : AV_LOG_FATAL);
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
    /* related to the video stream: the audio of the same program */
    int a = av_find_best_stream(ain->fmt, AVMEDIA_TYPE_AUDIO, -1, ain == &P.in[0] ? v : -1, NULL, 0);
    if (a >= 0) {
        ain->aidx = a;
        P.ast = ain->fmt->streams[a];
        if (open_audio() < 0) {
            P.ast = NULL;
            ain->aidx = -1;
        }
    }

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

    /* With frame-rate automation on, closing the decoder crashes the kernel (see ozone-hw) */
    if (!fr_auto_off()) {
        logmsg("can't turn off " SYS "tv/policy_fr_auto, refusing to start");
        return 1;
    }
    P.x11 = getenv("DISPLAY") && osd_open() == 0;
    if (!P.x11)
        kbd_open();
    else
        kbd[nkbd++] = (struct pollfd){ .fd = osd_fd(), .events = POLLIN };
    mixer_open();
    display_on();
    if (start > 0)
        P.last_pos = start;
    int status = session_start() < 0;
    while (!P.quit && !status) {
        /* right after the start the kernel moves pcrscr (see sync_clock), so catch that quickly */
        int fast = !P.started || now_us() - P.started_at < 2000000;
        int ready = poll(kbd, nkbd, fast ? 20 : 100) > 0;
        if (P.x11) {
            struct osd_event ev[8];
            int n = osd_poll(ev, 8);
            for (int i = 0; i < n && !P.quit; i++)
                do_action(&ev[i]);
            struct osd_state st = {
                .pos = play_time(), .dur = P.duration, .paused = P.paused, .loading = !P.started,
                .volume = saved_volume >= 0 ? saved_volume : volume_get(), .muted = saved_volume >= 0,
                .title = P.title, .live = P.live,
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
        if (finished())
            break;
    }
    session_stop();
    if (P.x11) {
        nkbd = 0;
        osd_close();
    } else {
        kbd_close();
    }
    display_off();
    return status ? status : P.exit_status;
}
