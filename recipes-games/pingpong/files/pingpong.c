// SPDX-License-Identifier: MIT
//
// pingpong - a tiny UART-controlled Pong for the LCPI-PC-T113 LCD.
//
// It renders to the Linux framebuffer (/dev/fb0), puts the LCD's virtual
// terminal into KD_GRAPHICS mode so the text console stops painting over it, and
// takes its control keys from a serial port (UART). No X11/Wayland/GPU needed -
// perfect for the T113-S3 (128MB RAM, 2D-only).
//
// Rendering notes (why the animation is smooth):
//   * The sun4i-drm fbdev emulation uses DEFERRED I/O + a shadow buffer, so a
//     bare memcpy into the mmap does not reliably reach the panel. After drawing
//     each frame we call FBIOPAN_DISPLAY, which goes through the DRM fbdev pan
//     handler and forces a damage flush -> continuous updates.
//   * If the fb exposes a virtual height >= 2*yres we page-flip between two
//     buffers (no tearing) and, when supported, wait for vblank.
//   * The screen is cleared with a single memcpy of a pre-rendered background
//     (with the net baked in), and frames are paced to ~60 FPS.
//
// Controls (bytes read from the input device, default /dev/stdin):
//   Left paddle : 'w' / 's'      (or Up / Down arrow keys)
//   Right paddle: 'i' / 'k'      (or 'o' / 'l')  -- using these disables the AI
//   Serve ball  : space          (also starts a new match after WIN/LOSE)
//   Quit        : 'q' (or Ctrl-C)
//
// A match is first-to-5: when a side reaches the winning score the ball freezes
// and each half shows WIN / LOSE until space starts the next match.
//
// Sound: short blips are synthesised on the fly and streamed to the ALSA
// "default" device (the same sun20i codec path the boot chime uses) in
// non-blocking mode, so audio never stalls the 60 FPS loop. If ALSA can't be
// opened the game just runs silently.
//
// Usage: pingpong [-i <input-dev>] [-f <fb-dev>] [-t <tty-dev>]
//   defaults: -i /dev/stdin  -f /dev/fb0  -t /dev/tty1

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <alsa/asoundlib.h>

#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, uint32_t)
#endif

#define WIN_SCORE 5                        /* first side to reach this wins the match */

/* ---------------- globals for clean teardown ---------------- */
static int   fb_fd = -1, tty_fd = -1, in_fd = -1;
static unsigned char *fbp = MAP_FAILED;   /* the mmap'd framebuffer          */
static unsigned char *back = NULL;         /* single-buffer scratch (malloc)  */
static unsigned char *bg_template = NULL;  /* pre-rendered background + net    */
static unsigned char *canvas = NULL;       /* where put_px() draws this frame  */
static long  mapsize = 0, framesize = 0;   /* framesize = line_length * yres    */
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static struct termios in_saved;
static int   in_is_tty = 0, restore_tty_mode = 0, bytespp = 4;
static int   num_pages = 1, cur_page = 0, have_vsync = 0;
static volatile sig_atomic_t running = 1;

static void snd_close(void);               /* defined with the sound engine below */

static void cleanup(void)
{
    if (fbp != MAP_FAILED && fbp) { munmap(fbp, mapsize); fbp = MAP_FAILED; }
    if (back) { free(back); back = NULL; }
    if (bg_template) { free(bg_template); bg_template = NULL; }
    if (tty_fd >= 0) {
        if (restore_tty_mode) ioctl(tty_fd, KDSETMODE, KD_TEXT);
        close(tty_fd); tty_fd = -1;
    }
    if (in_fd >= 0) {
        if (in_is_tty) tcsetattr(in_fd, TCSANOW, &in_saved);
        if (in_fd != STDIN_FILENO) close(in_fd);
        in_fd = -1;
    }
    if (fb_fd >= 0) { close(fb_fd); fb_fd = -1; }
    snd_close();
}

static void on_signal(int s) { (void)s; running = 0; }

/* ---------------- drawing primitives (write to `canvas`) ---------------- */
static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t rr = r >> (8 - vinfo.red.length);
    uint32_t gg = g >> (8 - vinfo.green.length);
    uint32_t bb = b >> (8 - vinfo.blue.length);
    return (rr << vinfo.red.offset) | (gg << vinfo.green.offset) |
           (bb << vinfo.blue.offset);
}

