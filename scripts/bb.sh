#!/bin/bash
# Wrapper: set up the root-free dunfell build env and run bitbake with args.
#   usage: scripts/bb.sh <target> [more bitbake args...]
set -e

_BB_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${_BB_SCRIPT_DIR}/env.sh"
source "${IMAGES_ROOT}/poky/oe-init-build-env" "${IMAGES_ROOT}/build" >/dev/null
exec bitbake "$@"
