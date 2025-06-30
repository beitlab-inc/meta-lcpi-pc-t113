DESCRIPTION = "Simple C++ App"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://main.cpp;md5=4ec7679f7aa4fb2257abdee7a4578d3d"
DEPENDS += "libgpiod"

SRC_URI = "file://main.cpp \
           file://Makefile"

S = "${WORKDIR}"

do_compile() {
    oe_runmake
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 mycpp ${D}${bindir}/mycpp
}
