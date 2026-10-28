/* An example: F9 builds it with gcc, F5 runs it in a terminal. */
#include <stdio.h>

int main(void)
{
    FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    int t;

    printf("Hello from the OzoneHD box!\n");
    if (f && fscanf(f, "%d", &t) == 1)
        printf("The processor is at %d °C.\n", t > 1000 ? t / 1000 : t);
    if (f)
        fclose(f);
    return 0;
}
