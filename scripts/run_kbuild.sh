#!/bin/bash
# Build just the kernel (linux-mainline) recipe.
set -e
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/bb.sh" linux-mainline
