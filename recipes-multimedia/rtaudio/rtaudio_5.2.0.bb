SUMMARY = "Realtime Audio I/O library"
HOMEPAGE = "https://www.music.mcgill.ca/~gary/rtaudio/"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "https://github.com/thestk/rtaudio/archive/refs/tags/5.2.0.tar.gz;downloadfilename=rtaudio-5.2.0.tar.gz"
SRC_URI[sha256sum] = "a8d9c738addffd485c3f0bab14cbba72600267e3113f274398c67829bbb49332"

S = "${WORKDIR}/rtaudio-5.2.0"

inherit cmake pkgconfig

DEPENDS = "alsa-lib"

EXTRA_OECMAKE += "-DRTAUDIO_API_ALSA=ON -DRTAUDIO_BUILD_STATIC_LIBS=OFF"
