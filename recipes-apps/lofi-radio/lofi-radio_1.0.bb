SUMMARY = "BeitlabOS internet lofi radio"
DESCRIPTION = "Background GStreamer player for Icecast/Shoutcast MP3 lofi \
stations. Audio only; the LCD dashboard can keep running."
HOMEPAGE = "https://github.com/beitlab-inc"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://lofi-radio.c \
           file://lofi \
           file://lofi-radio.service \
           file://stations \
           file://README"

S = "${WORKDIR}"

DEPENDS = "gstreamer1.0 gstreamer1.0-plugins-base"
RDEPENDS_${PN} = " \
    systemd \
    alsa-utils \
    gstreamer1.0 \
    gstreamer1.0-plugins-base-alsa \
    gstreamer1.0-plugins-base-audioconvert \
    gstreamer1.0-plugins-base-audioresample \
    gstreamer1.0-plugins-base-volume \
    gstreamer1.0-plugins-base-playback \
    gstreamer1.0-plugins-good-soup \
    gstreamer1.0-plugins-good-mpg123 \
    gstreamer1.0-plugins-good-autodetect \
    glib-networking \
"

inherit systemd pkgconfig

SYSTEMD_SERVICE_${PN} = "lofi-radio.service"
SYSTEMD_AUTO_ENABLE = "disable"

do_compile() {
    ${CC} ${CFLAGS} ${CPPFLAGS} ${LDFLAGS} \
        -O2 -Wall -Wextra \
        ${S}/lofi-radio.c -o ${B}/lofi-radio \
        $(pkg-config --cflags --libs gstreamer-1.0)
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/lofi-radio ${D}${bindir}/lofi-radio
    install -m 0755 ${S}/lofi ${D}${bindir}/lofi

    install -d ${D}${sysconfdir}/lofi-radio
    install -m 0644 ${S}/stations ${D}${sysconfdir}/lofi-radio/stations

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${S}/lofi-radio.service \
        ${D}${systemd_system_unitdir}/lofi-radio.service

    install -d ${D}${datadir}/doc/lofi-radio
    install -m 0644 ${S}/README ${D}${datadir}/doc/lofi-radio/README
}

CONFFILES_${PN} = "${sysconfdir}/lofi-radio/stations"
FILES_${PN} += " \
    ${systemd_system_unitdir}/lofi-radio.service \
    ${datadir}/doc/lofi-radio/README \
"
