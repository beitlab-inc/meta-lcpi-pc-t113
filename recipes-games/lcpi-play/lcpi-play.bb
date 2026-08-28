SUMMARY = "Background start/stop helper for LCPI framebuffer games"
DESCRIPTION = "Installs /usr/bin/game so pingpong, doom and later titles can \
be launched from a login shell in the background. `game ctl` attaches the \
SSH/serial terminal as a keyboard (FIFO). Only one game is allowed to run \
at a time; `game stop` terminates the active one and restores the LCD."
HOMEPAGE = "https://example.invalid/lcpi-av"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://game \
           file://lcpi-restore-console.c \
           file://lcpi-game-ctl.c"

S = "${WORKDIR}"

RDEPENDS_${PN} = "systemd util-linux"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -O2 -Wall -o lcpi-restore-console \
        ${S}/lcpi-restore-console.c
    ${CC} ${CFLAGS} ${LDFLAGS} -O2 -Wall -o lcpi-game-ctl \
        ${S}/lcpi-game-ctl.c
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/game ${D}${bindir}/game
    install -m 0755 ${B}/lcpi-restore-console ${D}${bindir}/lcpi-restore-console
    install -m 0755 ${B}/lcpi-game-ctl ${D}${bindir}/lcpi-game-ctl
}

FILES_${PN} = "${bindir}/game ${bindir}/lcpi-restore-console ${bindir}/lcpi-game-ctl"
