SUMMARY = "Light and Versatile Graphics Library"
DESCRIPTION = "Minimal LVGL 8 build for direct-framebuffer BeitlabOS applications"
HOMEPAGE = "https://lvgl.io/"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENCE.txt;md5=bf1198c89ae87f043108cea62460b03a"

SRC_URI = "git://github.com/lvgl/lvgl.git;protocol=https;branch=release/v8.3 \
           file://lv_conf.h"
SRCREV = "74d0a816a440eea53e030c4f1af842a94f7ce3d3"

S = "${WORKDIR}/git"

inherit cmake

# Build only the lightweight software renderer. The dashboard supplies its own
# Linux framebuffer port, so LVGL does not pull SDL, Wayland, DRM or input
# libraries into this 128 MB image.
do_configure_prepend() {
    # Upstream's custom CMake build otherwise emits only an unversioned .so,
    # which Yocto correctly classifies as a development symlink rather than a
    # runtime library.
    cat >> ${S}/CMakeLists.txt <<'EOF'
set_target_properties(lvgl PROPERTIES VERSION "8.3.11" SOVERSION "8")
EOF
}

EXTRA_OECMAKE = " \
    -DLV_CONF_PATH=${WORKDIR}/lv_conf.h \
    -DLV_CONF_INCLUDE_SIMPLE=ON \
    -DLV_LVGL_H_INCLUDE_SIMPLE=ON \
    -DBUILD_SHARED_LIBS=ON \
    -DLIB_INSTALL_DIR=${baselib} \
    -DINC_INSTALL_DIR=include/lvgl \
"

FILES_${PN} += "${libdir}/liblvgl.so.*"
FILES_${PN}-dev += "${includedir}/lvgl ${includedir}/lv_conf.h ${libdir}/liblvgl.so"