static inline void put_px(int x, int y, uint32_t c)
{
    if ((unsigned)x >= vinfo.xres || (unsigned)y >= vinfo.yres) return;
    unsigned char *p = canvas + (long)y * finfo.line_length + (long)x * bytespp;
    if (bytespp == 4)      *(uint32_t *)p = c;
    else if (bytespp == 2) *(uint16_t *)p = (uint16_t)c;
    else { p[0] = c & 0xff; p[1] = (c >> 8) & 0xff; p[2] = (c >> 16) & 0xff; }
}

static void fill_rect(int x, int y, int w, int h, uint32_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)vinfo.xres) w = vinfo.xres - x;
    if (y + h > (int)vinfo.yres) h = vinfo.yres - y;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            put_px(x + i, y + j, c);
}

/* Fill a raw buffer with a solid colour across the FULL hardware stride, not
 * just the visible xres. The sun4i DRM fbdev pads line_length (and yres_virtual)
 * beyond the visible box; those padding pixels are what leak through as stray
 * bars/columns if we leave them uninitialised. `rows` counts full stride rows. */
static void fill_solid(unsigned char *buf, long rows, uint32_t c)
{
    long stride_px = finfo.line_length / bytespp;
    for (long y = 0; y < rows; y++) {
        unsigned char *row = buf + y * finfo.line_length;
        for (long x = 0; x < stride_px; x++) {
            if (bytespp == 4)      ((uint32_t *)row)[x] = c;
            else if (bytespp == 2) ((uint16_t *)row)[x] = (uint16_t)c;
            else { row[x*3] = c & 0xff; row[x*3+1] = (c>>8) & 0xff; row[x*3+2] = (c>>16) & 0xff; }
        }
    }
}

/* 8x8 bitmap font for digits 0-9 (MSB = leftmost pixel) */
static const uint8_t font_digits[10][8] = {
    {0x3C,0x66,0x6E,0x7E,0x76,0x66,0x3C,0x00}, /*0*/
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, /*1*/
    {0x3C,0x66,0x06,0x0C,0x30,0x60,0x7E,0x00}, /*2*/
    {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00}, /*3*/
    {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00}, /*4*/
    {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00}, /*5*/
    {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00}, /*6*/
    {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00}, /*7*/
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00}, /*8*/
    {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00}, /*9*/
};

static void draw_digit(int d, int x, int y, int s, uint32_t c)
{
    if (d < 0 || d > 9) return;
    for (int row = 0; row < 8; row++)
        for (int col = 0; col < 8; col++)
            if (font_digits[d][row] & (0x80 >> col))
                fill_rect(x + col * s, y + row * s, s, s, c);
}

static void draw_number(int n, int x, int y, int s, uint32_t c)
{
    char buf[16];
    snprintf(buf, sizeof buf, "%d", n < 0 ? 0 : n);
    for (char *p = buf; *p; p++) { draw_digit(*p - '0', x, y, s, c); x += 9 * s; }
}

/* 8x8 glyphs for the few letters the WIN / LOSE banners need. */
static const uint8_t glyph_W[8] = {0x82,0x82,0x82,0x92,0xAA,0xAA,0x44,0x00};
static const uint8_t glyph_I[8] = {0x7E,0x18,0x18,0x18,0x18,0x18,0x7E,0x00};
static const uint8_t glyph_N[8] = {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00};
static const uint8_t glyph_L[8] = {0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xFE,0x00};
static const uint8_t glyph_O[8] = {0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00};
static const uint8_t glyph_S[8] = {0x7C,0xC6,0xC0,0x7C,0x06,0xC6,0x7C,0x00};
static const uint8_t glyph_E[8] = {0xFE,0xC0,0xC0,0xF8,0xC0,0xC0,0xFE,0x00};

static const uint8_t *glyph_for(char ch)
{
    switch (ch) {
    case 'W': return glyph_W; case 'I': return glyph_I; case 'N': return glyph_N;
    case 'L': return glyph_L; case 'O': return glyph_O; case 'S': return glyph_S;
    case 'E': return glyph_E; default:  return NULL;   /* space / unknown */
    }
}

static void draw_glyph(const uint8_t g[8], int x, int y, int s, uint32_t c)
{
    for (int row = 0; row < 8; row++)
        for (int col = 0; col < 8; col++)
            if (g[row] & (0x80 >> col))
                fill_rect(x + col * s, y + row * s, s, s, c);
}

