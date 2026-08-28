DESCRIPTION = "Linux Kernel from Tarball"
SECTION = "kernel"
LICENSE = "GPLv2"

inherit kernel
require recipes-kernel/linux/linux-yocto.inc

LIC_FILES_CHKSUM = "file://COPYING;md5=6bc538ed5bd9a7fc9398086aedcd7e46"

SRC_URI = "\
    https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${PV}.tar.xz \
    file://001-second_core_support_in_platsmp.patch \
    file://002-add-mangopi-dual-dtb.patch \
    file://defconfig \
    "
SRC_URI[sha256sum] = "8cf10379f7df8ea731e09bff3d0827414e4b643dd41dc99d0af339669646ef95"

# ---------------------------------------------------------------------------
# Audio / video / display enablement (formerly the separate meta-lcpi-av layer)
# ---------------------------------------------------------------------------
# Extra kernel options (GT911 touch, framebuffer console, PWM backlight, zram +
# the lz4/lzo/zstd compressors zram-swap needs) merged via kernel-yocto's
# automatic *.cfg handling.
SRC_URI += "file://lcpi-av.cfg"

# The BSP device tree enables an "allwinner,sun20i-d1-codec" node, but mainline
# 6.5.5 has NO driver for the T113s/D1 internal analog codec (never upstreamed).
# Ship Samuel Holland's sun20i-codec driver - the source the BSP's codec DT node
# was written for - and build it in.
SRC_URI += "file://sun20i-codec.c"

# The codec's analog output is powered by two on-die LDOs (ALDO->avcc,
# HPLDO->hpvcc) whose enable+voltage bits live in the codec POWER register
# (0x2030348). Upstream models these as a separate "sun20i-analog-ldos"
# regulator, but that driver is absent from mainline 6.5.5 AND its eFuse
# "bg_trim" cell is a bitfield that nvmem_cell_read_u8() rejects on this kernel
# (-EINVAL) - which left the codec deferring on its avcc/hpvcc supplies and its
# reset stuck (-EBUSY), so no card registered. Instead, sun20i-codec.c now powers
# those LDOs itself in its component probe (see the POWER-register writes there),
# so the codec is fully self-contained: no extra regulator driver, no DT supply
# refs, and no codec<->child-regulator dependency cycle.

LINUX_VERSION ?= "${PV}"
LINUX_VERSION_EXTENSION_append = "-custom"

S = "${WORKDIR}/linux-${PV}"
COMPATIBLE_MACHINE = "sun8i"

# The BSP synthesises a "MangoPi-Dual" device tree via 002-add-mangopi-dual-dtb.patch.
# Retarget it to the LCPI-PC-T113 kit hardware just before the kernel is configured
# (after do_patch has regenerated ${S}, before do_compile builds the dtb).
DTS_FILE ?= "${S}/arch/arm/boot/dts/allwinner/sun8i-t113-mangopi-dual.dts"

