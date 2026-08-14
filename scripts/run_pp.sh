#!/bin/bash
# Build just the UART-controlled framebuffer Pong (pingpong) recipe.
set -e
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/bb.sh" pingpong
