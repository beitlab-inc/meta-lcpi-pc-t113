#ifndef BEITLAB_FBDEV_PORT_H
#define BEITLAB_FBDEV_PORT_H

int fbdev_port_init(const char *framebuffer_path, const char *tty_path);
void fbdev_port_shutdown(void);
int fbdev_port_width(void);
int fbdev_port_height(void);

#endif
