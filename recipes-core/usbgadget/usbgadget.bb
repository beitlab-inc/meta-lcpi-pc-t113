SUMMARY = "USB OTG CDC-ECM network gadget for the LCPI-PC-T113 (BEITLAB)"
DESCRIPTION = "Creates a CDC-ECM USB-Ethernet gadget on the T113 OTG port \
(USB0/MUSB) via configfs at boot, so plugging the OTG USB-C into a PC or Mac \
gives a point-to-point network link to the board (192.168.20.2). Pairs with \
usb0.network (static IP + a small DHCP server so the host auto-configures)."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://usb-gadget-ecm.sh \
           file://usb-gadget.service"

S = "${WORKDIR}"

inherit systemd

SYSTEMD_SERVICE_${PN} = "usb-gadget.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${WORKDIR}/usb-gadget-ecm.sh ${D}${bindir}/usb-gadget-ecm.sh

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/usb-gadget.service ${D}${systemd_system_unitdir}/usb-gadget.service
}

FILES_${PN} += "${bindir}/usb-gadget-ecm.sh ${systemd_system_unitdir}/usb-gadget.service"
