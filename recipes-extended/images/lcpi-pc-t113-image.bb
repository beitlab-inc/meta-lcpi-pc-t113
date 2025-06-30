DESCRIPTION = "image for lcpi-pc-t113 board(T113)"

SERIAL_CONSOLE = "115200;ttyS0"
CORE_IMAGE_EXTRA_INSTALL += "util-linux"

IMAGE_INSTALL = "\
    mycpp \
    piano-player \
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
IMAGE_ROOTFS_SIZE ?= "204800"

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
"

IMAGE_INSTALL += " \
    ${WIFI_TOOLS} \
    ${FB_TOOLS} \
    ${MISC_TOOLS} \
"

set_8189fs_loglevel(){
    mkdir -p ${IMAGE_ROOTFS}/etc/modprobe.d
    echo 'options 8189fs rtw_drv_log_level=1' > ${IMAGE_ROOTFS}/etc/modprobe.d/8189fs.conf
}

ROOTFS_POSTPROCESS_COMMAND += "set_8189fs_loglevel;"
IMAGE_ROOTFS_EXTRA_SPACE_append = "${@bb.utils.contains("DISTRO_FEATURES", "systemd", " + 4096", "" ,d)}"
