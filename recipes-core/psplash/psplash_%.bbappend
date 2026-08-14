FILESEXTRAPATHS_prepend := "${THISDIR}/files:"

# Rebrand the framebuffer boot splash with the LCPI company logo.
#
# psplash builds ONE small executable per splash image, baking the picture into
# the binary as a C header generated at build time (make-image-header.sh, via
# gdk-pixbuf-native). By pointing SPLASH_IMAGES at our PNG we replace the stock
# Poky logo; the "outsuffix=default" keeps the produced package name
# "psplash-default" and its /usr/bin/psplash alternative, so nothing else in the
# recipe needs to change.
#
# To use your own artwork: drop an 8-bit RGB PNG at files/company-logo.png
# (any size - psplash centres it on the panel and paints the progress bar below)
# and rebuild:  bitbake -c cleansstate psplash && bitbake psplash
#
# SRC_URI already appends ${SPLASH_IMAGES}, so we only override the variable and
# make our files/ dir searchable (FILESEXTRAPATHS above).
SPLASH_IMAGES = "file://company-logo.png;outsuffix=default"

# Replace the boot progress bar with an indeterminate rotating spinner. Boot
# progress isn't metered on this board, so a spinning "comet" of fading dots
# below the logo looks more polished than a bar that jumps around. The patch
# also drives the animation from psplash's fifo loop (short select() timeout).
SRC_URI += "file://0001-Replace-progress-bar-with-rotating-spinner.patch"

# The company logo is a full-screen (800x480) BLACK image. psplash defaults to a
# light-grey background (#ecece1) and, with PSPLASH_IMG_FULLSCREEN=0, positions
# the logo in the upper 5/6 "split" - shifting the 800x480 image up by 40px and
# leaving the bottom strip (plus the MSG/clear band) painted light. Those light
# areas are the two horizontal bars seen at the bottom of the panel.
#
# Fix: paint the whole splash black and centre the logo full-screen so it covers
# the entire panel. Colours live in psplash-colors.h (plain #defines, no ifndef
# guards) and the layout switch in psplash-config.h, so rewrite them before
# configure rather than fighting -D redefinition warnings.
do_configure_prepend() {
    sed -i \
        -e 's/#define PSPLASH_BACKGROUND_COLOR .*/#define PSPLASH_BACKGROUND_COLOR 0x00,0x00,0x00/' \
        -e 's/#define PSPLASH_BAR_BACKGROUND_COLOR .*/#define PSPLASH_BAR_BACKGROUND_COLOR 0x00,0x00,0x00/' \
        -e 's/#define PSPLASH_TEXT_COLOR .*/#define PSPLASH_TEXT_COLOR 0xff,0xff,0xff/' \
        ${S}/psplash-colors.h
    sed -i \
        -e 's/#define PSPLASH_IMG_FULLSCREEN .*/#define PSPLASH_IMG_FULLSCREEN 1/' \
        ${S}/psplash-config.h
}
