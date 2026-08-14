#!/bin/sh
# lcpi-audio-unmute - force the sun20i codec output path on at boot.
#
# The T113-S3 sun20i codec powers up MUTED, and any unmute that runs before the
# card is registered silently no-ops. This script waits for the ALSA mixer to
# appear, then unmutes and raises every simple control (codec-name independent:
# it enables the DAC/Headphone volumes AND the output-mixer routing switches
# without needing to know the exact control names), and finally persists the
# state so alsa-restore keeps it across reboots too.
#
# It is intentionally aggressive but safe on this board: there is no microphone
# loopback path, so unmuting/maxing every control just guarantees the speaker
# output is live. SPDX-License-Identifier: MIT

set -u

# 1) Wait up to ~5s for the sound card / mixer to become available.
i=0
while [ "$i" -lt 50 ]; do
    if [ -n "$(amixer scontrols 2>/dev/null)" ]; then
        break
    fi
    i=$((i + 1))
    sleep 0.1
done

# No card showed up - nothing to do (don't fail the boot).
[ -n "$(amixer scontrols 2>/dev/null)" ] || exit 0

# 2) Apply any previously saved state first (best effort).
alsactl restore >/dev/null 2>&1 || true

# 3) Unmute + raise every simple control. Each op is best-effort so controls
#    that are enum/switch-only (no volume) or volume-only (no switch) never fail
#    the unit.
amixer scontrols 2>/dev/null | sed -e "s/^Simple mixer control //" -e "s/'//g" |
while IFS= read -r ctl; do
    name=$(printf '%s' "$ctl" | sed -e 's/,[0-9]*$//')
    [ -n "$name" ] || continue
    amixer -q sset "$name" unmute >/dev/null 2>&1 || true
    amixer -q sset "$name" 100%   >/dev/null 2>&1 || true
    amixer -q sset "$name" on     >/dev/null 2>&1 || true
done

# 4) Persist so the state survives reboots (and a later alsa-restore).
alsactl store >/dev/null 2>&1 || true
exit 0
