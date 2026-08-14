#!/bin/bash
# Source this to get a root-free dunfell build environment.
#
# Location-independent: it finds the build root (the directory that holds
# poky/, buildtools/, hosttools/ and this layer) relative to its own path, so
# it works no matter where the tree is checked out. Layout assumed:
#
#   <build-root>/
#   ├── buildtools/        (Yocto buildtools-extended SDK)
#   ├── hosttools/         (extra host tools on PATH)
#   ├── poky/              (oe-init-build-env lives here)
#   └── meta-lcpi-pc-t113/ (this layer)
#       └── scripts/       (this file)

_ENV_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGES_ROOT="$(cd "${_ENV_SCRIPT_DIR}/../.." && pwd)"
export IMAGES_ROOT

export PATH="${IMAGES_ROOT}/hosttools/bin:${PATH}"
source "${IMAGES_ROOT}/buildtools/environment-setup-x86_64-pokysdk-linux"
