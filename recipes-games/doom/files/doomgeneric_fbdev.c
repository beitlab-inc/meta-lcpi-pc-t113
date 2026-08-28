// SPDX-License-Identifier: GPL-2.0
//
// doomgeneric framebuffer backend for the LCPI-PC-T113 LCD.
//
// Draws DG_ScreenBuffer to /dev/fb0, puts /dev/tty1 in KD_GRAPHICS, and
// reads controls from `game ctl` (/run/lcpi-game.input) plus a USB keyboard
// (evdev) if one is present. The serial console on PB6/PB7 (/dev/ttyS0) is
// never opened, so `game stop` still works from the login that started it.
// FBIOPAN_DISPLAY after every frame so sun4i-drm's deferred IO
// actually flushes to the panel (a bare mmap memcpy does not).

#include "doomkeys.h"
#include "doomgeneric.h"
#include "i_system.h"

/* doomkeys.h and linux/input.h collide on KEY_ENTER / KEY_TAB / ... */
enum {
    DOOM_KEY_ENTER = KEY_ENTER,
    DOOM_KEY_TAB = KEY_TAB,
    DOOM_KEY_BACKSPACE = KEY_BACKSPACE,
    DOOM_KEY_ESCAPE = KEY_ESCAPE,
    DOOM_KEY_UP = KEY_UPARROW,
    DOOM_KEY_DOWN = KEY_DOWNARROW,
    DOOM_KEY_LEFT = KEY_LEFTARROW,
    DOOM_KEY_RIGHT = KEY_RIGHTARROW,
    DOOM_KEY_USE = KEY_USE,
    DOOM_KEY_FIRE = KEY_FIRE,
    DOOM_KEY_RSHIFT = KEY_RSHIFT,
    DOOM_KEY_LALT = KEY_LALT
};

#undef KEY_TAB
#undef KEY_ENTER
#undef KEY_BACKSPACE
#undef KEY_MINUS
#undef KEY_F1
#undef KEY_F2
#undef KEY_F3
#undef KEY_F4
#undef KEY_F5
#undef KEY_F6
#undef KEY_F7
#undef KEY_F8
#undef KEY_F9
#undef KEY_F10
#undef KEY_F11

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <poll.h>
#include <dirent.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, uint32_t)
#endif

#define KEYQUEUE_SIZE 32
#define MAX_INPUT_DEVS 16
#define UART_HOLD_MS 120
#define GAME_INPUT "/run/lcpi-game.input"

static struct timeval startTime;

static int fb_fd = -1, tty_fd = -1, uart_fd = -1;
static unsigned char *fbp = MAP_FAILED;
static long mapsize;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static int bytespp = 4, restore_tty_mode = 0, uart_is_tty = 0;
static struct termios uart_saved;

static int numInputFds;
static int inputFds[MAX_INPUT_DEVS];
static struct pollfd pollfds[MAX_INPUT_DEVS];

static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWriteIndex;
static unsigned int s_KeyQueueReadIndex;

static unsigned char uart_held_key;
static uint32_t uart_held_until;
static int uart_esc; /* 1 = saw ESC, waiting to see if it is CSI */
static int ctrl_down;

static void cleanup(void);

static void addKeyToQueue(int pressed, unsigned char key)
{
    unsigned short keyData = (unsigned short)((pressed << 8) | key);
    s_KeyQueue[s_KeyQueueWriteIndex] = keyData;
    s_KeyQueueWriteIndex = (s_KeyQueueWriteIndex + 1) % KEYQUEUE_SIZE;
}

static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000u);
}

static uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t rr = r >> (8 - vinfo.red.length);
    uint32_t gg = g >> (8 - vinfo.green.length);
    uint32_t bb = b >> (8 - vinfo.blue.length);
    return (rr << vinfo.red.offset) | (gg << vinfo.green.offset) |
           (bb << vinfo.blue.offset);
}

