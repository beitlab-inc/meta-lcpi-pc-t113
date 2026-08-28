# pingpong — framebuffer Pong for the LCPI-PC-T113 LCD

A tiny, dependency-free Pong for the LCD. It draws to `/dev/fb0`, puts
`/dev/tty1` into graphics mode, and takes keys from `game ctl` (SSH or
serial) and from a USB keyboard if one is plugged in.

It does **not** start at boot. Launch it from a login shell (SSH or serial):

```bash
game start pingpong    # background; returns immediately
game ctl               # this terminal is the keyboard; look at the LCD
game stop              # kill it and restore the LCD console
game status
```

`Ctrl-]` in `game ctl` detaches (game keeps running). Only one game may
run at a time. A second `game start` is refused until `game stop`.

Do **not** let the game raw `/dev/ttyS0` — that freezes the serial login.
`game ctl` puts only *this* tty in raw mode and restores it on detach.

## Controls (`game ctl` or USB keyboard)

- Left paddle : `w` / `s`  (or Up / Down)
- Right paddle: `i` / `k`  (using these disables the AI)
- Serve       : space
- Quit        : `q`

Prefer SSH (OTG `192.168.20.2`) for `game ctl` so the PB6/PB7 debug UART
stays a normal login.

## Change the build-time default

`pingpong_1.0.bb`:

```
SYSTEMD_AUTO_ENABLE = "disable"
```

Set to `"enable"` only if you want the board to boot straight into Pong.
