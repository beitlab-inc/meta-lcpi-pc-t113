# Corporate identity in /etc/os-release (the machine-readable half of the
# branding: anything reading the OS name programmatically - lsb_release,
# systemd, a fleet-management agent, our own apps - looks here).
#
# NAME / VERSION / PRETTY_NAME / ID already follow DISTRO_NAME and
# DISTRO_VERSION from conf/distro/beitlabos.conf. The stock recipe just doesn't
# emit these extra fields, so add them to OS_RELEASE_FIELDS. Empty values are
# skipped by the recipe, so an unset URL simply doesn't appear.
#
# This recipe is allarch: keep every value here machine-independent.

BEITLAB_VENDOR ?= "BEITLAB"
BEITLAB_PRODUCT ?= "Beitlab Embedded Platform"
BEITLAB_BUILD ?= ""
BEITLAB_URL ?= ""
BEITLAB_SUPPORT ?= ""

OS_RELEASE_FIELDS_append = " VARIANT HOME_URL SUPPORT_URL BUILD_ID"

VARIANT = "${BEITLAB_PRODUCT}"
HOME_URL = "${BEITLAB_URL}"
SUPPORT_URL = "${BEITLAB_SUPPORT}"

# Upstream defaults BUILD_ID to DATETIME, which makes it useless for tracing a
# board back to a release. Pin it to the internal build number instead.
BUILD_ID = "${BEITLAB_BUILD}"
