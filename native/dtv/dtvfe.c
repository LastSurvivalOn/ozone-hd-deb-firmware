/*
 * dtvfe: drives the box's DVB-T/T2 and DVB-C frontend from native Linux.
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
 *   tunec <kHz>                            -> the same, cable (DVB-C, any symbol rate and QAM)
 *   status                                 -> "status <locked> <strength> <quality>"
 *   antenna <0|1>                          -> "antenna <0|1>": the 5 V for an active antenna, never
 *                                             on while tuned to cable
 *   quit
 * On start it prints "ready <frontend type>" or "error <reason>" and exits.
 *
 * Interface of libth_dtv_external_c.so, from its disassembly and the TV app's middleware:
 *   External_Init(); PI_System_Start(); PI_Tuner_Init(&type, &cfg[36]);
 *   PI_Tuner_DVB_T_StartLock(tuner, &params) and PI_Tuner_DVB_T_CheckSignalStatus(tuner, &status);
 *   PI_Tuner_DVB_T_SetAntennaPower(tuner, mode): mode 5 ("5V") sets the power GPIO, anything else
 *   clears it.
 *
 * DVB-C, which the TV app's own code never calls (from the library's exports): the demodulator's
 * work mode, AVL_SetWorkMode(0 DVB-C / 2 DVB-T/T2), loads its firmware for that standard and does
 * nothing when it is in that mode already; MxL608_Lock(&tuner) tunes the MxL608 in the mode at
 * tuner+4 (0: cable) to the Hz at +8 with the bandwidth in Hz at +16, inside
 * AVL_Demod_I2CBypassOn(0)/Off(0); AVL_Demod_DVBCAutoLock(0) finds the symbol rate and QAM, and
 * AVL_Demod_GetLockStatus(&u8, 0) says when it has. PI_Tuner_DVB_T_CheckSignalStatus reads the
 * DVB-C demodulator too. The board's DVB-C settings, AVL_DVBCConfigChip0 (input path, IF Hz, symbol
 * rate, spectrum, standard), have an IF of 36 MHz, but the MxL608 puts out 5 MHz as for DVB-T: no
 * lock with them. They are copied when the demodulator starts, so they're changed before that.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
static int (*PI_Tuner_DVB_T_SetAntennaPower)(int tuner, int mode);
static int (*AVL_SetWorkMode)(int mode);
static int (*AVL_Demod_I2CBypassOn)(int chip);
static int (*AVL_Demod_I2CBypassOff)(int chip);
static int (*MxL608_Lock)(uint8_t *tuner);
static int (*AVL_Demod_DVBCAutoLock)(int chip);
static int (*AVL_Demod_GetLockStatus)(uint8_t *locked, int chip);

enum { AVL_DVBC = 0, AVL_DVBTX = 2 };
#define CABLE_IF_HZ 5000000
#define CABLE_LOCK_MS 3000   /* locks seen after 0.9-2.9 s */

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

static void tune_cable(unsigned khz)
{
    uint8_t tuner[64], locked = 0;
    memset(tuner, 0, sizeof tuner);
    *(uint32_t *)(tuner + 4) = 0;   /* the MxL608's cable mode */
    *(uint32_t *)(tuner + 8) = khz * 1000;
    *(uint32_t *)(tuner + 16) = 8000000;
    AVL_SetWorkMode(AVL_DVBC);
    AVL_Demod_I2CBypassOn(0);
    MxL608_Lock(tuner);
    AVL_Demod_I2CBypassOff(0);
    AVL_Demod_DVBCAutoLock(0);
    for (int ms = 0; ms < CABLE_LOCK_MS && !locked; ms += 100) {
        usleep(100000);
        AVL_Demod_GetLockStatus(&locked, 0);
    }
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
    PI_Tuner_DVB_T_SetAntennaPower = sym(ext, "PI_Tuner_DVB_T_SetAntennaPower");
    AVL_SetWorkMode = sym(ext, "AVL_SetWorkMode");
    AVL_Demod_I2CBypassOn = sym(ext, "AVL_Demod_I2CBypassOn");
    AVL_Demod_I2CBypassOff = sym(ext, "AVL_Demod_I2CBypassOff");
    MxL608_Lock = sym(ext, "MxL608_Lock");
    AVL_Demod_DVBCAutoLock = sym(ext, "AVL_Demod_DVBCAutoLock");
    AVL_Demod_GetLockStatus = sym(ext, "AVL_Demod_GetLockStatus");
    ((uint32_t *)sym(ext, "AVL_DVBCConfigChip0"))[1] = CABLE_IF_HZ;

    External_Init();
    PI_System_Start();
    if (PI_Tuner_Init(&type, cfg) == 0) {
        printf("error no frontend found\n");
        return 1;
    }
    printf("ready %u\n", type);

    /* the antenna power asked for, and whether it is on: off on a cable network, whose amplifiers
     * aren't made for 5 V from a receiver */
    unsigned antenna = 0, cable = 0;
    while (fgets(line, sizeof line, stdin)) {
        unsigned khz = 0, bw = 8, plp = 0, on = 0;
        if (sscanf(line, "tune %u %u %u", &khz, &bw, &plp) >= 1) {
            struct tune_params p = { khz, (uint8_t)bw, 0, 0, (uint8_t)plp, plp_info };
            AVL_SetWorkMode(AVL_DVBTX);
            if (cable && antenna)
                PI_Tuner_DVB_T_SetAntennaPower(0, 5);
            cable = 0;
            PI_Tuner_DVB_T_StartLock(0, &p);
            status("tune");
        } else if (sscanf(line, "tunec %u", &khz) == 1) {
            if (!cable && antenna)
                PI_Tuner_DVB_T_SetAntennaPower(0, 0);
            cable = 1;
            tune_cable(khz);
            status("tune");
        } else if (sscanf(line, "antenna %u", &on) == 1) {
            antenna = on ? 1 : 0;
            if (!cable)
                PI_Tuner_DVB_T_SetAntennaPower(0, antenna ? 5 : 0);
            printf("antenna %u\n", antenna);
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