/* DG_ScreenBuffer is 32-bit XRGB (R@16 G@8 B@0) at DOOMGENERIC_RESX x RESY. */
static void blit_frame(void)
{
    const int dw = DOOMGENERIC_RESX;
    const int dh = DOOMGENERIC_RESY;
    const int fw = (int)vinfo.xres;
    const int fh = (int)vinfo.yres;
    int scale = fw / dw;
    if (fh / dh < scale)
        scale = fh / dh;
    if (scale < 1)
        scale = 1;
    const int out_w = dw * scale;
    const int out_h = dh * scale;
    const int x0 = (fw - out_w) / 2;
    const int y0 = (fh - out_h) / 2;
    const pixel_t *src = (const pixel_t *)DG_ScreenBuffer;
    unsigned char *dst = fbp;
    int y, x, sy, sx;

    for (y = 0; y < dh; y++) {
        for (sy = 0; sy < scale; sy++) {
            unsigned char *row =
                dst + (long)(y0 + y * scale + sy) * finfo.line_length +
                (long)x0 * bytespp;
            const pixel_t *s = src + y * dw;
            for (x = 0; x < dw; x++) {
                pixel_t p = s[x];
                uint32_t c = pack_rgb((p >> 16) & 0xff, (p >> 8) & 0xff,
                                      p & 0xff);
                for (sx = 0; sx < scale; sx++) {
                    if (bytespp == 4)
                        ((uint32_t *)row)[x * scale + sx] = c;
                    else if (bytespp == 2)
                        ((uint16_t *)row)[x * scale + sx] = (uint16_t)c;
                    else {
                        unsigned char *px = row + (x * scale + sx) * 3;
                        px[0] = c & 0xff;
                        px[1] = (c >> 8) & 0xff;
                        px[2] = (c >> 16) & 0xff;
                    }
                }
            }
        }
    }

    vinfo.yoffset = 0;
    ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo);
}

static unsigned char ascii_to_doom(unsigned char c)
{
    switch (c) {
    case 'w':
    case 'W':
        return DOOM_KEY_UP;
    case 's':
    case 'S':
        return DOOM_KEY_DOWN;
    case 'a':
    case 'A':
        return DOOM_KEY_LEFT;
    case 'd':
    case 'D':
        return DOOM_KEY_RIGHT;
    case ' ':
        return DOOM_KEY_USE;
    case 'j':
    case 'J':
    case 'f':
    case 'F':
    case 'k':
    case 'K':
        return DOOM_KEY_FIRE;
    case '\r':
    case '\n':
        return DOOM_KEY_ENTER;
    case 27:
        return DOOM_KEY_ESCAPE;
    case '\t':
        return DOOM_KEY_TAB;
    case 8:
    case 127:
        return DOOM_KEY_BACKSPACE;
    default:
        if (c >= '1' && c <= '9')
            return c;
        if (c >= 'b' && c <= 'z')
            return c; /* run = shift mapped separately; letters for menus */
        return 0;
    }
}

static void uart_press(unsigned char doomkey)
{
    if (!doomkey)
        return;
    if (uart_held_key && uart_held_key != doomkey)
        addKeyToQueue(0, uart_held_key);
    if (uart_held_key != doomkey)
        addKeyToQueue(1, doomkey);
    uart_held_key = doomkey;
    uart_held_until = now_ms() + UART_HOLD_MS;
}

static void uart_release_held(void)
{
    if (uart_held_key) {
        addKeyToQueue(0, uart_held_key);
        uart_held_key = 0;
    }
}

static void poll_uart(void)
{
    unsigned char buf[32];
    ssize_t n;
    int i;

    if (uart_fd < 0)
        return;

    if (uart_held_key && (int32_t)(now_ms() - uart_held_until) >= 0)
        uart_release_held();

    n = read(uart_fd, buf, sizeof buf);
    if (n <= 0)
        return;

    for (i = 0; i < n; i++) {
        unsigned char c = buf[i];
        if (uart_esc == 1) {
            uart_esc = (c == '[') ? 2 : 0;
            if (!uart_esc)
                uart_press(DOOM_KEY_ESCAPE);
            continue;
        }
        if (uart_esc == 2) {
            uart_esc = 0;
            switch (c) {
            case 'A':
                uart_press(DOOM_KEY_UP);
                break;
            case 'B':
                uart_press(DOOM_KEY_DOWN);
                break;
            case 'C':
                uart_press(DOOM_KEY_RIGHT);
                break;
            case 'D':
                uart_press(DOOM_KEY_LEFT);
                break;
            default:
                break;
            }
            continue;
        }
        if (c == 27) {
            uart_esc = 1;
            continue;
        }
        uart_press(ascii_to_doom(c));
    }
}

