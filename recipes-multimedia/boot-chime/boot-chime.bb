SUMMARY = "Original power-on chime played at boot via ALSA"
DESCRIPTION = "A tiny C program that synthesises a warm major-chord start-up \
chime and plays it once through the ALSA default device, plus a systemd unit \
that unmutes the sun20i codec's headphone amp path and plays it at boot."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://bootchime.c \
           file://boot-chime.service"

S = "${WORKDIR}"

DEPENDS = "alsa-lib"
# amixer (from alsa-utils) is used by the service to unmute the codec first.
RDEPENDS_${PN} = "alsa-utils"

inherit systemd

SYSTEMD_SERVICE_${PN} = "boot-chime.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -O2 -Wall -o bootchime ${S}/bootchime.c -lasound -lm
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/bootchime ${D}${bindir}/bootchime

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${S}/boot-chime.service ${D}${systemd_system_unitdir}/boot-chime.service
}

FILES_${PN} += "${systemd_system_unitdir}/boot-chime.service"
