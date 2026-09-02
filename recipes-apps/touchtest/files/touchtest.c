// SPDX-License-Identifier: MIT
//
// touchtest - GT911 capacitive-touch bring-up for the LCPI-PC-T113 LCD.
//
// Draws a grid and four corner targets on /dev/fb0, reads the Goodix evdev
// node (ABS_MT_* plus single-touch ABS_X/ABS_Y), and paints a coloured dot
// plus a fading trail for every contact. That is enough to see whether the
// P4 CTP flex is live, whether X/Y are swapped or inverted, and whether
// multi-touch slots actually move independently.
//
// The sun4i-drm fbdev path needs FBIOPAN_DISPLAY after each frame or the
// panel never updates (same deferred-I/O quirk pingpong works around).
//
// Usage: touchtest [-f fb] [-t tty] [-d /dev/input/eventN] [-x] [-y] [-s]
//   -x  invert X in userspace      -y  invert Y      -s  swap X/Y
//   q / game stop to quit.  c to clear the trail and corner hits.

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, uint32_t)
#endif

#define GAME_INPUT "/run/lcpi-game.input"
#define MAX_SLOTS 10
#define TRAIL_LEN 220
#define CORNER_R 36

static int fb_fd = -1, tty_fd = -1, touch_fd = -1, ctl_fd = -1;
static int kbd_fds[8], n_kbd;
static unsigned char *fbp = MAP_FAILED;
static unsigned char *back = NULL;
static unsigned char *bg_template = NULL;
static unsigned char *canvas = NULL;
static long mapsize, framesize;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static int restore_tty_mode, bytespp = 4, num_pages = 1, cur_page, have_vsync;
static volatile sig_atomic_t running = 1;

static int invert_x, invert_y, swap_xy;
static char touch_name[64] = "searching...";
static char touch_path[64] = "";

struct slot {
    int active;
    int tracking_id;
    int x, y, pressure;
    int have_x, have_y;
};

struct trail {
    int x, y, age, slot;
};

static struct slot slots[MAX_SLOTS];
static int cur_slot;
static int abs_xmin, abs_xmax = 799, abs_ymin, abs_ymax = 479;
static int st_x = -1, st_y = -1, st_press;
static struct trail trail[TRAIL_LEN];
static int trail_n;
static int corner_hit[4];
static int last_sx = -1, last_sy = -1, last_slots;

static void cleanup(void)
{
    if (fbp != MAP_FAILED && fbp) {
        munmap(fbp, mapsize);
        fbp = MAP_FAILED;
    }
    free(back);
    back = NULL;
    free(bg_template);
    bg_template = NULL;
    if (tty_fd >= 0) {
        if (restore_tty_mode)
            ioctl(tty_fd, KDSETMODE, KD_TEXT);
        close(tty_fd);
        tty_fd = -1;
    }
    if (touch_fd >= 0) {
        close(touch_fd);
        touch_fd = -1;
    }
    if (ctl_fd >= 0) {
        close(ctl_fd);
        ctl_fd = -1;
    }
    for (int i = 0; i < n_kbd; i++) {
        ioctl(kbd_fds[i], EVIOCGRAB, 0);
        close(kbd_fds[i]);
    }
    n_kbd = 0;
    if (fb_fd >= 0) {
        close(fb_fd);
        fb_fd = -1;
    }
}

static void on_signal(int s)
{
    (void)s;
    running = 0;
}

static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t rr = r >> (8 - vinfo.red.length);
    uint32_t gg = g >> (8 - vinfo.green.length);
    uint32_t bb = b >> (8 - vinfo.blue.length);
    return (rr << vinfo.red.offset) | (gg << vinfo.green.offset) |
           (bb << vinfo.blue.offset);
}

