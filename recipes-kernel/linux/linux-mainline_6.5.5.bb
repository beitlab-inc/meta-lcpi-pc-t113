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

    # GT911 on the P4 6-pin CTP flex (MangoPi MQ-Dual / LCPI schematic):
    #   I2C2 = PE12/PE13, 7-bit addr 0x14
    #   INT  = PB3, RST = PB2
    # The inherited node used the 8-bit write address 0x28 and the SPI0
    # pins PC3/PC2. That reset sequence never reached the chip, so the
    # Goodix probe timed out talking to 0x14 (-110).
    sed -i 's/gt911: touchscreen@28/gt911: touchscreen@14/' "${dts}"
    sed -i '/gt911: touchscreen@14 {/,/};/{s/reg = <0x28>;/reg = <0x14>;/}' "${dts}"
    sed -i '/gt911: touchscreen@14 {/,/};/{
        s/interrupts = <2 3 IRQ_TYPE_EDGE_FALLING>;/interrupts = <1 3 IRQ_TYPE_EDGE_FALLING>;/
        s/irq-gpios = <\&pio 2 3 GPIO_ACTIVE_HIGH>;/irq-gpios = <\&pio 1 3 GPIO_ACTIVE_HIGH>;/
        s/reset-gpios = <\&pio 2 2 GPIO_ACTIVE_HIGH>;/reset-gpios = <\&pio 1 2 GPIO_ACTIVE_HIGH>;/
    }' "${dts}"

    if ! grep -q "LCPI GT911 touch on P4" "${dts}"; then
        cat >> "${dts}" <<'EOF'

/* --- LCPI GT911 touch on P4 (CTP flex); free unused SPI-NAND pinmux --- */
&spi0 {
	status = "disabled";
};
EOF
    fi

    # Without an alias, i2c2 registers as /dev/i2c-0 (hence "0-0014" in dmesg).
    if ! grep -q "LCPI i2c2 alias" "${dts}"; then
        cat >> "${dts}" <<'EOF'

/* --- LCPI i2c2 alias so the CTP bus is /dev/i2c-2 --- */
/ {
	aliases {
		i2c2 = &i2c2;
	};
};
EOF
    fi

    # P4 has no documented pull-up on CTP_RST. The Goodix driver releases
    # reset then switches the GPIO to input; the line floats, the chip
    # drops back into reset, and i2cdetect on i2c2 is empty. Keep RST
    # driven released, pull it up in pinctrl, and run TWI2 at 100 kHz.
    goodix_c="${S}/drivers/input/touchscreen/goodix.c"
    if [ -f "${goodix_c}" ] && ! grep -q "LCPI keep GT911 RST driven" "${goodix_c}"; then
        sed -i '/Put the reset pin back in to input/,/return 0;/{
            s/if (ts->irq_pin_access_method == IRQ_PIN_ACCESS_GPIO)/if (0 \/* LCPI keep GT911 RST driven *\/ \&\& ts->irq_pin_access_method == IRQ_PIN_ACCESS_GPIO)/
        }' "${goodix_c}"
    fi

    if ! grep -q "LCPI GT911 RST hold" "${dts}"; then
        cat >> "${dts}" <<'EOF'

/* --- LCPI GT911 RST hold + 100 kHz TWI2 --- */
&i2c2 {
	clock-frequency = <100000>;
};
EOF
    fi

    # Live board: pinctrl-0 on the GT911 node claimed PB2, then
    # reset-gpios failed with -EINVAL (-22) and probe aborted.
    if ! grep -q "LCPI GT911 no extra pinctrl" "${dts}"; then
        cat >> "${dts}" <<'EOF'

/* --- LCPI GT911 no extra pinctrl (PB2 is reset-gpios only) --- */
&gt911 {
	/delete-property/ pinctrl-0;
	/delete-property/ pinctrl-names;
};
EOF
    fi

    # Live board: pinmux PE12/PE13 is already i2c2, but every xfer logs
    # "mv64xxx: I2C bus locked" and times out (-110). The inherited
    # sun8i-t113s.dtsi attaches DMA to i2c2; that path never completes
    # an IRQ on this SoC. Drop DMA and enable on-chip pull-ups so SCL
    # can rise even if the panel flex has none.
    if ! grep -q "LCPI i2c2 PIO no-DMA" "${dts}"; then
        cat >> "${dts}" <<'EOF'

/* --- LCPI i2c2 PIO no-DMA (bus-locked workaround) --- */
&i2c2_pe12_pins {
	bias-pull-up;
};

&i2c2 {
	/delete-property/ dmas;
	/delete-property/ dma-names;
	clock-frequency = <100000>;
};
EOF
    fi
}