/* Draw `text` centred horizontally on `cx`, vertical top at `y`, scale `s`. */
static void draw_text_centered(const char *text, int cx, int y, int s, uint32_t c)
{
    int n = (int)strlen(text);
    int adv = 9 * s;                       /* per-char advance (8px glyph + 1px gap) */
    int x = cx - (n * adv - s) / 2;        /* trailing gap trimmed for tighter centring */
    for (const char *p = text; *p; p++, x += adv) {
        const uint8_t *g = glyph_for(*p);
        if (g) draw_glyph(g, x, y, s, c);
    }
}

/* ---------------- audio (non-blocking ALSA synth) ---------------- */
#define SND_RATE   44100
#define SND_CH     2
#define SND_MAXFR  (SND_RATE / 2)          /* 0.5 s of headroom per effect */

static snd_pcm_t *pcm = NULL;
static int16_t   *sfx = NULL;              /* interleaved S16 stereo scratch */
static long       sfx_len = 0, sfx_pos = 0, sfx_build = 0;

/* Append one decaying sine "note" to the effect buffer being built. */
static void snd_note(double freq, double ms, double gain, double decay_ms)
{
    long n = (long)(ms / 1000.0 * SND_RATE);
    for (long i = 0; i < n && sfx_build < SND_MAXFR; i++, sfx_build++) {
        double t   = (double)i / SND_RATE;
        double atk = t < 0.003 ? t / 0.003 : 1.0;           /* 3 ms click guard */
        double env = exp(-t * 1000.0 / decay_ms) * atk * gain;
        double v   = sin(2.0 * M_PI * freq * t) * env;
        int16_t s  = (int16_t)(v * 32767.0);
        sfx[sfx_build * SND_CH + 0] = s;
        sfx[sfx_build * SND_CH + 1] = s;
    }
}

/* Arm the just-built effect for playback from the top. */
static void snd_arm(void)
{
    sfx_len = sfx_build;
    sfx_pos = 0;
    if (pcm) snd_pcm_prepare(pcm);         /* restart cleanly (drops any tail) */
}

static void sfx_paddle(void) { if (!pcm) return; sfx_build = 0; snd_note(660, 45, 0.55, 55);  snd_arm(); }
static void sfx_wall(void)   { if (!pcm) return; sfx_build = 0; snd_note(300, 55, 0.55, 70);  snd_arm(); }
static void sfx_score(void)  { if (!pcm) return; sfx_build = 0; snd_note(196, 220, 0.55, 180); snd_arm(); }
static void sfx_win(void)    { if (!pcm) return; sfx_build = 0;
                               snd_note(523, 110, 0.5, 260); snd_note(659, 110, 0.5, 260);
                               snd_note(784, 240, 0.5, 380); snd_arm(); }
static void sfx_lose(void)   { if (!pcm) return; sfx_build = 0;
                               snd_note(392, 140, 0.5, 260); snd_note(311, 140, 0.5, 260);
                               snd_note(233, 260, 0.5, 380); snd_arm(); }

/* Push whatever the ALSA ring can accept this frame; never blocks. */
static void snd_service(void)
{
    if (!pcm || sfx_pos >= sfx_len) return;
    snd_pcm_sframes_t avail = snd_pcm_avail_update(pcm);
    if (avail < 0) { snd_pcm_recover(pcm, (int)avail, 1); snd_pcm_prepare(pcm); return; }
    long chunk = sfx_len - sfx_pos;
    if (chunk > avail) chunk = avail;
    if (chunk > 0) {
        snd_pcm_sframes_t wrote = snd_pcm_writei(pcm, sfx + sfx_pos * SND_CH, chunk);
        if (wrote < 0) { snd_pcm_recover(pcm, (int)wrote, 1); snd_pcm_prepare(pcm); return; }
        sfx_pos += wrote;
    }
    /* Kick playback as soon as a little is buffered (low-latency blips). */
    if (snd_pcm_state(pcm) == SND_PCM_STATE_PREPARED) snd_pcm_start(pcm);
}

/* Open (or re-open) the ALSA device. Returns 1 on success. Kept separate from
 * snd_init so the game can RETRY: this board's codec PCM is single-client, and
 * the boot chime (a one-shot) can still hold "default" when the kiosk starts, so
 * a single open at startup would fail with -EBUSY and leave the game silent
 * forever. snd_retry() re-attempts this until the device is free. */