static void put_px(int x, int y, uint32_t c)
{
    if ((unsigned)x >= (unsigned)vinfo.xres ||
        (unsigned)y >= (unsigned)vinfo.yres)
        return;
    unsigned char *p = canvas + (long)y * finfo.line_length + (long)x * bytespp;
    if (bytespp == 2)
        *(uint16_t *)p = (uint16_t)c;
    else if (bytespp == 3) {
        p[0] = (uint8_t)c;
        p[1] = (uint8_t)(c >> 8);
        p[2] = (uint8_t)(c >> 16);
    } else {
        *(uint32_t *)p = c;
    }
}

static void fill_rect(int x, int y, int w, int h, uint32_t c)
{
    if (w <= 0 || h <= 0)
        return;
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++)
            put_px(xx, yy, c);
}

static void fill_solid(unsigned char *dst, int rows, uint32_t c)
{
    unsigned char *saved = canvas;
    canvas = dst;
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < (int)vinfo.xres; x++)
            put_px(x, y, c);
    canvas = saved;
}

static void fill_circle(int cx, int cy, int r, uint32_t c)
{
    int r2 = r * r;
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r2)
                put_px(cx + x, cy + y, c);
}

static void ring(int cx, int cy, int r, uint32_t c)
{
    int r2 = r * r, rin = (r - 2) * (r - 2);
    if (rin < 0)
        rin = 0;
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++) {
            int d = x * x + y * y;
            if (d <= r2 && d >= rin)
                put_px(cx + x, cy + y, c);
        }
}

/* 5x7, bit 4 = leftmost pixel. Covers ' ' .. 'Z' plus a few extras. */
static const uint8_t FONT[][7] = {
    {0,0,0,0,0,0,0}, /* space */
    {4,4,4,4,0,4,0}, /* ! */
    {0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0},
    {0,4,4,31,4,4,0}, /* + */
    {0,0,0,0,0,4,8},  /* , */
    {0,0,0,14,0,0,0}, /* - */
    {0,0,0,0,0,4,0},  /* . */
    {1,2,4,4,8,16,0}, /* / */
    {14,17,19,21,25,17,14}, /* 0 */
    {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31},
    {14,17,1,6,1,17,14},
    {2,6,10,18,31,2,2},
    {31,16,30,1,1,17,14},
    {6,8,16,30,17,17,14},
    {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14},
    {14,17,17,15,1,2,12}, /* 9 */
    {0,4,0,0,0,4,0}, /* : */
    {0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0},
    {0,0,14,0,14,0,0}, /* = */
    {0,0,0,0,0,0,0},
    {14,17,1,2,4,0,4}, /* ? */
    {0,0,0,0,0,0,0},
    {14,17,17,31,17,17,17}, /* A */
    {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31},
    {31,16,16,30,16,16,16},
    {14,17,16,19,17,17,14},
    {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14},
    {1,1,1,1,17,17,14},
    {17,18,20,24,20,18,17},
    {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},
    {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13},
    {30,17,17,30,20,18,17},
    {14,17,16,14,1,17,14},
    {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10},
    {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},
    {31,1,2,4,8,16,31}, /* Z */
};

static void draw_char(int x, int y, char ch, int s, uint32_t c)
{
    if (ch >= 'a' && ch <= 'z')
        ch = (char)(ch - 'a' + 'A');
    if (ch < ' ' || ch > 'Z')
        return;
    const uint8_t *g = FONT[ch - ' '];
    for (int row = 0; row < 7; row++)
        for (int col = 0; col < 5; col++)
            if (g[row] & (0x10 >> col))
                fill_rect(x + col * s, y + row * s, s, s, c);
}

static void draw_text(int x, int y, const char *text, int s, uint32_t c)
{
    for (; *text; text++, x += 6 * s)
        draw_char(x, y, *text, s, c);
}

static void begin_frame(void)
{
    if (num_pages >= 2)
        canvas = fbp + (long)cur_page * framesize;
    else
        canvas = back;
    memcpy(canvas, bg_template, framesize);
}