static unsigned char evdev_to_doom(unsigned int key)
{
    switch (key) {
    case KEY_ENTER:
        return DOOM_KEY_ENTER;
    case KEY_ESC:
        return DOOM_KEY_ESCAPE;
    case KEY_LEFT:
        return DOOM_KEY_LEFT;
    case KEY_RIGHT:
        return DOOM_KEY_RIGHT;
    case KEY_UP:
        return DOOM_KEY_UP;
    case KEY_DOWN:
        return DOOM_KEY_DOWN;
    case KEY_LEFTCTRL:
    case KEY_RIGHTCTRL:
        return DOOM_KEY_FIRE;
    case KEY_SPACE:
        return DOOM_KEY_USE;
    case KEY_LEFTSHIFT:
    case KEY_RIGHTSHIFT:
        return DOOM_KEY_RSHIFT;
    case KEY_LEFTALT:
    case KEY_RIGHTALT:
        return DOOM_KEY_LALT;
    case KEY_TAB:
        return DOOM_KEY_TAB;
    case KEY_BACKSPACE:
        return DOOM_KEY_BACKSPACE;
    case KEY_W:
        return DOOM_KEY_UP;
    case KEY_S:
        return DOOM_KEY_DOWN;
    case KEY_A:
        return DOOM_KEY_LEFT;
    case KEY_D:
        return DOOM_KEY_RIGHT;
    case KEY_1:
        return '1';
    case KEY_2:
        return '2';
    case KEY_3:
        return '3';
    case KEY_4:
        return '4';
    case KEY_5:
        return '5';
    case KEY_6:
        return '6';
    case KEY_7:
        return '7';
    case KEY_8:
        return '8';
    case KEY_9:
        return '9';
    default:
        return 0;
    }
}

static void poll_evdev(void)
{
    int i;
    if (numInputFds <= 0)
        return;
    if (poll(pollfds, numInputFds, 0) <= 0)
        return;
    for (i = 0; i < numInputFds; i++) {
        struct input_event ev;
        if (!(pollfds[i].revents & POLLIN))
            continue;
        while (read(inputFds[i], &ev, sizeof ev) == (ssize_t)sizeof ev) {
            unsigned char dk;
            if (ev.type != EV_KEY)
                continue;
            if (ev.code == KEY_LEFTCTRL || ev.code == KEY_RIGHTCTRL) {
                ctrl_down = (ev.value != 0);
                dk = DOOM_KEY_FIRE;
                if (ev.value == 0 || ev.value == 1)
                    addKeyToQueue(ev.value, dk);
                continue;
            }
            /* Ctrl-C or F10 leave the process so ExecStopPost can restore the VT */
            if (ev.value == 1 &&
                (ev.code == KEY_F10 || (ev.code == KEY_C && ctrl_down))) {
                cleanup();
                _exit(0);
            }
            dk = evdev_to_doom(ev.code);
            if (!dk)
                continue;
            if (ev.value == 0 || ev.value == 1)
                addKeyToQueue(ev.value, dk);
        }
    }
}

#define TEST_KEY(k) (keybits[(k) / 8] & (1 << ((k) % 8)))
static int is_keyboard(const char *devPath)
{
    unsigned long evbits = 0;
    unsigned char keybits[KEY_MAX / 8 + 1];
    int fd = open(devPath, O_RDONLY | O_CLOEXEC);
    int ok = 0;
    if (fd < 0)
        return 0;
    if (ioctl(fd, EVIOCGBIT(0, sizeof evbits), &evbits) < 0)
        goto out;
    if (!(evbits & (1ul << EV_KEY)))
        goto out;
    memset(keybits, 0, sizeof keybits);
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keybits), keybits) < 0)
        goto out;
    if (TEST_KEY(KEY_A) && TEST_KEY(KEY_ENTER))
        ok = 1;
out:
    close(fd);
    return ok;
}

