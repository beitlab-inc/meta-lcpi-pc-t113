# meta-lcpi-pc-t113

BSP layer for the [**LCPI-PC-T113**](https://linux-sunxi.org/LCPI-PC-T113/F113)
single-board computer, which uses the Allwinner **T113-S3** SoC (dual Cortex-A7,
128 MB in-package DDR3).

This layer boots the board with a mainline **Linux 6.5.5** kernel and the
[AWBoot](https://github.com/szemzoa/awboot) bootloader, and produces a flashable
`.wic` SD-card image.

> **Everything is in this one layer.** The 4.3" 800×480 parallel-RGB LCD, GT911
> capacitive touch, the internal analog audio codec (+ its analog-LDO regulator),
> a lean 2D media stack, and a UART-controlled framebuffer game all live here.
> (These used to be a separate `meta-lcpi-av` overlay; they were folded in so the
> BSP is self-contained — see [Display, audio & the LCD game](#display-audio--the-lcd-game).)

---

## Table of contents

1. [What works](#what-works)
2. [Repository layout](#repository-layout)
3. [Quick start (upstream / clean host)](#quick-start-upstream--clean-host)
4. [Required build config (local.conf)](#required-build-config-localconf)
5. [Display, audio & the LCD game](#display-audio--the-lcd-game)
6. [Flashing](#flashing)
7. [How this layer is put together](#how-this-layer-is-put-together)
8. [Building on a modern / restricted host](#building-on-a-modern--restricted-host)
9. [Credits](#credits)

---

## What works

| Peripheral                 | Status | Notes |
|----------------------------|:------:|-------|
| UART0 serial console       |   ✅   | `ttyS0`, 115200 |
| WiFi (RTL8189FTV)          |   ✅   | out-of-tree `rtl8189ftv` module |
| Boot (AWBoot + zImage)     |   ✅   | |
| 4.3" 800×480 RGB LCD       |   ✅   | `rocktech,rk070er9427`, RGB666 18-bit bus |
| GT911 capacitive touch     |   ✅   | Goodix GT911 on I²C |
| Internal analog audio      |   ✅¹  | backported self-contained `sun20i-codec` |
| GStreamer media (2D)       |   ✅   | `fbdevsink` / `kmssink`, no GL |
| Branded boot splash        |   ✅   | `psplash` logo + rotating spinner on black (no penguin) |
| UART-controlled LCD game   |   ✅   | `pingpong` boots on the panel |

¹ The codec driver is built in and powers its own on-die analog LDOs, so the card
enumerates as `aplay -l` card 0 with no extra regulator driver. The output paths
are DAPM switches **muted at every power-on**, so the `alsa-unmute` service
(`recipes-multimedia/alsa-unmute/`) forces them on at boot — no manual `alsamixer`
needed. See [Audio](#display-audio--the-lcd-game).

---

## Repository layout

```
meta-lcpi-pc-t113/
├── conf/
│   ├── layer.conf                         # layer metadata (priority 5)
│   ├── machine/lcpi-pc-t113.conf          # MACHINE definition (T113-S3, sun8i)
│   ├── local.conf.sample                  # template local.conf
│   └── bblayers.conf.sample               # template bblayers.conf
├── recipes-bsp/
│   ├── awboot/                            # AWBoot bootloader + patches
│   └── xfel/                              # xfel USB flashing tool
├── recipes-kernel/
│   ├── linux/
│   │   ├── linux-mainline_6.5.5.bb        # mainline kernel recipe (+ A/V, see below)
│   │   └── linux-mainline/
│   │       ├── 001-second_core_support_in_platsmp.patch  # 2nd Cortex-A7
│   │       ├── 002-add-mangopi-dual-dtb.patch            # T113 device tree
│   │       ├── defconfig                                 # kernel config
│   │       ├── spi-nor.cfg                               # SPI-NOR fragment
│   │       ├── lcpi-av.cfg                               # touch/fbcon/PWM-bl/zram config
│   │       └── sun20i-codec.c                            # backported analog codec driver (self-powers its LDOs)
│   └── rtl8189/                           # out-of-tree WiFi driver
├── recipes-connectivity/wpa-supplicant/   # WiFi supplicant config
├── recipes-core/                          # systemd getty, networking, initramfs
│   ├── psplash/                           # boot-splash bbappend + company-logo.png
│   ├── usbgadget/                         # OTG CDC-ECM USB-Ethernet gadget (PC/Mac)
│   └── systemd/systemd-conf/              # wlan0.network + usb0.network
├── recipes-apps/                          # sample apps (mycpp, myserial, piano-player)
├── recipes-games/pingpong/                # UART-controlled framebuffer Pong (LCD kiosk)
│   ├── pingpong_1.0.bb
│   ├── README.md                          # how to enable/disable the game + UART
│   └── files/{pingpong.c, pingpong.service}
├── recipes-multimedia/
│   ├── alsa-unmute/                       # boot service: force the muted-at-boot codec on
│   │   ├── alsa-unmute.bb
│   │   └── files/{lcpi-audio-unmute.sh, alsa-unmute.service}
│   ├── boot-chime/                        # power-on chime played early at boot
│   │   ├── boot-chime.bb
│   │   └── files/{bootchime.c, boot-chime.service}
│   ├── rtaudio/                           # RtAudio library
│   └── gstreamer/                         # gstreamer1.0-plugins-bad bbappend (kms, no gl)
├── recipes-extended/images/
│   └── lcpi-pc-t113-image.bb              # the main image recipe (+ A/V, game)
└── wic/
    ├── lcpi-pc-t113.wks                   # SD-card partition layout
    └── t113-boot.wks
```

---

## Quick start (upstream / clean host)

This is the classic flow, using Yocto **dunfell** and the upstream template
config. On a supported host it "just works"; if your host is newer (Ubuntu 24.04,
Python 3.12+) see [Building on a modern / restricted host](#building-on-a-modern--restricted-host).

**1. Clone the layers**
```bash
git clone git://git.yoctoproject.org/poky -b dunfell
cd poky/
git clone https://git.yoctoproject.org/meta-arm -b dunfell
git clone https://github.com/openembedded/meta-openembedded.git -b dunfell
git clone https://github.com/AndresJejen/meta-lcpi-pc-t113.git -b dunfell
cd ../
```

**2. Initialise the build environment** (uses this layer's template config)
```bash
export TEMPLATECONF=${TEMPLATECONF:-meta-lcpi-pc-t113/conf}
source poky/oe-init-build-env lcpi-pc-t113
```

**3. Build**
```bash
bitbake lcpi-pc-t113-image
```

**4. Flash** — see [Flashing](#flashing).

**5. Enjoy :-)**

---

## Required build config (local.conf)

The image needs `meta-openembedded/meta-oe` on the layer path (for zram, fbida,
evtest, v4l-utils, alsa-tools, …) and a couple of `local.conf` choices. The
board boots **systemd** and a **2D-only** graphics stack (the T113-S3 has no 3D
GPU), so:

```
# --- 2D headless-HMI graphics: no X11/Wayland/Vulkan, keep systemd (+ egl) ---
DISTRO_FEATURES_append = " systemd opengl"
DISTRO_FEATURES_remove = " x11 wayland vulkan"
VIRTUAL-RUNTIME_init_manager = "systemd"

# Keep "sysvinit" in DISTRO_FEATURES (the default). This BSP runs systemd as
# PID 1, but was authored/tested with the default sysvinit+systemd feature set;
# dropping sysvinit distro-wide produced an unbootable rootfs
# ("No working init found"). The one failing LSB-wrapped zram service is masked
# in the image recipe instead (see below), which is the surgical, boot-safe fix.
```

`Dropbear` SSH + `debug-tweaks` are handy while bringing the board up (the LCD
boots into the game, so SSH is your shell — see below):

```
EXTRA_IMAGE_FEATURES += "debug-tweaks ssh-server-dropbear"
```

---

## Display, audio & the LCD game

All of this is built by **this** layer (folded in from the former `meta-lcpi-av`
overlay), so a plain `bitbake lcpi-pc-t113-image` produces a fully-featured image.

**Display (LCD)** — the kernel recipe keeps the BSP's default panel
`rocktech,rk070er9427` (**4.3" 800×480**, RGB666 on the 18-bit `lcd_rgb666_pins`
bus), drops CMA to 32 MB, adds `console=tty0` (mirror the log to the panel) and
`consoleblank=0` (never blank the LCD). Touch is the Goodix **GT911** (enabled in
`lcpi-av.cfg`).

**Audio** — mainline 6.5.5 has **no** driver for the T113s/D1 internal analog
codec (upstreamed only ~v6.13), so the kernel recipe backports Samuel Holland's
`sun20i-codec.c` and builds it in:

- `sun20i-codec.c` → registers the ALSA card. It requires a `routing` DT property,
  which the recipe appends to the `&codec` node along with `widgets`.
- The codec's analog output is fed by two on-die LDOs — **ALDO (avcc)** and
  **HPLDO (hpvcc)**. Upstream drives these via a separate `sun20i-analog-ldos`
  regulator, but that driver is missing from 6.5.5 **and** its eFuse `bg_trim`
  cell is a bitfield that `nvmem_cell_read_u8()` rejects (`-EINVAL`), which left
  the codec deferring on its supplies and its reset stuck (`-EBUSY`). Instead the
  backported codec **powers the LDOs itself** in its component probe (bandgap
  trim + 1.8 V + enable, written to the always-on POWER register), so the card is
  self-contained: no extra regulator driver, no DT supply refs, and no
  codec↔child-regulator dependency cycle.

**Unmute-at-boot (`recipes-multimedia/alsa-unmute/`)** — the codec's output
DAPM switches come up **muted on every power-on**, so audio that worked after a
manual `alsamixer` would go silent again after a reboot. To make sound "just
work" on every boot (for both the game and the boot chime), this layer installs
a tiny oneshot service:

- `files/lcpi-audio-unmute.sh` — waits (up to ~5 s) for the ALSA card to appear,
  then unmutes and raises **every** simple control (so it does not depend on the
  exact control names being `Headphone`/`DAC`), and finally runs `alsactl store`
  so the state also persists for `alsa-restore`.
- `files/alsa-unmute.service` — a `Type=oneshot` unit, auto-enabled and ordered
  `Before=boot-chime.service pingpong.service`, so the path is live before either
  opens the PCM.

It needs no configuration; a freshly flashed card is audible on first boot. To
check or drive it manually on the board:

```bash
systemctl status alsa-unmute.service          # did it run?
journalctl -u alsa-unmute.service -b          # what it did this boot
/usr/bin/lcpi-audio-unmute.sh                 # re-run the unmute by hand
```

To **tighten** it to only your real output controls, read the names on the board
(`amixer scontrols` / `amixer contents`) and replace the "unmute every control"
loop in `lcpi-audio-unmute.sh` with explicit `amixer -q sset '<name>' unmute
100%` lines. To **disable** the auto-unmute, set
`SYSTEMD_AUTO_ENABLE = "disable"` in `recipes-multimedia/alsa-unmute/alsa-unmute.bb`
(or drop `alsa-unmute` from the image recipe).

Manual check / bring-up on the board:

```bash
aplay -l                       # card 0 should now exist
alsamixer                      # inspect levels; 'M' toggles mute
speaker-test -Dhw:0 -c2 -twav  # test tone
alsactl store                  # persist the current mixer state
```

**Boot chime (`recipes-multimedia/boot-chime/`)** — an original synthesised
power-on chord (`bootchime.c`) played once early in boot via
`boot-chime.service` (ordered after `alsa-unmute`). Swap the tone by editing the
frequencies/envelope in `bootchime.c`; remove `boot-chime` from the image recipe
to drop it.

**Media stack** — ALSA utils + a curated GStreamer set: `fbdevsink`/`kmssink` to
the sun4i-drm display, audio, and v4l2 (`cedrus` H.264/… decoder shows up as
`/dev/video0`). The `gstreamer1.0-plugins-bad` bbappend enables `kms` and drops
the `gl` PACKAGECONFIG (no GL platform without X11/Wayland). **zram** compressed
swap stretches the 128 MB DDR3.

Runtime bring-up:
```bash
fbi -d /dev/fb0 -T 1 image.png       # show an image on the LCD (fbida)
evtest                               # pick the Goodix device, tap the screen
gst-launch-1.0 videotestsrc ! fbdevsink   # test pattern to the framebuffer
gst-launch-1.0 videotestsrc ! kmssink     # or straight to KMS/DRM
```
> Note: `pingpong` owns `/dev/fb0` + `/dev/tty1` while it runs — `systemctl stop
> pingpong` first if you want the framebuffer for `fbi`/GStreamer.

**Boot splash (company logo + progress bar)** — instead of the kernel penguins
and scrolling boot logs, the panel shows a branded [psplash](https://git.yoctoproject.org/psplash/)
splash while the system comes up. Three pieces make this work:

1. **No penguin, quiet panel** — the kernel fragment disables the built-in logo
   (`# CONFIG_LOGO is not set`) and the kernel cmdline drops `console=tty0` and
   adds `quiet loglevel=3 vt.global_cursor_default=0` (see the kernel recipe's
   `do_configure_prepend`). Boot logs still go to the **serial** console
   (`ttyS0`); the LCD stays clean for the splash.
2. **psplash** (`recipes-core/psplash/psplash_%.bbappend`) draws the logo to
   `/dev/fb0` from `sysinit.target` (`psplash-start.service`). Boot progress isn't
   metered on this board, so a patch
   (`files/0001-Replace-progress-bar-with-rotating-spinner.patch`) swaps the
   progress bar for an indeterminate **rotating green spinner**, and the bbappend
   paints the whole splash **black** (matching the full-screen logo) via a
   `do_configure_prepend` that sets `PSPLASH_BACKGROUND_COLOR`/`PSPLASH_TEXT_COLOR`
   and `PSPLASH_IMG_FULLSCREEN=1`. The logo is baked into the binary at build time
   from `recipes-core/psplash/files/company-logo.png`.
3. **Hand-off to the game** — `pingpong.service` runs `psplash-write "QUIT"` in
   `ExecStartPre` (and waits a moment) so psplash releases the framebuffer before
   pingpong takes it over.

To use your own logo, drop an 8-bit RGB PNG (any size; it is centred on the
panel) at `recipes-core/psplash/files/company-logo.png` and rebuild:

```bash
bitbake -c cleansstate psplash && bitbake lcpi-pc-t113-image
```

Splash colours (background/bar/text) are compile-time `#define`s in psplash's
`psplash-colors.h`; this layer already rewrites them to black/white in the
bbappend's `do_configure_prepend` (edit those `sed` lines to re-theme). The
spinner size/position/colour live in the spinner patch
(`PSPLASH_SPINNER_*` and the `fg[]`/`bg[]` arrays). To drop the splash entirely,
remove `psplash` / `psplash-default` from the image recipe.

**LCD game (kiosk)** — `recipes-games/pingpong` is a tiny, dependency-free
framebuffer Pong that boots on the panel instead of a login prompt. It draws
straight to `/dev/fb0`, puts `/dev/tty1` into `KD_GRAPHICS`, and reads controls
from the UART (`/dev/ttyS0`):

- Left paddle `w`/`s` (or ↑/↓), right paddle `i`/`k` (disables the AI),
  serve `space`, quit `q`.
- `pingpong.service` `Conflicts=` `getty@tty1` + `serial-getty@ttyS0`, so it owns
  the VT and the serial port. **Your shell is SSH** while it runs. To get a
  serial/LCD console back:
  ```bash
  systemctl stop pingpong
  systemctl start serial-getty@ttyS0    # optional: serial login
  ```
- Prefer a normal login on the LCD instead of the game? Set
  `SYSTEMD_AUTO_ENABLE = "disable"` in `recipes-games/pingpong/pingpong_1.0.bb`
  and re-add a `getty@tty1` enable in the image recipe.

---

## Connectivity

**WiFi (RTL8189FTV)** — `wlan0` uses `wpa_supplicant` +
`systemd-networkd` (DHCP, see `recipes-connectivity/wpa-supplicant` and
`systemd-conf/wlan0.network`). SSH in with Dropbear (`debug-tweaks` allows
passwordless root):

```bash
ssh -o HostKeyAlgorithms=+ssh-rsa root@<board-ip>
```

**USB OTG network (PC/Mac, no WiFi needed)** — the two USB-C ports are two
different controllers:

| Port | Controller | Role | Use |
|------|-----------|------|-----|
| **OTG** | USB0 MUSB (dual-role) | device by default | USB-Ethernet to a PC/Mac; power in; FEL |
| **HOST** | USB1 EHCI/OHCI | host only | keyboard, storage, USB-serial, hub, … |

The `usbgadget` recipe (`recipes-core/usbgadget/`) creates a **CDC-ECM**
USB-Ethernet gadget on the OTG port at boot (systemd `usb-gadget.service` →
`usb-gadget-ecm.sh`, via configfs). `systemd-conf/usb0.network` gives the board
`192.168.20.2/24` **and runs a small DHCP server**, so the host auto-configures.
CDC-ECM is native on Linux and **macOS** (Windows needs an ECM/RNDIS driver).

Plug OTG USB-C into the PC/Mac, then:

```bash
# macOS/Linux get an address automatically from the board's DHCP server:
ssh -o HostKeyAlgorithms=+ssh-rsa root@192.168.20.2
```

If you prefer a manual host IP (no DHCP), set the new USB interface to
`192.168.20.1/24` and SSH to `.2`. The kernel already has the gadget stack built
in (`USB_MUSB_DUAL_ROLE`, `USB_CONFIGFS_ECM`, `LIBCOMPOSITE`, `CONFIGFS_FS`), so
no kernel change is needed. To expose a **USB serial console** (`/dev/ttyGS0`) or
**mass storage** alongside the network, add an `acm.usb0` / `mass_storage.usb0`
function in `usb-gadget-ecm.sh` (composite gadget).

---

## Flashing

The image is produced at
`tmp/deploy/images/lcpi-pc-t113/lcpi-pc-t113-image-lcpi-pc-t113.wic`.

**Preferred — `bmaptool`** (fast, sparse-aware; uses the generated `.wic.bmap`):
```bash
bmaptool copy \
  tmp/deploy/images/lcpi-pc-t113/lcpi-pc-t113-image-lcpi-pc-t113.wic \
  /dev/sdX
```

**Fallback — `dd`:**
```bash
sudo dd if=tmp/deploy/images/lcpi-pc-t113/lcpi-pc-t113-image-lcpi-pc-t113.wic \
  of=/dev/sdX bs=4M conv=fsync status=progress
```

> Replace `/dev/sdX` with your SD card. Double-check with `lsblk` — writing to the
> wrong device will destroy data.

You can also flash over USB (FEL mode) with the bundled **xfel** tool.

---

## How this layer is put together

- **`conf/machine/lcpi-pc-t113.conf`** — defines the machine: `SOC_FAMILY=sun8i`,
  Cortex-A7 tune, `virtual/kernel = linux-mainline`, `virtual/bootloader =
  awboot`, `KERNEL_IMAGETYPE = zImage`, device tree
  `allwinner/sun8i-t113-mangopi-dual.dtb`, and `IMAGE_FSTYPES` including
  `wic wic.bmap`. It also trims heavy `DISTRO_FEATURES` (`pci 3g nfc wayland
  vulkan bluetooth …`).
- **`recipes-kernel/linux/linux-mainline_6.5.5.bb`** — fetches the mainline 6.5.5
  tarball and applies two patches: SMP support for the **second Cortex-A7 core**,
  and the **MangoPi-Dual device tree** that describes the T113 hardware. Config
  comes from `defconfig` (+ `spi-nor.cfg`). It also folds in the **A/V
  enablement**: merges `lcpi-av.cfg` (GT911 touch, framebuffer console, PWM
  backlight, zram + lz4/lzo/zstd, and `# CONFIG_LOGO is not set` to drop the boot
  penguins), builds in the backported self-contained `sun20i-codec` driver
  (`do_configure_prepend` copies the source into the kernel tree and appends
  `obj-y`), and edits the device tree in place (CMA 32 MB; a clean-splash cmdline
  that keeps logs on serial only — `consoleblank=0 quiet loglevel=3
  vt.global_cursor_default=0`, no `console=tty0`; and the codec
  `routing`/`widgets`). All DT edits are idempotent; changing `SRC_URI` re-unpacks
  a clean tree so builds stay deterministic.
- **`recipes-bsp/awboot/`** — builds the **AWBoot** bootloader, including patches
  to fix the Makefile path, disable the FS cache, and enable SPI-NOR boot.
- **`recipes-kernel/rtl8189/`** — out-of-tree **RTL8189FTV** WiFi driver, built as
  a kernel module.
- **`recipes-extended/images/lcpi-pc-t113-image.bb`** — the image: core boot +
  cmdline tools, WiFi tooling, framebuffer tools, ALSA utils, the `AV_TOOLS`
  group (GStreamer, ALSA, fbida, v4l-utils, evtest, zram, `pingpong`), and debug
  utilities. At rootfs post-process it lowers the RTL8189 log level and masks two
  cosmetic boot failures: the LSB-wrapped `zram.service` (duplicate of the native
  systemd unit; the kernel has zram built in) and `proc-fs-nfsd.mount` (no NFSD in
  the kernel).
- **`recipes-core/psplash/`** — bbappend that rebrands the framebuffer boot
  splash with `files/company-logo.png` (the BEITLAB green-brain logo on black,
  Apple-startup style; swap the PNG to use different artwork).
- **`recipes-games/pingpong/`** — the UART-controlled framebuffer game shown on
  the LCD (`pingpong.c` + `pingpong.service`), auto-enabled as a kiosk. It quits
  psplash in `ExecStartPre` before grabbing the framebuffer.
- **`recipes-multimedia/gstreamer/`** — `gstreamer1.0-plugins-bad` bbappend that
  enables `kms` and removes the `gl` PACKAGECONFIG.
- **`recipes-multimedia/alsa-unmute/`** — oneshot boot service that forces the
  muted-at-boot sun20i codec output path on (and persists it), so audio works on
  every boot without a manual `alsamixer`. Ordered before the chime and the game.
- **`recipes-multimedia/boot-chime/`** — a synthesised power-on chime
  (`bootchime.c`) played once early in boot.
- **`recipes-apps/`** — small example payloads (`mycpp`, `myserial`,
  `piano-player`) showing how to add your own applications.

---

## Building on a modern / restricted host

Yocto **dunfell** predates modern distros and expects an older Python and a set
of host tools. On **Ubuntu 24.04 / Python 3.12** without root, the following was
needed to build this BSP successfully (all captured for reproducibility):

1. **Yocto buildtools-extended tarball** — provides a compatible Python 3.8 and
   several host tools without touching the system:
   ```bash
   # download the dunfell buildtools-extended tarball, install it, then:
   source /path/to/buildtools/environment-setup-x86_64-pokysdk-linux
   ```
2. **`gawk` and `diffstat` from source** — a couple of host tools were still
   missing/incompatible; they were built from source and prepended to `PATH`
   (see `hosttools/` in this workspace).
3. **Don't let the IDE watch the big build dirs.** IDE file watchers can exhaust
   `fs.inotify.max_user_watches` (root-locked at 65536) once `tmp/` grows, which
   makes `bitbake` fail to start with `inotify ... No space left on device`.
   Either point the large dirs outside the workspace in `local.conf`:
   ```
   TMPDIR     = "${TOPDIR}/tmp"
   DL_DIR     = "${TOPDIR}/downloads"
   SSTATE_DIR = "${TOPDIR}/sstate-cache"
   ```
   …and exclude `build/tmp`, `build/downloads`, `build/sstate-cache`, `poky`,
   `meta-openembedded`, `meta-arm` from the editor's file watcher/search (in this
   workspace that's done via `.vscode/settings.json`).
4. **Fetching `meta-arm`** — use the Yocto mirror if GitHub fails:
   `https://git.yoctoproject.org/meta-arm`.

With those in place, `bitbake lcpi-pc-t113-image` completes and produces the
`.wic` image.

---

## Credits

- Thanks to the [AWBoot](https://github.com/szemzoa/awboot) contributors — their
  bootloader and kernel patches are used directly in this layer.
- Inspired by [meta-mangopi](https://github.com/ArashEM/meta-mangopi) by ArashEM.
- The backported `sun20i-codec` analog-codec driver comes from Samuel Holland's
  [`smaeul/linux`](https://github.com/smaeul/linux) `d1/all` tree (adapted here to
  power its own on-die analog LDOs so no separate regulator driver is needed).
