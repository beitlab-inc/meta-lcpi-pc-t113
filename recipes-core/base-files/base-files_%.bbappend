# Corporate login banner (/etc/issue, /etc/issue.net), MOTD and hostname.
#
# Stock base-files builds /etc/issue as "<DISTRO_NAME> <DISTRO_VERSION> \n \l",
# which is what produces the upstream
#   "Poky (Yocto Project Reference Distro) 3.1.33 lcpi-pc-t113 ttyS0"
# line on the UART. We replace that generator with a branded, multi-line banner
# that also names the board and the Yocto base it was built from.
#
# The identity variables come from conf/distro/beitlabos.conf (product, internal
# release/build number) and conf/machine/lcpi-pc-t113.conf (board name,
# hostname); the fallbacks below keep this bbappend usable under any other
# distro.

BEITLAB_PRODUCT ?= "Beitlab Embedded Platform"
BEITLAB_VENDOR ?= "BEITLAB"
BEITLAB_BOARD ?= "${MACHINE}"
BEITLAB_URL ?= ""
BEITLAB_SUPPORT ?= ""
BEITLAB_YOCTO_CODENAME ?= "${DISTRO_CODENAME}"
BEITLAB_YOCTO_VERSION ?= "${DISTRO_VERSION}"

# Hostname, which agetty prints as the "<host> login:" prompt right after the
# banner. Defaults to MACHINE (upstream behaviour) unless a machine conf sets it.
BEITLAB_HOSTNAME ?= "${MACHINE}"
hostname = "${BEITLAB_HOSTNAME}"

# Set to "0" for a plain-ASCII banner (see the escape stripping in the task).
BEITLAB_BANNER_COLOR ?= "1"

# agetty expands these escapes when it prints /etc/issue, just before the
# "<hostname> login:" prompt:
#   \l  the terminal this login runs on (ttyS0 on the debug UART, tty1 on the
#       LCD) - this is the "terminal" part of the old banner
#   \r  kernel release      \m  machine arch      \n  hostname
#   \e  ESC, which is how the colours below get in
# The heredocs are quoted so the shell keeps the backslashes verbatim; BitBake
# still expands ${...} when it writes the task script.
#
# /etc/issue.net is left plain: it is the pre-login banner handed to network
# logins, its escapes are telnetd-style (%h = hostname) rather than agetty's,
# and whatever consumes it may not be a terminal at all.
do_install_basefilesissue () {
	cat > ${D}${sysconfdir}/issue <<'BEITLAB_ISSUE_EOF'
\e[1;32m${DISTRO_NAME} ${DISTRO_VERSION}\e[0m  -  \e[1m${BEITLAB_PRODUCT}\e[0m
Board: \e[1m${BEITLAB_BOARD}\e[0m
Console: \l   Kernel: \r   \e[2mYocto base: ${BEITLAB_YOCTO_CODENAME} ${BEITLAB_YOCTO_VERSION}\e[0m

BEITLAB_ISSUE_EOF

	cat > ${D}${sysconfdir}/issue.net <<'BEITLAB_ISSUE_NET_EOF'
${DISTRO_NAME} ${DISTRO_VERSION}  -  ${BEITLAB_PRODUCT}
Board: ${BEITLAB_BOARD}   Host: %h
Authorized use only. Activity may be logged and monitored.

BEITLAB_ISSUE_NET_EOF

	chmod 0644 ${D}${sysconfdir}/issue ${D}${sysconfdir}/issue.net
}

# Shown after a successful login (serial, LCD and SSH).
do_install_append () {
	cat > ${D}${sysconfdir}/motd <<'BEITLAB_MOTD_EOF'
\e[1;32m${DISTRO_NAME} ${DISTRO_VERSION}\e[0m  |  ${BEITLAB_PRODUCT}
\e[2m${BEITLAB_BOARD}  |  built on Yocto ${BEITLAB_YOCTO_CODENAME} ${BEITLAB_YOCTO_VERSION}\e[0m
BEITLAB_MOTD_EOF

	if [ -n "${BEITLAB_SUPPORT}" ]; then
		echo "Support: ${BEITLAB_SUPPORT}" >> ${D}${sysconfdir}/motd
	fi
	if [ -n "${BEITLAB_URL}" ]; then
		echo "${BEITLAB_URL}" >> ${D}${sysconfdir}/motd
	fi
	echo >> ${D}${sysconfdir}/motd

	# The banner is authored once, with "\e" markers, and rendered here:
	# agetty turns "\e" into ESC itself when it prints /etc/issue, but
	# /etc/motd is only cat'd by login, so that one needs real ESC bytes.
	# With colour off, both files get the escapes stripped instead.
	if [ "${BEITLAB_BANNER_COLOR}" = "1" ]; then
		sed -i 's/\\e/\x1b/g' ${D}${sysconfdir}/motd
	else
		sed -i 's/\\e\[[0-9;]*m//g' ${D}${sysconfdir}/issue ${D}${sysconfdir}/motd
	fi

	chmod 0644 ${D}${sysconfdir}/motd
}
