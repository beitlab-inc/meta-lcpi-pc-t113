SUMMARY = "Simple piano note generator using RtAudio"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://piano_player.cpp"

S = "${WORKDIR}"

DEPENDS = "rtaudio"

do_compile() {
    ${CXX} ${CXXFLAGS} ${LDFLAGS} piano_player.cpp -o piano_player -lrtaudio -lpthread -std=c++11
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 piano_player ${D}${bindir}/piano_player
}
