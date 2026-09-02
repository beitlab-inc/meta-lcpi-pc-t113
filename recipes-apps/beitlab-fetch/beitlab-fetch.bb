SUMMARY = "neofetch-style system summary with the Beitlab logo"
DESCRIPTION = "Installs /usr/bin/beitlab-fetch, which prints the Beitlab mark \
from github.com/beitlab-inc as terminal art next to a block of system \
information (board, kernel, uptime, CPU, memory, zram, rootfs, SoC \
temperature, address). neofetch needs bash and fastfetch has no recipe on \
this release, so this is a dependency-free POSIX sh implementation that reads \
/proc and /sys directly. It is run on demand; set BEITLAB_FETCH_ON_LOGIN to 1 \
to also print it on interactive login. Works over the 115200 baud UART, the \
LCD getty and SSH."
HOMEPAGE = "https://github.com/beitlab-inc"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://beitlab-fetch \
           file://beitlab-logo-large.txt \
           file://beitlab-logo-small.txt \
           file://beitlab-fetch-profile.sh"

S = "${WORKDIR}"

# Only the C library and a shell are strictly required; df/wc/tty come from
# coreutils or busybox, both of which are already in packagegroup-core-boot.
RDEPENDS_${PN} = ""

# Identity shown in the banner. These are the same variables the login banner
# and MOTD use (conf/distro/beitlabos.conf, conf/machine/lcpi-pc-t113.conf);
# the fallbacks keep the recipe buildable under any other distro or machine.
BEITLAB_PRODUCT ?= "Beitlab Embedded Platform"
BEITLAB_BOARD ?= "${MACHINE}"
BEITLAB_YOCTO_CODENAME ?= "${DISTRO_CODENAME}"
BEITLAB_YOCTO_VERSION ?= "${DISTRO_VERSION}"

# beitlab-fetch is a command, not a login banner: the /etc/profile.d hook is
# installed but inert by default. Set to "1" (here, in local.conf, or in
# /etc/beitlab-fetch.conf on the target) to print it on interactive login.
BEITLAB_FETCH_ON_LOGIN ?= "0"

# The generated config embeds MACHINE/DISTRO identity, so it must not be
# shared between machines via sstate.
PACKAGE_ARCH = "${MACHINE_ARCH}"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/beitlab-fetch ${D}${bindir}/beitlab-fetch

    install -d ${D}${datadir}/beitlab-fetch
    install -m 0644 ${S}/beitlab-logo-large.txt ${D}${datadir}/beitlab-fetch/logo-large.txt
    install -m 0644 ${S}/beitlab-logo-small.txt ${D}${datadir}/beitlab-fetch/logo-small.txt

    install -d ${D}${sysconfdir}
    cat > ${D}${sysconfdir}/beitlab-fetch.conf <<'BEITLAB_FETCH_CONF_EOF'
# Defaults for beitlab-fetch(1). Baked from the machine and distro config at
# build time; edit on the target to override.

BEITLAB_PRODUCT="${BEITLAB_PRODUCT}"
BEITLAB_BOARD="${BEITLAB_BOARD}"
BEITLAB_YOCTO="${BEITLAB_YOCTO_CODENAME} ${BEITLAB_YOCTO_VERSION}"

# auto | large | small | none - "auto" picks by terminal width.
BEITLAB_FETCH_LOGO=auto

# auto | always | never
BEITLAB_FETCH_COLOR=auto

# 1 draws the mark with block characters, 0 falls back to '#' for consoles
# without a Unicode font.
BEITLAB_FETCH_ASCII=0

# Counting installed packages is the one field that shells out to the package
# manager; set to 0 if login latency matters more than the number.
BEITLAB_FETCH_PACKAGES=1

# 1 prints the summary from /etc/profile.d on interactive login shells; 0 means
# it only runs when you type `beitlab-fetch`.
BEITLAB_FETCH_ON_LOGIN=${BEITLAB_FETCH_ON_LOGIN}
BEITLAB_FETCH_CONF_EOF
    chmod 0644 ${D}${sysconfdir}/beitlab-fetch.conf

    install -d ${D}${sysconfdir}/profile.d
    install -m 0644 ${S}/beitlab-fetch-profile.sh ${D}${sysconfdir}/profile.d/beitlab-fetch.sh
}

FILES_${PN} = "${bindir}/beitlab-fetch \
               ${datadir}/beitlab-fetch \
               ${sysconfdir}/beitlab-fetch.conf \
               ${sysconfdir}/profile.d/beitlab-fetch.sh"

CONFFILES_${PN} = "${sysconfdir}/beitlab-fetch.conf"
