#!/bin/bash
# Build the full board image (lcpi-pc-t113-image -> .wic/.ext4/.tar.bz2).
set -e
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/bb.sh" lcpi-pc-t113-image
