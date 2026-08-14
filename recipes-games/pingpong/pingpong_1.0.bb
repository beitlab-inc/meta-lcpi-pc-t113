SUMMARY = "UART-controlled framebuffer Pong for the LCPI-PC-T113 LCD"
DESCRIPTION = "A tiny dependency-free Pong that draws directly to /dev/fb0, puts \
the LCD virtual terminal into graphics mode, and takes its controls from a UART. \
Suitable for the T113-S3 (128MB RAM, 2D-only, no X11/Wayland)."
HOMEPAGE = "https://example.invalid/lcpi-av"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://pingpong.c \
           file://pingpong.service"

S = "${WORKDIR}"

# Sound effects are synthesised in-process and played through ALSA (libasound);
# amixer (alsa-utils) is used by the service to unmute the codec output path.
DEPENDS = "alsa-lib"
RDEPENDS_${PN} = "alsa-utils"

inherit systemd

SYSTEMD_SERVICE_${PN} = "pingpong.service"
# Boot straight into the game (kiosk). Set to "disable" to keep the console login
# and start it on demand with: systemctl start pingpong
SYSTEMD_AUTO_ENABLE = "enable"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -O2 -Wall -o pingpong ${S}/pingpong.c -lasound -lm
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/pingpong ${D}${bindir}/pingpong

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${S}/pingpong.service ${D}${systemd_system_unitdir}/pingpong.service
}

FILES_${PN} += "${systemd_system_unitdir}/pingpong.service"
