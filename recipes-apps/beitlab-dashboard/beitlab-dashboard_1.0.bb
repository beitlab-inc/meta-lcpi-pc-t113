SUMMARY = "BeitlabOS LVGL system and network dashboard"
DESCRIPTION = "An 800x480 corporate dashboard showing time, CPU, memory, \
temperature, storage and a rolling ten-second network throughput chart."
HOMEPAGE = "https://github.com/beitlab-inc"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://dashboard.c \
           file://metrics.c \
           file://metrics.h \
           file://location.c \
           file://location.h \
           file://fbdev_port.c \
           file://fbdev_port.h \
           file://beitlab_identity.h.in \
           file://beitlab-dashboard.service \
           file://beitlab-dashboard-restore \
           file://README"

S = "${WORKDIR}"

DEPENDS = "lvgl"
RDEPENDS_${PN} = "systemd util-linux lcpi-play curl tzdata"

inherit systemd

SYSTEMD_SERVICE_${PN} = "beitlab-dashboard.service"
# Validate it on the panel first. `systemctl enable beitlab-dashboard` turns it
# into the default boot UI without requiring another image build.
SYSTEMD_AUTO_ENABLE = "disable"

BEITLAB_VENDOR ?= "BEITLAB"
BEITLAB_PRODUCT ?= "Beitlab Embedded Platform"
BEITLAB_BOARD ?= "${MACHINE}"
BEITLAB_RELEASE ?= "${DISTRO_VERSION}"
BEITLAB_BUILD ?= "unknown"

PACKAGE_ARCH = "${MACHINE_ARCH}"

do_compile() {
    sed \
        -e 's|@BEITLAB_VENDOR@|${BEITLAB_VENDOR}|g' \
        -e 's|@BEITLAB_PRODUCT@|${BEITLAB_PRODUCT}|g' \
        -e 's|@BEITLAB_BOARD@|${BEITLAB_BOARD}|g' \
        -e 's|@BEITLAB_RELEASE@|${BEITLAB_RELEASE}|g' \
        -e 's|@BEITLAB_BUILD@|${BEITLAB_BUILD}|g' \
        ${S}/beitlab_identity.h.in > ${B}/beitlab_identity.h

    ${CC} ${CFLAGS} ${CPPFLAGS} ${LDFLAGS} \
        -O2 -Wall -Wextra -DLV_CONF_INCLUDE_SIMPLE \
        -I${B} \
        ${S}/dashboard.c ${S}/metrics.c ${S}/location.c ${S}/fbdev_port.c \
        -o ${B}/beitlab-dashboard -llvgl -lm -lpthread
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/beitlab-dashboard ${D}${bindir}/beitlab-dashboard
    install -m 0755 ${S}/beitlab-dashboard-restore \
        ${D}${bindir}/beitlab-dashboard-restore

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${S}/beitlab-dashboard.service \
        ${D}${systemd_system_unitdir}/beitlab-dashboard.service

    install -d ${D}${datadir}/doc/beitlab-dashboard
    install -m 0644 ${S}/README ${D}${datadir}/doc/beitlab-dashboard/README
}

FILES_${PN} += " \
    ${systemd_system_unitdir}/beitlab-dashboard.service \
    ${datadir}/doc/beitlab-dashboard/README \
"
