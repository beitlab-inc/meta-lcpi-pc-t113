SUMMARY = "Force the sun20i codec output path on at boot and persist mixer state"
DESCRIPTION = "The T113-S3 sun20i codec powers up muted, and unmutes that run \
before the card is ready silently no-op - so audio works after a manual poke but \
is silent again on the next boot. This installs a oneshot service that waits for \
the ALSA card, unmutes/raises the output controls (name-independent) and runs \
alsactl store, so the speaker is live on every boot for both the boot chime and \
the pingpong game."
HOMEPAGE = "https://example.invalid/lcpi-av"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://lcpi-audio-unmute.sh \
           file://alsa-unmute.service"

S = "${WORKDIR}"

inherit systemd

# amixer + alsactl live in alsa-utils.
RDEPENDS_${PN} = "alsa-utils"

SYSTEMD_SERVICE_${PN} = "alsa-unmute.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/lcpi-audio-unmute.sh ${D}${bindir}/lcpi-audio-unmute.sh

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${S}/alsa-unmute.service ${D}${systemd_system_unitdir}/alsa-unmute.service
}

FILES_${PN} += "${systemd_system_unitdir}/alsa-unmute.service"