do_configure_prepend() {
    # --- Drop in the T113s/D1 internal audio codec driver (built-in) ---
    sunxi_snd="${S}/sound/soc/sunxi"
    if [ -d "${sunxi_snd}" ]; then
        cp -f "${WORKDIR}/sun20i-codec.c" "${sunxi_snd}/sun20i-codec.c"
        if ! grep -q "sun20i-codec.o" "${sunxi_snd}/Makefile"; then
            echo 'obj-y += sun20i-codec.o' >> "${sunxi_snd}/Makefile"
        fi
    else
        bbfatal "sound/soc/sunxi not found; cannot install sun20i codec driver"
    fi

    dts="${DTS_FILE}"
    if [ ! -f "${dts}" ]; then
        bbfatal "Expected device tree ${dts} not found; the BSP DTS patch layout changed"
    fi

    # Panel: keep the BSP default "rocktech,rk070er9427" (800x480, RGB666), which
    # matches the kit's 4.3" 800x480 parallel-RGB glass and the board's 18-bit
    # lcd_rgb666_pins wiring. (An earlier attempt forced the ampire 480x272 entry,
    # but the hardware is 800x480 - a 480x272 image only filled the top-left of the
    # panel. RGB666 also gives correct colours on the 18-line bus.) No sed here.

    # 32MB CMA is ample for an 800x480 framebuffer + G2D and leaves more of the
    # 128MB DDR3 for userspace.
    sed -i 's/cma=64M/cma=32M/' "${dts}"

    # Clean boot splash on the LCD.
    # We do NOT mirror the kernel console to tty0: otherwise boot logs would paint
    # over the psplash company logo on the panel. Logs stay on the serial console
    # (ttyS0) for debugging. Strip any previously-added "console=tty0 " (keeps this
    # idempotent even on a non-clean kernel tree).
    sed -i 's/console=tty0 console=ttyS0,115200/console=ttyS0,115200/' "${dts}"

    # Panel cmdline tweaks for a tidy splash (idempotent):
    #   consoleblank=0             - never blank the panel after idle
    #   quiet loglevel=3           - keep the boot quiet (serial still gets warnings)
    #   vt.global_cursor_default=0 - hide the blinking VT cursor before psplash draws
    if ! grep -q "consoleblank=0" "${dts}"; then
        sed -i 's/console=ttyS0,115200/console=ttyS0,115200 consoleblank=0 quiet loglevel=3 vt.global_cursor_default=0/' "${dts}"
    fi

    # --- Audio codec: routing/widgets + analog-LDO supplies ---
    # The BSP's &codec node only sets status="okay"; it has NO "routing" property.
    # But the sun20i-codec driver calls snd_soc_of_parse_audio_routing(,"routing")
    # UNCONDITIONALLY and bails if it is missing:
    #   ASoC: Property 'routing' does not exist ... Failed to initialize card (-EINVAL)
    # Append (idempotently) a machine routing + widgets so the card registers.
    #
    # NOTE: we deliberately DO NOT set avcc-supply/hpvcc-supply/vdd33-supply here.
    # Pointing avcc-supply at reg_aldo (a child of the codec node) created a
    # codec<->child-regulator dependency cycle and made DAPM defer on those
    # supplies forever (the analog-ldos regulator driver also failed on its eFuse
    # bg_trim cell). sun20i-codec.c now powers the ALDO/HPLDO itself in its probe,
    # so the DAPM "avcc"/"hpvcc"/"vdd33" supplies fall back to dummy regulators
    # and the card probes cleanly on the first try.
    if ! grep -q "LCPI audio routing" "${dts}"; then
        cat >> "${dts}" <<'EOF'

/* --- LCPI audio routing (folded in from meta-lcpi-av) --- */
&codec {
	widgets =
		"Headphone", "Headphone Jack",
		"Line", "Line Out Jack",
		"Microphone", "Mic Jack";

	routing =
		"Headphone Jack", "HPOUTL",
		"Headphone Jack", "HPOUTR",
		"Line Out Jack", "LINEOUTL",
		"Line Out Jack", "LINEOUTR",
		"MICIN1", "Mic Jack";
};
EOF
    fi

    # Move the Linux debug console off UART0/PE2/PE3 (those pins are the
    # camera DVP PCLK/MCLK on P3) onto UART3 TX/RX on PB6/PB7 (P2 header).
    # UART0 cannot mux onto PB6/PB7 on the T113; UART3 is the controller that
    # does. Alias it as serial0 so userspace still sees /dev/ttyS0.
    if ! grep -q "LCPI UART3 console on PB6/PB7" "${dts}"; then
        sed -i 's/serial0 = \&uart0;/serial0 = \&uart3;/' "${dts}"
        sed -i 's/earlyprintk=sunxi-uart,0x2500000/earlyprintk=sunxi-uart,0x2500c00/' "${dts}"
        cat >> "${dts}" <<'EOF'

/* --- LCPI UART3 console on PB6/PB7 (was UART0 on PE2/PE3) --- */
&pio {
	uart3_pb6_pins: uart3-pb6-pins {
		pins = "PB6", "PB7";
		function = "uart3";
	};
};

&uart0 {
	status = "disabled";
};

&uart3 {
	pinctrl-0 = <&uart3_pb6_pins>;
	pinctrl-names = "default";
	status = "okay";
};
EOF
    fi
}
