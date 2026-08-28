#!/bin/sh
# Bring up a CDC-ECM USB-Ethernet gadget on the T113 OTG port (USB0/MUSB).
#
# CDC-ECM is supported natively by Linux and macOS (no driver needed; Windows
# would need an ECM/RNDIS driver). Once bound, plugging the OTG USB-C into a
# PC/Mac creates a point-to-point network:
#   board (this device) : 192.168.20.2/24   (see usb0.network)
#   host (PC/Mac)        : handed out by the board's tiny DHCP server
# then:  ssh root@192.168.20.2
#
# This runs from the *main* system (systemd), because the SD image boots the
# rootfs directly with no initramfs - so the initramfs ECM helper never runs.
set -e

CFG=/sys/kernel/config
GADGET="$CFG/usb_gadget/beitlab"

# systemd normally mounts configfs (sys-kernel-config.mount); mount it if not.
if [ ! -d "$CFG/usb_gadget" ]; then
	mount -t configfs none "$CFG" 2>/dev/null || true
fi
[ -d "$CFG/usb_gadget" ] || { echo "configfs/usb_gadget unavailable" >&2; exit 1; }

# Idempotent: if we already bound a UDC, there is nothing to do.
if [ -s "$GADGET/UDC" ]; then
	exit 0
fi

mkdir -p "$GADGET"
cd "$GADGET"

echo 0x1d6b > idVendor          # Linux Foundation
echo 0x0104 > idProduct         # Multifunction Composite Gadget
echo 0x0100 > bcdDevice
echo 0x0200 > bcdUSB             # USB 2.0

# Device identity. The @BEITLAB_*@ placeholders are substituted at build time
# from the distro/machine variables (see usbgadget.bb), so the serial carries
# the exact BeitlabOS release + internal build number that is running.
#
# A firmware release is not unique per unit though, and USB hosts key their
# persistent interface names off the serial number - two boards on the same
# host would collide. So append the SoC's factory-programmed 128-bit SID when
# the nvmem node is there (CONFIG_NVMEM_SUNXI_SID), and fall back to the plain
# release string if it isn't.
SERIAL="@BEITLAB_SERIAL@"
sid_nvmem=$(ls /sys/bus/nvmem/devices/sunxi-sid*/nvmem 2>/dev/null | head -n1)
if [ -n "$sid_nvmem" ] && [ -r "$sid_nvmem" ]; then
	uid=$(od -An -tx1 -N16 "$sid_nvmem" 2>/dev/null | tr -d ' \n' || true)
	[ -n "$uid" ] && SERIAL="$SERIAL-$(printf '%s' "$uid" | tail -c 8)"
fi

mkdir -p strings/0x409
echo "$SERIAL"           > strings/0x409/serialnumber
echo "@BEITLAB_VENDOR@"  > strings/0x409/manufacturer
echo "@BEITLAB_PRODUCT@" > strings/0x409/product

mkdir -p configs/c.1/strings/0x409
echo "CDC-ECM (USB Ethernet)" > configs/c.1/strings/0x409/configuration
echo 250 > configs/c.1/MaxPower

# CDC-ECM function. Fixed, locally-administered MACs keep the interface names
# stable across reboots on both the board and the host.
mkdir -p functions/ecm.usb0
echo "02:1a:11:00:00:02" > functions/ecm.usb0/dev_addr    # board (device) side
echo "02:1a:11:00:00:01" > functions/ecm.usb0/host_addr   # PC/Mac side
ln -sf functions/ecm.usb0 configs/c.1/

# Bind to the OTG USB Device Controller. The MUSB UDC is created by the built-in
# driver during kernel init, but allow a moment in case we race it at boot.
udc=""
i=0
while [ "$i" -lt 50 ]; do
	udc=$(ls /sys/class/udc 2>/dev/null | head -n1)
	[ -n "$udc" ] && break
	i=$((i + 1))
	sleep 0.1
done
[ -n "$udc" ] || { echo "no UDC found - is the OTG port in device mode?" >&2; exit 1; }
echo "$udc" > UDC

echo "usb-gadget: CDC-ECM bound to UDC $udc (board 192.168.20.2)"
