#!/bin/bash
# Build the framebuffer Doom recipe (doomgeneric + Freedoom IWAD).
set -e
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/bb.sh" doom