static void present(void)
{
    if (num_pages >= 2) {
        vinfo.yoffset = cur_page * vinfo.yres;
        ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo);
        if (have_vsync) {
            uint32_t z = 0;
            ioctl(fb_fd, FBIO_WAITFORVSYNC, &z);
        }
        cur_page ^= 1;
    } else {
        memcpy(fbp, back, framesize);
        vinfo.yoffset = 0;
        ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo);
        if (have_vsync) {
            uint32_t z = 0;
            ioctl(fb_fd, FBIO_WAITFORVSYNC, &z);
        }
    }
}

static long now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000000000L + ts.tv_nsec;
}

static int is_touch(const char *path)
{
    unsigned long evbits = 0;
    unsigned long absbits[(ABS_MAX / (sizeof(long) * 8)) + 1];
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    int ok = 0;
    if (fd < 0)
        return 0;
    if (ioctl(fd, EVIOCGBIT(0, sizeof evbits), &evbits) < 0)
        goto out;
    if (!(evbits & (1ul << EV_ABS)))
        goto out;
    memset(absbits, 0, sizeof absbits);
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof absbits), absbits) < 0)
        goto out;
    if ((absbits[ABS_MT_POSITION_X / (sizeof(long) * 8)] &
         (1ul << (ABS_MT_POSITION_X % (sizeof(long) * 8)))) ||
        (absbits[ABS_X / (sizeof(long) * 8)] &
         (1ul << (ABS_X % (sizeof(long) * 8)))))
        ok = 1;
out:
    close(fd);
    return ok;
}

