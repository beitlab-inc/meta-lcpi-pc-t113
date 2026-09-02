SUMMARY = "GT911 capacitive-touch test for the LCPI-PC-T113 LCD"
DESCRIPTION = "Fullscreen framebuffer grid that reads the Goodix evdev node \
and paints every contact. Used to prove the P4 CTP flex is live after the \
GT911 binds. Launch with: game start touchtest"
HOMEPAGE = "https://github.com/beitlab-inc"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://touchtest.c \
           file://touchtest.service \
           file://README"

S = "${WORKDIR}"

RDEPENDS_${PN} = "lcpi-play"

inherit systemd

SYSTEMD_SERVICE_${PN} = "touchtest.service"
SYSTEMD_AUTO_ENABLE = "disable"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -O2 -Wall -Wextra -o touchtest ${S}/touchtest.c
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/touchtest ${D}${bindir}/touchtest

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${S}/touchtest.service ${D}${systemd_system_unitdir}/touchtest.service

    install -d ${D}${datadir}/doc/touchtest
    install -m 0644 ${S}/README ${D}${datadir}/doc/touchtest/README
}

FILES_${PN} += " \
    ${systemd_system_unitdir}/touchtest.service \
    ${datadir}/doc/touchtest/README \
"
