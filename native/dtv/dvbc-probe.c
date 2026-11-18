/* dvbc-probe: does the AVL6762 + MxL608 lock DVB-C here? Uses the TV app's libraries as dtvfe does,
 * then: demod mode 0 (AVL_DVBC), MxL608 in cable mode at each frequency, DVB-C auto lock (symbol rate
 * and QAM found by the demod), lock status for 5 s. Usage: [IF=Hz] [INV=n] [SR=sps] dvbc-probe kHz...
 * (IF, INV, SR override the board's DVB-C config before the demod is initialised). An experiment,
 * not installed: build it as build.sh builds dtvfe and run it with sudo while no ozone-tv uses the
 * tuner; see TODO.md, section 7. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void *sym(void *lib, const char *name)
{
    void *p = dlsym(lib, name);
    if (!p) { printf("missing %s\n", name); exit(1); }
    return p;
}

int main(int argc, char **argv)
{
    uint8_t cfg[36] = { 0 };
    uint32_t type = 0;
    setvbuf(stdout, NULL, _IOLBF, 0);
    void *pi = dlopen("libth_dtv_pi.so", RTLD_NOW | RTLD_GLOBAL);
    void *ext = pi ? dlopen("libth_dtv_external_c.so", RTLD_NOW | RTLD_GLOBAL) : NULL;
    if (!ext) { printf("dlopen %s\n", dlerror()); return 1; }
    void (*External_Init)(void) = sym(ext, "External_Init");
    int (*PI_System_Start)(void) = sym(pi, "PI_System_Start");
    int (*PI_Tuner_Init)(uint32_t *, uint8_t *) = sym(ext, "PI_Tuner_Init");
    int (*SetWorkMode)(int) = sym(ext, "AVL_SetWorkMode");
    int (*BypassOn)(int) = sym(ext, "AVL_Demod_I2CBypassOn");
    int (*BypassOff)(int) = sym(ext, "AVL_Demod_I2CBypassOff");
    int (*TunerLock)(void *) = sym(ext, "MxL608_Lock");
    int (*TunerStatus)(void *) = sym(ext, "MxL608_GetLockStatus");
    int (*AutoLockC)(int) = sym(ext, "AVL_Demod_DVBCAutoLock");
    int (*LockStatus)(uint8_t *, int) = sym(ext, "AVL_Demod_GetLockStatus");
    int (*ModInfo)(void *, int) = sym(ext, "AVL_Demod_DVBCGetModulationInfo");
    int (*GetSSI)(uint16_t *, int) = sym(ext, "AVL_Demod_GetSSI");

    /* the board's DVB-C settings: input path, IF Hz, symbol rate, spectrum, standard. Their IF is
     * 36 MHz (a reference tuner's), the MxL608 here puts out 5 MHz, as for DVB-T. Before the
     * demod's initialisation, which copies them */
    uint32_t *cc = sym(ext, "AVL_DVBCConfigChip0");
    printf("DVB-C config: %u %u %u %u %u\n", cc[0], cc[1], cc[2], cc[3], cc[4]);
    if (getenv("IF")) cc[1] = strtoul(getenv("IF"), NULL, 10);
    if (getenv("INV")) cc[3] = strtoul(getenv("INV"), NULL, 10);
    if (getenv("SR")) cc[2] = strtoul(getenv("SR"), NULL, 10);
    printf("now:          %u %u %u %u %u\n", cc[0], cc[1], cc[2], cc[3], cc[4]);
    External_Init();
    PI_System_Start();
    if (PI_Tuner_Init(&type, cfg) == 0) { printf("no frontend\n"); return 1; }
    printf("frontend %u\n", type);
    int r = SetWorkMode(0);
    printf("set mode DVB-C: %d\n", r);
    for (int i = 1; i < argc; i++) {
        unsigned khz = strtoul(argv[i], NULL, 10);
        uint8_t tuner[64];
        memset(tuner, 0, sizeof tuner);
        *(uint32_t *)(tuner + 4) = 0;              /* AVL_DVBC: MxL608 cable mode */
        *(uint32_t *)(tuner + 8) = khz * 1000;
        *(uint32_t *)(tuner + 16) = 8000000;
        BypassOn(0);
        int t = TunerLock(tuner);
        usleep(50000);
        TunerStatus(tuner);
        BypassOff(0);
        int a = AutoLockC(0);
        uint8_t lock = 0;
        int ms;
        for (ms = 0; ms < 5000 && !lock; ms += 100) {
            usleep(100000);
            LockStatus(&lock, 0);
        }
        uint16_t ssi = 0;
        GetSSI(&ssi, 0);
        printf("%u kHz: tuner %d/%u, autolock %d, lock %u after %d ms, ssi %u", khz, t, tuner[2], a, lock, ms, ssi);
        if (lock) {
            uint8_t info[32];
            memset(info, 0, sizeof info);
            ModInfo(info, 0);
            printf(", modinfo");
            for (int j = 0; j < 16; j++)
                printf(" %02x", info[j]);
        }
        printf("\n");
    }
    printf("set mode DVB-T/T2: %d\n", SetWorkMode(2));
    return 0;
}
