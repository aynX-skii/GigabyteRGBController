// ab-global-modes - isolates which of the two global-mode commands added in
// 9d01535 kills the RGB Fusion path.
//
// OUTCOME: neither. On a mains-cut cold boot all three paints show, so 0x31
// and 0x48 are both innocent and 9d01535 was not a regression. What was
// actually swallowing every write after a Windows boot is 0x47 keep-setting,
// which initialize() now switches off on connect.
//
// Kept because the shape of the test is the reusable part: paint through the
// Fusion path, send one suspect command, paint again, and the first paint that
// does not show names the command that did it. Point it at a new suspect by
// editing the stages.
//
//   cc -O1 -o ab-global-modes tools/ab-global-modes.c
//   ./ab-global-modes
//
// Run it as the FIRST thing after a boot, with the GUI closed and the restore
// unit masked - anything that calls Config::apply() first has already sent the
// commands under test, and the result will say nothing.
#include <errno.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define LEN 64

static int fd;

static void pace(void){ struct timespec t = {0, 20000000L}; nanosleep(&t, NULL); }
static void hold(int s){ struct timespec t = {s, 0};        nanosleep(&t, NULL); }

static int cc(unsigned char c, unsigned char a0, const char *what)
{
    unsigned char b[LEN];
    memset(b, 0, LEN);
    b[0] = 0xCC; b[1] = c; b[2] = a0;
    pace();
    int r = ioctl(fd, HIDIOCSFEATURE(LEN), b);
    printf("  %-26s %s\n", what, r < 0 ? strerror(errno) : "ok");
    return r;
}

static void paint(unsigned char r, unsigned char g, unsigned char bl, const char *name)
{
    unsigned char b[LEN];
    for (int i = 0; i < 8; i++) {
        memset(b, 0, LEN);
        b[0] = 0xCC; b[1] = (unsigned char)(0x20 + i); b[2] = (unsigned char)(1u << i);
        b[11] = 0x01;   // static
        b[12] = 60;     // brightness, well clear of the mode ceiling
        b[14] = bl; b[15] = g; b[16] = r;
        pace();
        if (ioctl(fd, HIDIOCSFEATURE(LEN), b) < 0) {
            printf("  zone%d FAIL %s\n", i + 1, strerror(errno));
            return;
        }
    }
    cc(0x28, 0xFF, "APPLY (0x28 0xFF)");
    printf("  >>> 应该是%s，看 8 秒\n\n", name);
    hold(8);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/dev/hidraw5";
    fd = open(path, O_RDWR);
    if (fd < 0) { perror(path); return 1; }
    printf("%s\n\n", path);

    cc(0x60, 0, "INIT (0x60)");
    unsigned char info[LEN] = {0xCC};
    pace();
    if (ioctl(fd, HIDIOCGFEATURE(LEN), info) > 0)
        printf("  SuppCmdFlag 0x%02X\n\n", info[11]);

    printf("阶段 1 基线：还没发过任何全局模式命令\n");
    paint(0x00, 0xFF, 0x00, "绿色");

    printf("阶段 2：只发 0x31（音频律动关）\n");
    cc(0x31, 0, "BEAT off (0x31)");
    paint(0xFF, 0xFF, 0x00, "黄色");

    printf("阶段 3：只发 0x48（LampArray/MSDL 关）\n");
    cc(0x48, 0, "LAMPARRAY off (0x48)");
    paint(0xFF, 0x00, 0xFF, "紫色");

    printf("读法：\n"
           "  绿→黄→紫 全都出现   两条命令都无辜，锁死是 Windows 造成的\n"
           "  绿、黄出现，紫没有   0x48 是元凶，回归坐实\n"
           "  只有绿出现           0x31 是元凶\n"
           "  连绿都没有           冷启动本身就是坏的，跟这两条命令无关\n");
    close(fd);
    return 0;
}
