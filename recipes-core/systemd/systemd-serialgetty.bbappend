FILESEXTRAPATHS_prepend := "${THISDIR}/files:"

SRC_URI += " \
    file://serial-getty@ttyS0.service \
"

do_install_append() {
    install -m 0644 ${WORKDIR}/serial-getty@ttyS0.service ${D}${systemd_unitdir}/system/
    # enable the service
    ln -sf  ${systemd_unitdir}/system/serial-getty@ttyS0.service \
            ${D}${sysconfdir}/systemd/system/getty.target.wants/serial-getty@ttyS0.service
}
