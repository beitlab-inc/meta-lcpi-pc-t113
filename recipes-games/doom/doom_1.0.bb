SUMMARY = "Framebuffer Doom (doomgeneric) for the LCPI-PC-T113 LCD"
DESCRIPTION = "doomgeneric with a Linux framebuffer backend plus the Freedoom \
Phase 1 IWAD. Draws to /dev/fb0 and takes controls from a USB keyboard. \
The serial console on PB6/PB7 stays usable. Launch with: game start doom; game ctl"
HOMEPAGE = "https://github.com/ozkl/doomgeneric"
LICENSE = "GPL-2.0 & BSD-3-Clause"
LIC_FILES_CHKSUM = "\
    file://${COMMON_LICENSE_DIR}/GPL-2.0;md5=801f80980d171dd6425610833a22dbe6 \
    file://${COMMON_LICENSE_DIR}/BSD-3-Clause;md5=550794465ba0ec5312d6919e203a55f9 \
"

SRC_URI = "\
    git://github.com/ozkl/doomgeneric.git;protocol=https;branch=master \
    https://github.com/freedoom/freedoom/releases/download/v0.13.0/freedoom-0.13.0.zip;name=wad \
    file://doomgeneric_fbdev.c \
    file://Makefile.fbdev \
    file://doom.service \
"
SRCREV = "dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284"
SRC_URI[wad.sha256sum] = "3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59"

S = "${WORKDIR}/git/doomgeneric"

inherit systemd

SYSTEMD_SERVICE_${PN} = "doom.service"
SYSTEMD_AUTO_ENABLE = "disable"

RDEPENDS_${PN} = "lcpi-play"
DEPENDS += "unzip-native"

do_compile() {
    install -m 0644 ${WORKDIR}/doomgeneric_fbdev.c ${S}/doomgeneric_fbdev.c
    install -m 0644 ${WORKDIR}/Makefile.fbdev ${S}/Makefile.fbdev
    oe_runmake -f Makefile.fbdev \
        CC="${CC}" \
        CFLAGS="${CFLAGS}" \
        LDFLAGS="${LDFLAGS}"
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/doomgeneric ${D}${bindir}/doomgeneric

    install -d ${D}${datadir}/games/doom
    wad=$(find ${WORKDIR} -name 'freedoom1.wad' | head -n1)
    if [ -z "$wad" ]; then
        bbfatal "freedoom1.wad not found after unpacking Freedoom"
    fi
    install -m 0644 "$wad" ${D}${datadir}/games/doom/freedoom1.wad

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/doom.service ${D}${systemd_system_unitdir}/doom.service
}

FILES_${PN} += "${datadir}/games/doom/freedoom1.wad ${systemd_system_unitdir}/doom.service"
