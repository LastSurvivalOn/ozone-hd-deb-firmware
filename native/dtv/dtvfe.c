/*
 * dtvfe: drives the box's DVB-T/T2 frontend from native Linux.
 *
 * The kernel has a DVB demux but no frontend driver: on Android, the TV app's own libraries talk to
 * the demodulator (Availink AVL6762) and tuner (MaxLinear MxL608) over /dev/i2c-2, with reset on
 * GPIO 74. Rather than re-implementing those (firmware patch, board settings and all), this loads
 * the very same libraries from the Android system partition and calls them. It is therefore an
 * Android (bionic) program: built with the NDK, run with /system mounted and an empty Android
 * property area at /dev/__properties__ (see ozone-dtv).
 *
 * Protocol, one command per line on stdin, one reply line on stdout:
 *   tune <kHz> [<bandwidth MHz> [<plp>]]   -> "tune <locked 0/1> <strength %> <quality %>"
 *   status                                 -> "status <locked> <strength> <quality>"
 *   quit
 * On start it prints "ready <frontend type>" or "error <reason>" and exits.
 *
 * Interface of libth_dtv_external_c.so, from its disassembly and the TV app's middleware:
 *   External_Init(); PI_System_Start(); PI_Tuner_Init(&type, &cfg[36]);
 *   PI_Tuner_DVB_T_StartLock(tuner, &params) and PI_Tuner_DVB_T_CheckSignalStatus(tuner, &status).
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct tune_params {
    uint32_t freq_khz;
    uint8_t bandwidth_mhz;
    uint8_t mode;             /* 0: DVB-T/T2 auto; 3 and 4 select other standards */
    uint8_t reserved;
    uint8_t plp;
    void *plp_info;           /* >= 512 bytes, the driver stores the PLP list here */
};

struct signal_status {
    uint32_t unknown0;
    uint32_t unknown4;
    float extra8, extra12;
    uint32_t locked;          /* +16 */
    uint8_t strength;         /* +20, 0..100 */
    uint8_t quality;          /* +21, 0..100 */
    uint8_t flags[10];
};

static void (*External_Init)(void);
static int (*PI_System_Start)(void);
static int (*PI_Tuner_Init)(uint32_t *type, uint8_t *cfg);
static int (*PI_Tuner_DVB_T_StartLock)(int tuner, struct tune_params *p);
static int (*PI_Tuner_DVB_T_CheckSignalStatus)(int tuner, struct signal_status *s);

static void *sym(void *lib, const char *name)
{
    void *p = dlsym(lib, name);
    if (!p) {
        printf("error missing %s\n", name);
        exit(1);
    }
    return p;
}

static void status(const char *what)
{
    struct signal_status s;
    memset(&s, 0, sizeof s);
    PI_Tuner_DVB_T_CheckSignalStatus(0, &s);
    printf("%s %u %u %u\n", what, s.locked ? 1 : 0, s.strength, s.quality);
}

int main(void)
{
    static uint8_t plp_info[1024];
    uint8_t cfg[36] = { 0 };
    uint32_t type = 0;
    char line[128];

    setvbuf(stdout, NULL, _IOLBF, 0);
    /* libth_dtv_pi brings the Amlogic AM_* layer the frontend code calls into */
    void *pi = dlopen("libth_dtv_pi.so", RTLD_NOW | RTLD_GLOBAL);
    void *ext = pi ? dlopen("libth_dtv_external_c.so", RTLD_NOW | RTLD_GLOBAL) : NULL;
    if (!ext) {
        printf("error dlopen %s\n", dlerror());
        return 1;
    }
    External_Init = sym(ext, "External_Init");
    PI_System_Start = sym(pi, "PI_System_Start");
    PI_Tuner_Init = sym(ext, "PI_Tuner_Init");
    PI_Tuner_DVB_T_StartLock = sym(ext, "PI_Tuner_DVB_T_StartLock");
    PI_Tuner_DVB_T_CheckSignalStatus = sym(ext, "PI_Tuner_DVB_T_CheckSignalStatus");

    External_Init();
    PI_System_Start();
    if (PI_Tuner_Init(&type, cfg) == 0) {
        printf("error no frontend found\n");
        return 1;
    }
    printf("ready %u\n", type);

    while (fgets(line, sizeof line, stdin)) {
        unsigned khz = 0, bw = 8, plp = 0;
        if (sscanf(line, "tune %u %u %u", &khz, &bw, &plp) >= 1) {
            struct tune_params p = { khz, (uint8_t)bw, 0, 0, (uint8_t)plp, plp_info };
            PI_Tuner_DVB_T_StartLock(0, &p);
            status("tune");
        } else if (!strncmp(line, "status", 6)) {
            status("status");
        } else if (!strncmp(line, "quit", 4)) {
            break;
        } else {
            printf("error unknown command\n");
        }
    }
    return 0;
}
