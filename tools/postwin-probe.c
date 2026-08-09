// postwin-probe - walks the RGB Fusion 2.0 command sequence one report at a
// time, printing the result of each, and stops at the first failure.
//
// It exists for one question: coming out of Windows, the controller answers
// INIT and then dies partway through a normal apply. This says which report it
// dies on, and whether pacing the reports is enough to stop it happening.
//
//   cc -O1 -o postwin-probe tools/postwin-probe.c
//   ./postwin-probe                 # 20 ms between reports, as the app now uses
//   ./postwin-probe --delay 0       # back-to-back, the behaviour that broke it
//   ./postwin-probe --delay 50      # if 20 still is not enough
//
// Run it as the FIRST thing after booting from Windows, with the GUI closed.
// Once the controller has hung, nothing here will get an answer out of it and
// only cutting power to the board brings it back.
#include <errno.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define LEN 64

static int fd;
static int delay_ms = 20;

static void pace(void)
{
    if (delay_ms <= 0)
        return;
    struct timespec ts = {delay_ms / 1000, (long)(delay_ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

// Returns 0 on success, -errno on failure.
static int put(const unsigned char *b, const char *what)
{
    pace();
    printf("  %-28s ", what);
    fflush(stdout);
    if (ioctl(fd, HIDIOCSFEATURE(LEN), b) < 0) {
        printf("FAIL  %s (errno %d)\n", strerror(errno), errno);
        return -errno;
    }
    printf("ok\n");
    return 0;
}

static int cmd(unsigned char c, unsigned char a0, const char *what)
{
    unsigned char b[LEN];
    memset(b, 0, LEN);
    b[0] = 0xCC;
    b[1] = c;
    b[2] = a0;
    return put(b, what);
}

// Zone i static, dim white - the shape of report the app sends.
static int zone(int i)
{
    unsigned char b[LEN];
    char what[32];
    memset(b, 0, LEN);
    b[0]  = 0xCC;
    b[1]  = (unsigned char)(0x20 + i);
    b[2]  = (unsigned char)(1u << i);
    b[11] = 0x01;  // Mode_Sel: static
    b[12] = 0x14;  // MaxBrightness
    b[14] = b[15] = b[16] = 0x40;
    snprintf(what, sizeof what, "SET zone%d (0x%02X)", i + 1, 0x20 + i);
    return put(b, what);
}

int main(int argc, char **argv)
{
    const char *path = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--delay") && i + 1 < argc)
            delay_ms = atoi(argv[++i]);
        else
            path = argv[i];
    }

    // The lighting collection is the second ITE interface; hidraw5 on the
    // development board. Pass a path if it landed somewhere else.
    if (!path)
        path = "/dev/hidraw5";

    fd = open(path, O_RDWR);
    if (fd < 0) {
        perror(path);
        return 1;
    }
    printf("%s, %d ms between reports\n\n", path, delay_ms);

    printf("stage 1: init\n");
    if (cmd(0x60, 0, "INIT (0x60)") < 0)
        goto dead;

    unsigned char info[LEN] = {0xCC};
    pace();
    printf("  %-28s ", "GET info");
    fflush(stdout);
    int n = ioctl(fd, HIDIOCGFEATURE(LEN), info);
    if (n < 0) {
        printf("FAIL  %s (errno %d)\n", strerror(errno), errno);
        goto dead;
    }
    printf("ok, %d bytes\n", n);
    printf("      product 0x%02X  fw %u.%u.%u.%u  SuppCmdFlag 0x%02X  StripDetect 0x%02X\n",
           info[1], info[4], info[5], info[6], info[7], info[11], info[3]);
    printf("      name \"%.28s\"\n\n", info + 12);

    // Everything below mirrors RgbFusion2::takeOverZones() + Config::apply().
    printf("stage 2: take over\n");
    if (cmd(0x31, 0, "BEAT off (0x31)") < 0)
        goto dead;
    if (info[11] & 0x02) {
        if (cmd(0x48, 0, "LAMPARRAY off (0x48)") < 0)
            goto dead;
    } else {
        printf("  %-28s skipped, SuppCmdFlag bit clear\n", "LAMPARRAY off (0x48)");
    }
    for (int i = 0; i < 8; i++) {
        if (cmd((unsigned char)(0x20 + i), 0, "RESET zone") < 0)
            goto dead;
    }
    if (cmd(0x28, 0xFF, "APPLY (0x28 0xFF)") < 0)
        goto dead;

    printf("\nstage 3: two staging passes\n");
    for (int pass = 0; pass < 2; pass++) {
        printf("  pass %d\n", pass + 1);
        for (int i = 0; i < 8; i++) {
            if (zone(i) < 0)
                goto dead;
        }
        if (cmd(0x28, 0xFF, "APPLY (0x28 0xFF)") < 0)
            goto dead;
    }

    printf("\nsurvived the whole sequence at %d ms.\n", delay_ms);
    close(fd);
    return 0;

dead:
    printf("\ndied here. Everything after this point will fail the same way\n"
           "until the board loses power - shut down and cut mains for ~10 s.\n");
    close(fd);
    return 1;
}
