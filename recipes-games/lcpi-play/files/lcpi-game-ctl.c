// SPDX-License-Identifier: MIT
// Attach this terminal to the running framebuffer game.
//
// The game itself never opens SSH or /dev/ttyS0 (that froze the login).
// Keys are copied to the FIFO the game already has open. Ctrl-] detaches
// and leaves the game running; use `game stop` to quit.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

#define FIFO "/run/lcpi-game.input"
#define DETACH 0x1d /* Ctrl-] */

static struct termios saved;
static int have_saved;
static int fifo_fd = -1;

static void restore(void)
{
    if (have_saved)
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    have_saved = 0;
    if (fifo_fd >= 0) {
        close(fifo_fd);
        fifo_fd = -1;
    }
}

static void on_signal(int s)
{
    (void)s;
    restore();
    _exit(0);
}

int main(void)
{
    struct termios raw;
    struct sigaction sa;
    int i;

    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "game ctl: stdin is not a terminal\n");
        return 1;
    }

    /* game start returns before ExecStart always opens the FIFO. */
    for (i = 0; i < 40; i++) {
        fifo_fd = open(FIFO, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (fifo_fd >= 0)
            break;
        if (errno != ENOENT && errno != ENXIO)
            break;
        usleep(50000);
    }
    if (fifo_fd < 0) {
        fprintf(stderr,
                "game ctl: nothing is listening (%s).\n"
                "Start a game first, then attach:\n"
                "  game start doom\n"
                "  game ctl\n",
                FIFO);
        return 1;
    }

    {
        int flags = fcntl(fifo_fd, F_GETFL);
        if (flags >= 0)
            fcntl(fifo_fd, F_SETFL, flags & ~O_NONBLOCK);
    }

    /* Writes to a dead game should not kill this process with SIGPIPE. */
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    fprintf(stderr,
            "Attached to the LCD game. Type here; look at the panel.\n"
            "  Ctrl-]  detach (game keeps running)\n"
            "  then:   game stop\n"
            "Doom: WASD/arrows move, j/f fire, space use, Enter start, Esc menu\n"
            "Pong: w/s left, i/k right, space serve, q quit\n");

    if (tcgetattr(STDIN_FILENO, &saved) < 0) {
        perror("tcgetattr");
        restore();
        return 1;
    }
    have_saved = 1;
    raw = saved;
    cfmakeraw(&raw);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) {
        perror("tcsetattr");
        restore();
        return 1;
    }

    for (;;) {
        unsigned char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0)
            break;
        if (c == DETACH)
            break;
        if (write(fifo_fd, &c, 1) != 1) {
            fprintf(stderr, "\ngame ctl: game closed the input pipe\n");
            break;
        }
    }

    restore();
    fprintf(stderr, "Detached. Game is still running until: game stop\n");
    return 0;
}