static int snd_open_dev(void)
{
    const char *dev = getenv("PINGPONG_PCM");
    if (!dev) dev = "default";
    if (snd_pcm_open(&pcm, dev, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK) < 0) {
        pcm = NULL; return 0;              /* busy/unavailable: retried later */
    }
    if (snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                           SND_CH, SND_RATE, 1 /*resample*/, 100000 /*100 ms*/) < 0) {
        snd_pcm_close(pcm); pcm = NULL; return 0;
    }
    return 1;
}

static void snd_init(void)
{
    sfx = malloc((size_t)SND_MAXFR * SND_CH * sizeof(int16_t));
    if (!sfx) return;
    snd_open_dev();                        /* ok if this fails now; retried lazily */
}

/* If the device could not be opened yet (e.g. busy while the boot chime plays),
 * retry about once a second so sound comes to life as soon as it is free. */
static void snd_retry(void)
{
    static int cooldown = 0;
    if (pcm || !sfx) return;
    if (cooldown > 0) { cooldown--; return; }
    cooldown = 60;                         /* ~1 s at 60 FPS between attempts */
    snd_open_dev();
}

static void snd_close(void)
{
    if (pcm) { snd_pcm_close(pcm); pcm = NULL; }
    if (sfx) { free(sfx); sfx = NULL; }
}

/* Point the draw canvas at this frame's target buffer. */
static void begin_frame(void)
{
    if (num_pages >= 2)
        canvas = fbp + (long)cur_page * framesize;   /* draw off-screen page */
    else
        canvas = back;                               /* draw scratch buffer  */
    memcpy(canvas, bg_template, framesize);          /* fast clear (bg + net) */
}

/* Push the finished frame to the panel. */
static void present(void)
{
    if (num_pages >= 2) {
        vinfo.yoffset = cur_page * vinfo.yres;
        ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo);       /* flip + flush (DRM pan) */
        if (have_vsync) { uint32_t z = 0; ioctl(fb_fd, FBIO_WAITFORVSYNC, &z); }
        cur_page ^= 1;
    } else {
        memcpy(fbp, back, framesize);
        vinfo.yoffset = 0;
        ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo);       /* force deferred-io flush */
        if (have_vsync) { uint32_t z = 0; ioctl(fb_fd, FBIO_WAITFORVSYNC, &z); }
    }
}

static long now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000000000L + ts.tv_nsec;
}

