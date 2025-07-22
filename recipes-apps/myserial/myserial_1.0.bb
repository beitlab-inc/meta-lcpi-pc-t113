SUMMARY = "UART Test Application"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
SRC_URI = "file://main.cpp"

S = "${WORKDIR}"

do_compile() {
    ${CXX} ${CXXFLAGS} ${LDFLAGS} main.cpp -o uart_test -std=c++11
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 uart_test ${D}${bindir}
}
