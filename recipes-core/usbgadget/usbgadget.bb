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

# USB string descriptors, stamped into the script at do_install so the gadget a
# host enumerates reports the same identity as the login banner. Fallbacks keep
# the recipe buildable under a non-BeitlabOS distro.
BEITLAB_VENDOR ?= "BEITLAB"
BEITLAB_BOARD ?= "${MACHINE}"
BEITLAB_RELEASE ?= "${DISTRO_VERSION}"
# The board is already named in the product string, so the serial only needs to
# carry the firmware release; the script appends the per-unit SoC ID at runtime.
BEITLAB_USB_SERIAL ?= "${BEITLAB_VENDOR}-${BEITLAB_RELEASE}"

# The board name is baked into the package, so it is machine-specific.
PACKAGE_ARCH = "${MACHINE_ARCH}"

inherit systemd

SYSTEMD_SERVICE_${PN} = "usb-gadget.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${WORKDIR}/usb-gadget-ecm.sh ${D}${bindir}/usb-gadget-ecm.sh
    sed -i \
        -e 's|@BEITLAB_VENDOR@|${BEITLAB_VENDOR}|g' \
        -e 's|@BEITLAB_PRODUCT@|${BEITLAB_BOARD}|g' \
        -e 's|@BEITLAB_SERIAL@|${BEITLAB_USB_SERIAL}|g' \
        ${D}${bindir}/usb-gadget-ecm.sh

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/usb-gadget.service ${D}${systemd_system_unitdir}/usb-gadget.service
}

FILES_${PN} += "${bindir}/usb-gadget-ecm.sh ${systemd_system_unitdir}/usb-gadget.service"