/* ---------------- main ---------------- */
int main(int argc, char **argv)
{
    const char *in_path = "/dev/stdin";
    const char *fb_path = "/dev/fb0";
    const char *tty_path = "/dev/tty1";

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-i") && i + 1 < argc) in_path = argv[++i];
        else if (!strcmp(argv[i], "-f") && i + 1 < argc) fb_path = argv[++i];
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) tty_path = argv[++i];
        else { fprintf(stderr, "usage: %s [-i in] [-f fb] [-t tty]\n", argv[0]); return 2; }
    }

    atexit(cleanup);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    /* --- framebuffer --- */
    fb_fd = open(fb_path, O_RDWR);
    if (fb_fd < 0) { perror(fb_path); return 1; }
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0 ||
        ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("FBIOGET_*SCREENINFO"); return 1;
    }
    bytespp = vinfo.bits_per_pixel / 8;
    if (bytespp < 2) bytespp = 2;
    mapsize = finfo.smem_len;
    framesize = (long)finfo.line_length * vinfo.yres;
    fbp = mmap(NULL, mapsize, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fbp == MAP_FAILED) { perror("mmap fb"); return 1; }

    /* Log the real geometry (goes to the serial console / journal) so panel
     * scanout quirks are easy to diagnose. */
    fprintf(stderr,
            "pingpong: fb %ux%u virt %ux%u bpp=%u line_length=%u smem_len=%u "
            "rgb=%u/%u,%u/%u,%u/%u\n",
            vinfo.xres, vinfo.yres, vinfo.xres_virtual, vinfo.yres_virtual,
            vinfo.bits_per_pixel, finfo.line_length, finfo.smem_len,
            vinfo.red.length, vinfo.red.offset,
            vinfo.green.length, vinfo.green.offset,
            vinfo.blue.length, vinfo.blue.offset);

    /* Double-buffer only if the virtual framebuffer really has room for 2 pages. */
    num_pages = vinfo.yres_virtual / vinfo.yres;
    if (num_pages >= 2 && mapsize >= 2 * framesize) num_pages = 2;
    else num_pages = 1;
    if (num_pages == 1) {
        back = malloc(framesize);
        if (!back) { perror("malloc"); return 1; }
    }

    /* Probe vsync support once (DRM fbdev often lacks it -> we just skip it). */
    { uint32_t z = 0; have_vsync = (ioctl(fb_fd, FBIO_WAITFORVSYNC, &z) == 0); }

    const int W = vinfo.xres, H = vinfo.yres;
    const uint32_t C_BG   = rgb(8, 12, 24);
    const uint32_t C_FG   = rgb(230, 230, 240);
    const uint32_t C_L    = rgb(90, 200, 255);
    const uint32_t C_R    = rgb(255, 140, 90);
    const uint32_t C_NET  = rgb(60, 70, 90);

    /* Pre-render the static background (solid colour + dashed centre net).
     * Fill the FULL stride (incl. any right-edge padding) so nothing stale can
     * leak through where line_length/bytespp > xres. */
    bg_template = malloc(framesize);
    if (!bg_template) { perror("malloc"); return 1; }
    canvas = bg_template;
    fill_solid(bg_template, vinfo.yres, C_BG);
    for (int y = 0; y < H; y += 24) fill_rect(W / 2 - 2, y, 4, 14, C_NET);

    /* Wipe the ENTIRE mapped framebuffer (all virtual rows + every page) to the
     * background once, up front. This clears leftover psplash/console pixels and
     * the padded rows/columns below/right of the visible box that we never draw
     * into - otherwise they show up as stray white bars at the bottom edge. */
    fill_solid(fbp, mapsize / finfo.line_length, C_BG);

    /* --- put the LCD's VT into graphics mode (stop fbcon painting) --- */
    tty_fd = open(tty_path, O_RDWR);
    if (tty_fd >= 0) {
        if (ioctl(tty_fd, KDSETMODE, KD_GRAPHICS) == 0) restore_tty_mode = 1;
    }

    /* --- input (UART or stdin), raw & non-blocking --- */
    in_fd = strcmp(in_path, "/dev/stdin") ? open(in_path, O_RDWR | O_NONBLOCK)
                                          : STDIN_FILENO;
    if (in_fd < 0) { perror(in_path); return 1; }
    if (in_fd == STDIN_FILENO) fcntl(in_fd, F_SETFL, O_NONBLOCK);
    if (isatty(in_fd)) {
        in_is_tty = 1;
        tcgetattr(in_fd, &in_saved);
        struct termios raw = in_saved;
        cfmakeraw(&raw);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(in_fd, TCSANOW, &raw);
    }

    /* --- audio: open the ALSA "default" device (silently skipped if absent) --- */
    snd_init();

    const int PW = W / 60;          /* paddle width  */
    const int PH = H / 5;           /* paddle height */
    const int BS = W / 60;          /* ball size     */
    const int PSPEED = H / 55 + 1;  /* paddle step per active frame */
    const int AISPEED = H / 90 + 1; /* AI speed */
    const int HOLD = 6;             /* frames a keypress keeps the paddle moving */
    const int score_scale = (H / 120) + 1;
    const int banner_scale = (H / 70) + 1;  /* WIN / LOSE banner size */

    float bx = W / 2.0f, by = H / 2.0f;
    float bvx = 0, bvy = 0;
    int ly = (H - PH) / 2, ry = (H - PH) / 2;
    int l_dir = 0, l_hold = 0, r_dir = 0, r_hold = 0;
    int ai = 1, sl = 0, sr = 0;
    int serve = 1, serve_dir = 1;
    int game_over = 0, left_won = 0;   /* match state + which side won */

    const long FRAME_NS = 16666667L;      /* ~60 FPS */
    long next = now_ns();

    while (running) {
        /* ---- input ---- */
        unsigned char buf[64];
        ssize_t n = read(in_fd, buf, sizeof buf);
        for (ssize_t i = 0; i < n; i++) {
            unsigned char c = buf[i];
            if (c == 27 && i + 2 < n && buf[i + 1] == '[') { /* arrow keys */
                unsigned char a = buf[i + 2]; i += 2;
                if (a == 'A') { l_dir = -1; l_hold = HOLD; }
                else if (a == 'B') { l_dir = 1; l_hold = HOLD; }
                continue;
            }
            switch (c) {
            case 'w': case 'W': l_dir = -1; l_hold = HOLD; break;
            case 's': case 'S': l_dir =  1; l_hold = HOLD; break;
            case 'i': case 'I': case 'o': case 'O': r_dir = -1; r_hold = HOLD; ai = 0; break;
            case 'k': case 'K': case 'l': case 'L': r_dir =  1; r_hold = HOLD; ai = 0; break;
            case ' ':
                if (game_over) {                 /* start a fresh match */
                    sl = sr = 0; game_over = 0; serve = 1;
                    bx = W / 2.0f; by = H / 2.0f;
                } else if (serve) {              /* serve the ball */
                    bvx = serve_dir * (W / 130.0f);
                    bvy = ((rand() % 2) ? 1 : -1) * (H / 240.0f);
                    serve = 0;
                }
                break;
            case 'q': case 'Q': case 3: running = 0; break;
            default: break;
            }
        }

        /* ---- paddles ---- */
        if (l_hold > 0) { ly += l_dir * PSPEED; l_hold--; }
        if (r_hold > 0) { ry += r_dir * PSPEED; r_hold--; }
        else if (ai) {
            int target = (int)by - PH / 2;
            if (ry < target - AISPEED) ry += AISPEED;
            else if (ry > target + AISPEED) ry -= AISPEED;
        }
        if (ly < 0) ly = 0; if (ly > H - PH) ly = H - PH;
        if (ry < 0) ry = 0; if (ry > H - PH) ry = H - PH;

        /* ---- ball ---- */
        if (!serve && !game_over) {
            bx += bvx; by += bvy;
            /* top / bottom wall bounce - the "hit with the table" thock */
            if (by < 0)      { by = 0;          bvy = -bvy; sfx_wall(); }
            if (by > H - BS) { by = H - BS;      bvy = -bvy; sfx_wall(); }

            if (bx <= PW && by + BS >= ly && by <= ly + PH && bvx < 0) {
                bx = PW; bvx = -bvx * 1.05f;
                bvy += ((by + BS / 2) - (ly + PH / 2)) * 0.04f;
                sfx_paddle();
            }
            if (bx + BS >= W - PW && by + BS >= ry && by <= ry + PH && bvx > 0) {
                bx = W - PW - BS; bvx = -bvx * 1.05f;
                bvy += ((by + BS / 2) - (ry + PH / 2)) * 0.04f;
                sfx_paddle();
            }
            if (bx < -BS || bx > W) {
                if (bx < -BS) { sr++; serve_dir = 1; }   /* right scores */
                else          { sl++; serve_dir = -1; }  /* left scores  */
                serve = 1; bx = W / 2.0f; by = H / 2.0f;
                if (sl >= WIN_SCORE || sr >= WIN_SCORE) {
                    game_over = 1; left_won = (sl >= WIN_SCORE);
                    /* Left/blue is the human player: rising jingle on a win,
                     * falling one when they lose. */
                    if (left_won) sfx_win(); else sfx_lose();
                } else {
                    sfx_score();                          /* point conceded blip */
                }
            }
        } else {
            bx = W / 2.0f; by = H / 2.0f;
        }

        /* ---- audio: (re)open if the device was busy, then feed the ring ---- */
        snd_retry();
        snd_service();

        /* ---- render ---- */
        begin_frame();
        fill_rect(0, ly, PW, PH, C_L);
        fill_rect(W - PW, ry, PW, PH, C_R);
        fill_rect((int)bx, (int)by, BS, BS, C_FG);
        draw_number(sl, W / 2 - 60 - 9 * score_scale, 20, score_scale, C_L);
        draw_number(sr, W / 2 + 60, 20, score_scale, C_R);
        if (game_over) {
            int by_banner = H / 2 - 4 * banner_scale;   /* vertically centre the 8px glyphs */
            draw_text_centered(left_won ? "WIN" : "LOSE", W / 4,     by_banner, banner_scale, C_L);
            draw_text_centered(left_won ? "LOSE" : "WIN", 3 * W / 4, by_banner, banner_scale, C_R);
        }
        /* Keep the extreme right column as background: some parallel-RGB panels
         * latch the last buffer pixel of a line into the first displayed column,
         * which otherwise mirrors the orange paddle onto the blue (left) side. */
        fill_rect(W - 1, 0, 1, H, C_BG);
        present();

        /* ---- steady frame pacing ---- */
        next += FRAME_NS;
        long dt = next - now_ns();
        if (dt > 0) {
            struct timespec s = { dt / 1000000000L, dt % 1000000000L };
            nanosleep(&s, NULL);
        } else {
            next = now_ns();   /* we fell behind; resync instead of spiralling */
        }
    }

    return 0;
}