#define TEST_KEY(k) (keybits[(k) / 8] & (1 << ((k) % 8)))
static int is_keyboard(const char *path)
{
    unsigned long evbits = 0;
    unsigned char keybits[KEY_MAX / 8 + 1];
    int fd = open(path, O_RDONLY | O_CLOEXEC);
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

static void load_abs_range(int fd)
{
    struct input_absinfo ax, ay;
    int code_x = ABS_MT_POSITION_X, code_y = ABS_MT_POSITION_Y;
    memset(&ax, 0, sizeof ax);
    memset(&ay, 0, sizeof ay);
    if (ioctl(fd, EVIOCGABS(code_x), &ax) < 0 || ax.maximum <= ax.minimum) {
        code_x = ABS_X;
        ioctl(fd, EVIOCGABS(code_x), &ax);
    }
    if (ioctl(fd, EVIOCGABS(code_y), &ay) < 0 || ay.maximum <= ay.minimum) {
        code_y = ABS_Y;
        ioctl(fd, EVIOCGABS(code_y), &ay);
    }
    if (ax.maximum > ax.minimum) {
        abs_xmin = ax.minimum;
        abs_xmax = ax.maximum;
    }
    if (ay.maximum > ay.minimum) {
        abs_ymin = ay.minimum;
        abs_ymax = ay.maximum;
    }
}

static int open_touch(const char *forced)
{
    if (touch_fd >= 0) {
        close(touch_fd);
        touch_fd = -1;
    }
    if (forced && forced[0]) {
        touch_fd = open(forced, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (touch_fd < 0)
            return 0;
        snprintf(touch_path, sizeof touch_path, "%s", forced);
    } else {
        DIR *dir = opendir("/dev/input");
        struct dirent *dp;
        char preferred[280] = "";
        char fallback[280] = "";
        if (!dir)
            return 0;
        while ((dp = readdir(dir))) {
            char path[280], name[64];
            int fd;
            if (strncmp(dp->d_name, "event", 5) != 0)
                continue;
            snprintf(path, sizeof path, "/dev/input/%s", dp->d_name);
            if (!is_touch(path))
                continue;
            fd = open(path, O_RDONLY | O_CLOEXEC);
            if (fd < 0)
                continue;
            memset(name, 0, sizeof name);
            ioctl(fd, EVIOCGNAME(sizeof name - 1), name);
            close(fd);
            if (!fallback[0])
                snprintf(fallback, sizeof fallback, "%s", path);
            if (strcasestr(name, "goodix") || strcasestr(name, "gt911") ||
                strcasestr(name, "touch"))
                snprintf(preferred, sizeof preferred, "%s", path);
        }
        closedir(dir);
        const char *pick = preferred[0] ? preferred : fallback;
        if (!pick[0])
            return 0;
        touch_fd = open(pick, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (touch_fd < 0)
            return 0;
        snprintf(touch_path, sizeof touch_path, "%s", pick);
    }
    memset(touch_name, 0, sizeof touch_name);
    if (ioctl(touch_fd, EVIOCGNAME(sizeof touch_name - 1), touch_name) < 0)
        snprintf(touch_name, sizeof touch_name, "touch");
    load_abs_range(touch_fd);
    fprintf(stderr, "touchtest: %s (%s) abs X %d..%d Y %d..%d\n",
            touch_path, touch_name, abs_xmin, abs_xmax, abs_ymin, abs_ymax);
    return 1;
}

static void scan_keyboards(void)
{
    DIR *dir = opendir("/dev/input");
    struct dirent *dp;
    if (!dir)
        return;
    while ((dp = readdir(dir)) && n_kbd < (int)(sizeof kbd_fds / sizeof kbd_fds[0])) {
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
        ioctl(fd, EVIOCGRAB, 1);
        kbd_fds[n_kbd++] = fd;
    }
    closedir(dir);
}

static void map_xy(int raw_x, int raw_y, int *sx, int *sy)
{
    int x = raw_x, y = raw_y;
    if (swap_xy) {
        int t = x;
        x = y;
        y = t;
    }
    int xspan = abs_xmax - abs_xmin;
    int yspan = abs_ymax - abs_ymin;
    if (xspan < 1)
        xspan = 1;
    if (yspan < 1)
        yspan = 1;
    int px = (int)((long)(x - abs_xmin) * (long)vinfo.xres / xspan);
    int py = (int)((long)(y - abs_ymin) * (long)vinfo.yres / yspan);
    if (invert_x)
        px = (int)vinfo.xres - 1 - px;
    if (invert_y)
        py = (int)vinfo.yres - 1 - py;
    if (px < 0)
        px = 0;
    if (py < 0)
        py = 0;
    if (px >= (int)vinfo.xres)
        px = (int)vinfo.xres - 1;
    if (py >= (int)vinfo.yres)
        py = (int)vinfo.yres - 1;
    *sx = px;
    *sy = py;
}

static void trail_add(int x, int y, int slot)
{
    if (trail_n < TRAIL_LEN) {
        trail[trail_n].x = x;
        trail[trail_n].y = y;
        trail[trail_n].age = 0;
        trail[trail_n].slot = slot;
        trail_n++;
        return;
    }
    memmove(trail, trail + 1, (TRAIL_LEN - 1) * sizeof trail[0]);
    trail[TRAIL_LEN - 1].x = x;
    trail[TRAIL_LEN - 1].y = y;
    trail[TRAIL_LEN - 1].age = 0;
    trail[TRAIL_LEN - 1].slot = slot;
}

static void trail_age(void)
{
    int w = 0;
    for (int i = 0; i < trail_n; i++) {
        trail[i].age++;
        if (trail[i].age < 90)
            trail[w++] = trail[i];
    }
    trail_n = w;
}

static void corners(int *cx, int *cy)
{
    const int m = 48;
    cx[0] = m;
    cy[0] = m + 28;
    cx[1] = (int)vinfo.xres - 1 - m;
    cy[1] = m + 28;
    cx[2] = m;
    cy[2] = (int)vinfo.yres - 1 - m;
    cx[3] = (int)vinfo.xres - 1 - m;
    cy[3] = (int)vinfo.yres - 1 - m;
}

static void hit_corners(int x, int y)
{
    int cx[4], cy[4];
    corners(cx, cy);
    for (int i = 0; i < 4; i++) {
        int dx = x - cx[i], dy = y - cy[i];
        if (dx * dx + dy * dy <= CORNER_R * CORNER_R)
            corner_hit[i] = 1;
    }
}

static void commit_report(void)
{
    int n = 0;
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!slots[i].active || !slots[i].have_x || !slots[i].have_y)
            continue;
        int sx, sy;
        map_xy(slots[i].x, slots[i].y, &sx, &sy);
        trail_add(sx, sy, i);
        hit_corners(sx, sy);
        last_sx = sx;
        last_sy = sy;
        n++;
    }
    if (!n && st_x >= 0 && st_y >= 0) {
        int sx, sy;
        map_xy(st_x, st_y, &sx, &sy);
        trail_add(sx, sy, 0);
        hit_corners(sx, sy);
        last_sx = sx;
        last_sy = sy;
        n = 1;
    }
    last_slots = n;
}

static void handle_event(const struct input_event *ev)
{
    if (ev->type == EV_ABS) {
        switch (ev->code) {
        case ABS_MT_SLOT:
            if (ev->value >= 0 && ev->value < MAX_SLOTS)
                cur_slot = ev->value;
            break;
        case ABS_MT_TRACKING_ID:
            if (ev->value < 0) {
                slots[cur_slot].active = 0;
                slots[cur_slot].have_x = slots[cur_slot].have_y = 0;
            } else {
                slots[cur_slot].active = 1;
                slots[cur_slot].tracking_id = ev->value;
            }
            break;
        case ABS_MT_POSITION_X:
            slots[cur_slot].x = ev->value;
            slots[cur_slot].have_x = 1;
            if (!slots[cur_slot].active)
                slots[cur_slot].active = 1;
            break;
        case ABS_MT_POSITION_Y:
            slots[cur_slot].y = ev->value;
            slots[cur_slot].have_y = 1;
            if (!slots[cur_slot].active)
                slots[cur_slot].active = 1;
            break;
        case ABS_MT_PRESSURE:
            slots[cur_slot].pressure = ev->value;
            break;
        case ABS_X:
            st_x = ev->value;
            break;
        case ABS_Y:
            st_y = ev->value;
            break;
        case ABS_PRESSURE:
            st_press = ev->value;
            if (ev->value == 0)
                st_x = st_y = -1;
            break;
        default:
            break;
        }
    } else if (ev->type == EV_KEY &&
               (ev->code == BTN_TOUCH || ev->code == BTN_LEFT) &&
               ev->value == 0) {
        st_x = st_y = -1;
        for (int i = 0; i < MAX_SLOTS; i++)
            slots[i].active = 0;
    } else if (ev->type == EV_SYN && ev->code == SYN_REPORT) {
        commit_report();
    }
}

static void drain_touch(void)
{
    struct input_event ev[32];
    if (touch_fd < 0)
        return;
    for (;;) {
        ssize_t n = read(touch_fd, ev, sizeof ev);
        if (n <= 0)
            break;
        int count = (int)(n / (ssize_t)sizeof ev[0]);
        for (int i = 0; i < count; i++)
            handle_event(&ev[i]);
    }
}

static void handle_key(unsigned char c)
{
    if (c == 'q' || c == 'Q')
        running = 0;
    if (c == 'c' || c == 'C') {
        trail_n = 0;
        memset(corner_hit, 0, sizeof corner_hit);
        last_sx = last_sy = -1;
        last_slots = 0;
    }
}

static void drain_keys(void)
{
    unsigned char buf[64];
    if (ctl_fd >= 0) {
        ssize_t n = read(ctl_fd, buf, sizeof buf);
        for (ssize_t i = 0; i < n; i++)
            handle_key(buf[i]);
    }
    for (int k = 0; k < n_kbd; k++) {
        struct input_event ev[16];
        ssize_t n = read(kbd_fds[k], ev, sizeof ev);
        int count = n > 0 ? (int)(n / (ssize_t)sizeof ev[0]) : 0;
        for (int i = 0; i < count; i++) {
            if (ev[i].type == EV_KEY && ev[i].value == 1) {
                if (ev[i].code == KEY_Q)
                    handle_key('q');
                if (ev[i].code == KEY_C)
                    handle_key('c');
            }
        }
    }
}

int main(int argc, char **argv)
{
    const char *fb_path = "/dev/fb0";
    const char *tty_path = "/dev/tty1";
    const char *forced = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-f") && i + 1 < argc)
            fb_path = argv[++i];
        else if (!strcmp(argv[i], "-t") && i + 1 < argc)
            tty_path = argv[++i];
        else if (!strcmp(argv[i], "-d") && i + 1 < argc)
            forced = argv[++i];
        else if (!strcmp(argv[i], "-x"))
            invert_x = 1;
        else if (!strcmp(argv[i], "-y"))
            invert_y = 1;
        else if (!strcmp(argv[i], "-s"))
            swap_xy = 1;
        else {
            fprintf(stderr,
                    "usage: %s [-f fb] [-t tty] [-d event] [-x] [-y] [-s]\n",
                    argv[0]);
            return 2;
        }
    }

    atexit(cleanup);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    fb_fd = open(fb_path, O_RDWR);
    if (fb_fd < 0) {
        perror(fb_path);
        return 1;
    }
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0 ||
        ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("FBIOGET_*SCREENINFO");
        return 1;
    }
    bytespp = vinfo.bits_per_pixel / 8;
    if (bytespp < 2)
        bytespp = 2;
    mapsize = finfo.smem_len;
    framesize = (long)finfo.line_length * vinfo.yres;
    fbp = mmap(NULL, mapsize, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fbp == MAP_FAILED) {
        perror("mmap fb");
        return 1;
    }

    num_pages = vinfo.yres_virtual / vinfo.yres;
    if (num_pages >= 2 && mapsize >= 2 * framesize)
        num_pages = 2;
    else
        num_pages = 1;
    if (num_pages == 1) {
        back = malloc(framesize);
        if (!back) {
            perror("malloc");
            return 1;
        }
    }
    {
        uint32_t z = 0;
        have_vsync = (ioctl(fb_fd, FBIO_WAITFORVSYNC, &z) == 0);
    }

    const int W = (int)vinfo.xres, H = (int)vinfo.yres;
    const uint32_t C_BG = rgb(10, 14, 26);
    const uint32_t C_GRID = rgb(28, 38, 58);
    const uint32_t C_INK = rgb(230, 234, 242);
    const uint32_t C_DIM = rgb(130, 140, 160);
    const uint32_t C_OK = rgb(70, 210, 130);
    const uint32_t C_WAIT = rgb(240, 190, 70);
    const uint32_t C_BAD = rgb(240, 90, 90);
    uint32_t slot_col[MAX_SLOTS];
    slot_col[0] = rgb(90, 200, 255);
    slot_col[1] = rgb(255, 140, 90);
    slot_col[2] = rgb(180, 130, 255);
    slot_col[3] = rgb(80, 230, 170);
    slot_col[4] = rgb(255, 90, 160);
    for (int i = 5; i < MAX_SLOTS; i++)
        slot_col[i] = rgb(200, 200, 80);

    bg_template = malloc(framesize);
    if (!bg_template) {
        perror("malloc");
        return 1;
    }
    canvas = bg_template;
    fill_solid(bg_template, vinfo.yres, C_BG);
    for (int x = 0; x < W; x += 80)
        fill_rect(x, 0, 1, H, C_GRID);
    for (int y = 0; y < H; y += 80)
        fill_rect(0, y, W, 1, C_GRID);
    fill_rect(W / 2 - 1, 0, 2, H, rgb(40, 55, 80));
    fill_rect(0, H / 2 - 1, W, 2, rgb(40, 55, 80));
    fill_solid(fbp, mapsize / finfo.line_length, C_BG);

    tty_fd = open(tty_path, O_RDWR | O_NOCTTY);
    if (tty_fd >= 0) {
        if (ioctl(tty_fd, KDSETMODE, KD_GRAPHICS) == 0)
            restore_tty_mode = 1;
    }

    ctl_fd = open(GAME_INPUT, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    scan_keyboards();
    if (!open_touch(forced))
        snprintf(touch_name, sizeof touch_name, "NO DEVICE");

    const long FRAME_NS = 16666667L;
    long next = now_ns();
    int rescan = 0;

    while (running) {
        drain_keys();
        drain_touch();
        trail_age();

        if (touch_fd < 0 && !forced) {
            if (++rescan >= 120) {
                rescan = 0;
                open_touch(NULL);
            }
        }

        begin_frame();

        int cx[4], cy[4];
        corners(cx, cy);
        int hits = 0;
        for (int i = 0; i < 4; i++) {
            uint32_t col = corner_hit[i] ? C_OK : C_WAIT;
            ring(cx[i], cy[i], CORNER_R, col);
            ring(cx[i], cy[i], CORNER_R - 8, col);
            if (corner_hit[i])
                hits++;
        }

        for (int i = 0; i < trail_n; i++) {
            int fade = 90 - trail[i].age;
            uint32_t base = slot_col[trail[i].slot % MAX_SLOTS];
            (void)base;
            int r = 40 + fade * 2;
            int g = 90 + fade;
            int b = 140 + fade;
            fill_circle(trail[i].x, trail[i].y, 3, rgb((uint8_t)r, (uint8_t)g, (uint8_t)b));
        }

        for (int i = 0; i < MAX_SLOTS; i++) {
            if (!slots[i].active || !slots[i].have_x || !slots[i].have_y)
                continue;
            int sx, sy;
            map_xy(slots[i].x, slots[i].y, &sx, &sy);
            fill_circle(sx, sy, 14, slot_col[i]);
            ring(sx, sy, 18, C_INK);
        }
        if (last_slots == 1 && st_x >= 0 && st_y >= 0) {
            int any = 0;
            for (int i = 0; i < MAX_SLOTS; i++)
                if (slots[i].active)
                    any = 1;
            if (!any) {
                int sx, sy;
                map_xy(st_x, st_y, &sx, &sy);
                fill_circle(sx, sy, 14, slot_col[0]);
                ring(sx, sy, 18, C_INK);
            }
        }

        fill_rect(0, 0, W, 26, rgb(6, 8, 16));
        fill_rect(0, H - 26, W, 26, rgb(6, 8, 16));

        char line[160];
        if (touch_fd >= 0)
            snprintf(line, sizeof line, "GT911  %s  %s", touch_path, touch_name);
        else
            snprintf(line, sizeof line, "NO TOUCH DEVICE  i2cdetect -y 2   dmesg | grep -i goodix");
        draw_text(8, 5, line, 2, touch_fd >= 0 ? C_INK : C_BAD);

        if (hits == 4)
            draw_text(W / 2 - 48, H / 2 - 20, "PASS", 5, C_OK);
        else {
            snprintf(line, sizeof line, "TAP 4 CORNERS  %d/4", hits);
            draw_text(W / 2 - 110, 8, line, 1, C_DIM);
        }

        if (last_sx >= 0)
            snprintf(line, sizeof line, "XY %d %d   SLOTS %d   Q QUIT  C CLEAR",
                     last_sx, last_sy, last_slots);
        else
            snprintf(line, sizeof line, "TOUCH THE GLASS   Q QUIT  C CLEAR");
        draw_text(8, H - 20, line, 2, C_DIM);

        present();

        next += FRAME_NS;
        long now = now_ns();
        long delay = next - now;
        if (delay > 0) {
            struct timespec ts = { delay / 1000000000L, delay % 1000000000L };
            nanosleep(&ts, NULL);
        } else {
            next = now;
        }
    }
    return 0;
}