static void scan_keyboards(void)
{
    DIR *dir = opendir("/dev/input");
    struct dirent *dp;
    if (!dir)
        return;
    while ((dp = readdir(dir)) != NULL && numInputFds < MAX_INPUT_DEVS) {
        char path[280];
        int fd;
        if (strncmp(dp->d_name, "event", 5) != 0)
            continue;
        snprintf(path, sizeof path, "/dev/input/%s", dp->d_name);
        if (!is_keyboard(path))
            continue;
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        pollfds[numInputFds].fd = fd;
        pollfds[numInputFds].events = POLLIN;
        inputFds[numInputFds++] = fd;
        ioctl(fd, EVIOCGRAB, 1);
        fprintf(stderr, "doom: keyboard %s\n", path);
    }
    closedir(dir);
}

static void cleanup(void)
{
    int i;
    uart_release_held();
    if (fbp != MAP_FAILED && fbp) {
        munmap(fbp, mapsize);
        fbp = MAP_FAILED;
    }
    if (tty_fd >= 0) {
        if (restore_tty_mode)
            ioctl(tty_fd, KDSETMODE, KD_TEXT);
        close(tty_fd);
        tty_fd = -1;
    }
    if (uart_fd >= 0) {
        if (uart_is_tty)
            tcsetattr(uart_fd, TCSANOW, &uart_saved);
        close(uart_fd);
        uart_fd = -1;
    }
    if (fb_fd >= 0) {
        close(fb_fd);
        fb_fd = -1;
    }
    for (i = 0; i < numInputFds; i++) {
        ioctl(inputFds[i], EVIOCGRAB, 0);
        close(inputFds[i]);
    }
    numInputFds = 0;
}

static void on_signal(int s)
{
    (void)s;
    cleanup();
    _exit(0);
}

void DG_Init(void)
{
    const char *fb_path = "/dev/fb0";
    const char *tty_path = "/dev/tty1";

    fb_fd = open(fb_path, O_RDWR);
    if (fb_fd < 0)
        I_Error("open %s: %s", fb_path, strerror(errno));
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0 ||
        ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0)
        I_Error("FBIOGET_*SCREENINFO: %s", strerror(errno));
    bytespp = vinfo.bits_per_pixel / 8;
    if (bytespp < 2)
        bytespp = 2;
    mapsize = finfo.smem_len;
    fbp = mmap(NULL, mapsize, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fbp == MAP_FAILED)
        I_Error("mmap fb: %s", strerror(errno));
    memset(fbp, 0, mapsize);

    fprintf(stderr,
            "doom: fb %ux%u bpp=%u line_length=%u (doom %dx%d)\n",
            vinfo.xres, vinfo.yres, vinfo.bits_per_pixel, finfo.line_length,
            DOOMGENERIC_RESX, DOOMGENERIC_RESY);

    tty_fd = open(tty_path, O_RDWR | O_NOCTTY);
    if (tty_fd >= 0) {
        if (ioctl(tty_fd, KDSETMODE, KD_GRAPHICS) == 0)
            restore_tty_mode = 1;
    }

    /* Keys from `game ctl` (SSH/serial) via FIFO. Not a TTY: do not
     * cfmakeraw(), and never open /dev/ttyS0. */
    uart_fd = open(GAME_INPUT, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    uart_is_tty = 0;

    scan_keyboards();
    gettimeofday(&startTime, NULL);
    atexit(cleanup);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
}

void DG_DrawFrame(void)
{
    blit_frame();
    poll_uart();
    poll_evdev();
}

void DG_SleepMs(uint32_t ms)
{
    usleep(ms * 1000);
}

uint32_t DG_GetTicksMs(void)
{
    struct timeval curTime;
    gettimeofday(&curTime, NULL);
    return (uint32_t)((curTime.tv_sec - startTime.tv_sec) * 1000 +
                      (curTime.tv_usec - startTime.tv_usec) / 1000);
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
    poll_uart();
    poll_evdev();
    if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex)
        return 0;
    {
        unsigned short keyData = s_KeyQueue[s_KeyQueueReadIndex];
        s_KeyQueueReadIndex = (s_KeyQueueReadIndex + 1) % KEYQUEUE_SIZE;
        *pressed = keyData >> 8;
        *doomKey = (unsigned char)(keyData & 0xff);
        return 1;
    }
}

void DG_SetWindowTitle(const char *title)
{
    fprintf(stderr, "doom: %s\n", title ? title : "");
}

int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);
    for (;;)
        doomgeneric_Tick();
    return 0;
}
