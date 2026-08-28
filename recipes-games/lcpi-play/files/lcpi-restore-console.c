// SPDX-License-Identifier: MIT
// Put /dev/tty1 back in text mode after a framebuffer game exits.
#include <fcntl.h>
#include <linux/kd.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(void)
{
    int fd = open("/dev/tty1", O_RDWR);
    if (fd < 0)
        return 0;
    ioctl(fd, KDSETMODE, KD_TEXT);
    close(fd);
    return 0;
}
