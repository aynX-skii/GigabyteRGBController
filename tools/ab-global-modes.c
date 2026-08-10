// ab-global-modes - isolates which of the two global-mode commands added in
// 9d01535 kills the RGB Fusion path.
//
// Background. Restoring stopped having any visible effect: the controller
// acknowledges every Fusion report and drives nothing, and only the standard
// LampArray interface still reaches the LEDs. The state survives reboots and
// normal shutdowns because the chip runs off +5VSB. It was assumed Windows put
// the board there, but 9d01535 is also when this program started sending two
// global-mode commands it never sent before - 0x31 (audio beat) and 0x48
// (LampArray/MSDL) - and the report is that restoring worked before that.
//
// So: prove it. Paint through the Fusion path, send one command, paint again.
// The first paint that does not show is the command that did it.
//
//   cc -O1 -o ab-global-modes tools/ab-global-modes.c
//   ./ab-global-modes
//
// Run this as the FIRST thing after a mains-cut cold boot, with the GUI closed
// and the restore unit masked - anything that calls Config::apply() first will
// have sent both commands already and the test tells you nothing.
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
