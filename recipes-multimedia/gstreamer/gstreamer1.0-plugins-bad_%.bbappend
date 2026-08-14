# Build the KMS video sink (kmssink) in addition to the default fbdev sink so we
# can push video straight to the sun4i-drm display on the LCD.
PACKAGECONFIG_append = " kms"

# The T113-S3 has no 3D GPU, and with no X11/Wayland there is no GStreamer-GL
# window-system platform in -base, so gstreamer-gl-1.0 is never produced. Drop
# the GL elements here so meson does not fail looking for it.
PACKAGECONFIG_remove = " gl"
