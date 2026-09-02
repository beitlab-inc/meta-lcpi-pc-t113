#include "fbdev_port.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <lvgl/lvgl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, uint32_t)
#endif

struct framebuffer_context {
    int framebuffer_fd;
    int tty_fd;
    uint8_t *memory;
    size_t memory_size;
    struct fb_var_screeninfo variable;
    struct fb_fix_screeninfo fixed;
    lv_color_t *draw_memory;
    lv_disp_draw_buf_t draw_buffer;
    lv_disp_drv_t display_driver;
    lv_disp_t *display;
};

static struct framebuffer_context context = {
    .framebuffer_fd = -1,
    .tty_fd = -1,
    .memory = MAP_FAILED,
};

static uint32_t channel_value(uint8_t value, struct fb_bitfield channel)
{
    if (!channel.length)
        return 0;
    uint32_t maximum = channel.length >= 32 ? UINT32_MAX :
                       ((1U << channel.length) - 1U);
    return (((uint32_t)value * maximum + 127U) / 255U) << channel.offset;
}

static uint32_t framebuffer_color(lv_color_t color)
{
    lv_color32_t expanded;
    expanded.full = lv_color_to32(color);
    return channel_value(LV_COLOR_GET_R32(expanded), context.variable.red) |
           channel_value(LV_COLOR_GET_G32(expanded), context.variable.green) |
           channel_value(LV_COLOR_GET_B32(expanded), context.variable.blue);
}

static void write_pixel(uint8_t *destination, uint32_t pixel, int bytes_per_pixel)
{
    switch (bytes_per_pixel) {
    case 2:
        *(uint16_t *)destination = (uint16_t)pixel;
        break;
    case 3:
        destination[0] = (uint8_t)pixel;
        destination[1] = (uint8_t)(pixel >> 8);
        destination[2] = (uint8_t)(pixel >> 16);
        break;
    case 4:
        *(uint32_t *)destination = pixel;
        break;
    default:
        break;
    }
}

static void framebuffer_flush(lv_disp_drv_t *driver, const lv_area_t *area,
                              lv_color_t *colors)
{
    (void)driver;
    int source_width = area->x2 - area->x1 + 1;
    int x1 = area->x1 < 0 ? 0 : area->x1;
    int y1 = area->y1 < 0 ? 0 : area->y1;
    int x2 = area->x2 >= (int)context.variable.xres ?
             (int)context.variable.xres - 1 : area->x2;
    int y2 = area->y2 >= (int)context.variable.yres ?
             (int)context.variable.yres - 1 : area->y2;
    int bytes_per_pixel = (int)(context.variable.bits_per_pixel + 7U) / 8;

    if (x1 <= x2 && y1 <= y2 && bytes_per_pixel >= 2 &&
        bytes_per_pixel <= 4) {
        for (int y = y1; y <= y2; ++y) {
            int source_row = y - area->y1;
            uint8_t *destination =
                context.memory +
                (size_t)(y + context.variable.yoffset) * context.fixed.line_length +
                (size_t)(x1 + context.variable.xoffset) * bytes_per_pixel;
            lv_color_t *source =
                colors + (size_t)source_row * source_width + (x1 - area->x1);

            for (int x = x1; x <= x2; ++x) {
                write_pixel(destination, framebuffer_color(*source),
                            bytes_per_pixel);
                destination += bytes_per_pixel;
                ++source;
            }
        }
    }

    if (lv_disp_flush_is_last(driver)) {
        uint32_t argument = 0;
        ioctl(context.framebuffer_fd, FBIO_WAITFORVSYNC, &argument);
        ioctl(context.framebuffer_fd, FBIOPAN_DISPLAY, &context.variable);
    }
    lv_disp_flush_ready(driver);
}

int fbdev_port_init(const char *framebuffer_path, const char *tty_path)
{
    context.framebuffer_fd = open(framebuffer_path, O_RDWR);
    if (context.framebuffer_fd < 0) {
        fprintf(stderr, "dashboard: cannot open %s: %s\n", framebuffer_path,
                strerror(errno));
        return -1;
    }

    if (ioctl(context.framebuffer_fd, FBIOGET_FSCREENINFO, &context.fixed) < 0 ||
        ioctl(context.framebuffer_fd, FBIOGET_VSCREENINFO, &context.variable) < 0) {
        fprintf(stderr, "dashboard: cannot query framebuffer: %s\n",
                strerror(errno));
        fbdev_port_shutdown();
        return -1;
    }

    int bytes_per_pixel = (int)(context.variable.bits_per_pixel + 7U) / 8;
    if (bytes_per_pixel < 2 || bytes_per_pixel > 4) {
        fprintf(stderr, "dashboard: unsupported framebuffer depth %u\n",
                context.variable.bits_per_pixel);
        fbdev_port_shutdown();
        return -1;
    }

    context.memory_size = context.fixed.smem_len;
    if (!context.memory_size)
        context.memory_size =
            (size_t)context.fixed.line_length * context.variable.yres_virtual;
    context.memory = mmap(NULL, context.memory_size, PROT_READ | PROT_WRITE,
                          MAP_SHARED, context.framebuffer_fd, 0);
    if (context.memory == MAP_FAILED) {
        fprintf(stderr, "dashboard: cannot map framebuffer: %s\n",
                strerror(errno));
        fbdev_port_shutdown();
        return -1;
    }
    memset(context.memory, 0, context.memory_size);

    context.tty_fd = open(tty_path, O_RDWR | O_NOCTTY);
    if (context.tty_fd >= 0)
        ioctl(context.tty_fd, KDSETMODE, KD_GRAPHICS);

    unsigned int rows = context.variable.yres < 40 ? context.variable.yres : 40;
    size_t pixels = (size_t)context.variable.xres * rows;
    context.draw_memory = calloc(pixels, sizeof(*context.draw_memory));
    if (!context.draw_memory) {
        fprintf(stderr, "dashboard: cannot allocate LVGL draw buffer\n");
        fbdev_port_shutdown();
        return -1;
    }

    lv_disp_draw_buf_init(&context.draw_buffer, context.draw_memory, NULL, pixels);
    lv_disp_drv_init(&context.display_driver);
    context.display_driver.hor_res = (lv_coord_t)context.variable.xres;
    context.display_driver.ver_res = (lv_coord_t)context.variable.yres;
    context.display_driver.flush_cb = framebuffer_flush;
    context.display_driver.draw_buf = &context.draw_buffer;
    context.display_driver.full_refresh = 0;
    context.display = lv_disp_drv_register(&context.display_driver);
    if (!context.display) {
        fprintf(stderr, "dashboard: LVGL display registration failed\n");
        fbdev_port_shutdown();
        return -1;
    }

    return 0;
}

void fbdev_port_shutdown(void)
{
    if (context.tty_fd >= 0) {
        ioctl(context.tty_fd, KDSETMODE, KD_TEXT);
        close(context.tty_fd);
        context.tty_fd = -1;
    }
    free(context.draw_memory);
    context.draw_memory = NULL;
    if (context.memory != MAP_FAILED) {
        memset(context.memory, 0, context.memory_size);
        munmap(context.memory, context.memory_size);
        context.memory = MAP_FAILED;
    }
    if (context.framebuffer_fd >= 0) {
        close(context.framebuffer_fd);
        context.framebuffer_fd = -1;
    }
}

int fbdev_port_width(void)
{
    return (int)context.variable.xres;
}

int fbdev_port_height(void)
{
    return (int)context.variable.yres;
}
