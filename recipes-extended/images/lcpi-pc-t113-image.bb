DESCRIPTION = "image for lcpi-pc-t113 board(T113)"

SERIAL_CONSOLE = "115200;ttyS0"
CORE_IMAGE_EXTRA_INSTALL += " util-linux"

# alsa-tools linux-firmware
IMAGE_INSTALL = "\
    mycpp \
    piano-player \
    myserial \
    packagegroup-core-boot \
    wireless-regdb \
    wpa-supplicant \
    packagegroup-core-full-cmdline \
    systemd-serialgetty \
    util-linux-rfkill \
    dpkg apt libgpiod-tools \
    alsa-utils alsa-lib curl wget \
    ${CORE_IMAGE_EXTRA_INSTALL} \
    "

inherit core-image

IMAGE_OVERHEAD_FACTOR ?= "1.0" 
IMAGE_ROOTFS_SIZE ?= "229376"

FB_TOOLS = " \
    fb-test \
    fbset-modes \
    fbset \
    libdrm-tests \
"

WIFI_TOOLS = " \
    kernel-modules \
    rtl8189ftv \
    wpa-supplicant \
"

MISC_TOOLS += " \
    beitlab-fetch \
    can-utils \
    libsocketcan \
    strace \
    vim \
    htop \
    lsof \
    e2fsprogs-resize2fs \
    os-release \
    lsb-release \
    usbutils \
    libusbgx \
    usbgadget \
"

# -----------------------------------------------------------------------------
# Audio / video / display + game userspace (folded in from meta-lcpi-av)
# -----------------------------------------------------------------------------
# The T113-S3 has no 3D GPU (only the 2D G2D + display engine), so the graphics
# story is deliberately 2D: framebuffer console, KMS/DRM, GStreamer to the LCD
# (fbdevsink / kmssink), audio out the analog codec. No X11/Wayland/GLES stack
# (too heavy for 128MB and unaccelerated). Framebuffer games (pingpong, doom)
# are launched on demand from a login shell with `game start <name>` — they do
# not autostart at boot.
AV_TOOLS = " \
    alsa-utils \
    alsa-tools \
    alsa-state \
    alsa-unmute \
    gstreamer1.0 \
    gstreamer1.0-meta-base \
    gstreamer1.0-meta-audio \
    gstreamer1.0-meta-video \
    gstreamer1.0-plugins-base-audiotestsrc \
    gstreamer1.0-plugins-base-videotestsrc \
    gstreamer1.0-plugins-bad-fbdevsink \
    gstreamer1.0-plugins-bad-kms \
    gstreamer1.0-plugins-good-video4linux2 \
    libdrm-tests \
    fbida \
    v4l-utils \
    evtest \
    zram \
    pingpong \
    doom \
    lcpi-play \
    psplash \
    psplash-default \
    boot-chime \
"

IMAGE_INSTALL += " \
    ${WIFI_TOOLS} \
    ${FB_TOOLS} \
    ${MISC_TOOLS} \
    ${AV_TOOLS} \
"

set_8189fs_loglevel(){
    mkdir -p ${IMAGE_ROOTFS}/etc/modprobe.d
    echo 'options 8189fs rtw_drv_log_level=1' > ${IMAGE_ROOTFS}/etc/modprobe.d/8189fs.conf
}

# The kernel has no NFSD, so systemd's static proc-fs-nfsd.mount (pulled in via
# nfs-utils) fails at boot with "Failed to mount NFSD configuration filesystem".
# Harmless; mask it so the boot is clean.
mask_nfsd_mount() {
    install -d ${IMAGE_ROOTFS}${sysconfdir}/systemd/system
    ln -sf /dev/null ${IMAGE_ROOTFS}${sysconfdir}/systemd/system/proc-fs-nfsd.mount
}

# meta-oe's zram recipe ships BOTH a SysV init script (wrapped by
# systemd-sysv-generator as "zram.service", "LSB: Increase Virtual Swap...") and
# native systemd units (zram-swap.service + dev-zram0.swap). The LSB one runs
# "modprobe zram", which fails because zram is built into the kernel, so it shows
# up as a red [FAILED] at boot. Mask just the LSB-wrapped unit; the native
# systemd zram-swap path (which has the lz4 compressor) does the real work.
mask_lsb_zram() {
    install -d ${IMAGE_ROOTFS}${sysconfdir}/systemd/system
    ln -sf /dev/null ${IMAGE_ROOTFS}${sysconfdir}/systemd/system/zram.service
}

# LCD and serial consoles stay at the login prompt after boot. Framebuffer
# games are started from a shell (SSH or UART) in the background:
#   game start pingpong
#   game start doom
#   game ctl            # this terminal is the keyboard (Ctrl-] detaches)
#   game stop
# Only one game may run at a time; a second `game start` is refused until
# `game stop`. Games Conflicts= getty@tty1 only (LCD). They must NOT stop
# serial-getty@ttyS0 — that is the PB6/PB7 debug UART. Play with `game ctl`
# from SSH/serial, or a USB keyboard on the USB-A host port.

ROOTFS_POSTPROCESS_COMMAND += "set_8189fs_loglevel; mask_nfsd_mount; mask_lsb_zram;"
IMAGE_ROOTFS_EXTRA_SPACE_append = "${@bb.utils.contains("DISTRO_FEATURES", "systemd", " + 4096", "" ,d)}"
