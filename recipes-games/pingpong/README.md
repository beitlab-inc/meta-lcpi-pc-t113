# pingpong — UART-controlled framebuffer Pong

A tiny, dependency-free Pong for the LCPI-PC-T113 LCD. It draws directly to
`/dev/fb0` (no X11/Wayland/GPU), puts the LCD's virtual terminal (`/dev/tty1`)
into graphics mode, and reads its controls from the board UART (`/dev/ttyS0`).

The systemd unit runs the game as a boot-time kiosk. It `Conflicts=` the LCD
login (`getty@tty1.service`) and the serial login (`serial-getty@ttyS0.service`)
so it can take over the panel and the UART.

## Enable / disable the service at runtime (on the board)

```bash
# Start it now (game takes over the LCD + UART immediately)
systemctl start pingpong

# Stop it now (releases /dev/fb0, /dev/tty1 and /dev/ttyS0)
systemctl stop pingpong

# Boot straight into the game on every boot
systemctl enable pingpong

# Don't autostart at boot (fall back to the console/login)
systemctl disable pingpong

# One-shot toggles without changing the boot default
systemctl restart pingpong
systemctl status pingpong
```

Because `pingpong.service` has
`Conflicts=getty@tty1.service serial-getty@ttyS0.service`:

- `systemctl start pingpong` automatically **stops** the LCD login (`tty1`) and
  the serial login (`ttyS0`).
- `systemctl stop pingpong` does **not** auto-restart those gettys. To get your
  serial console / login back:

```bash
systemctl stop pingpong
systemctl start serial-getty@ttyS0.service   # serial login back
systemctl start getty@tty1.service           # LCD console login back
```

## Change the build-time default (in the recipe)

The default is baked in via `pingpong_1.0.bb`:

```
SYSTEMD_SERVICE_${PN} = "pingpong.service"
SYSTEMD_AUTO_ENABLE = "enable"
```

- Keep `"enable"` → board boots into the game (kiosk mode).
- Change to `"disable"` → board boots to the normal console/login, and you start
  the game on demand with `systemctl start pingpong`.

After editing, rebuild and reflash:

```bash
bitbake pingpong                  # just this recipe
bitbake lcpi-pc-t113-image        # the full image
```

## Using the UART

The game uses `/dev/ttyS0` — the same UART as the serial console — for controls
(see `ExecStart=/usr/bin/pingpong -i /dev/ttyS0 ...` in `pingpong.service`).

1. **Free the UART from the login getty.** Starting `pingpong` via systemd does
   this automatically (`Conflicts=serial-getty@ttyS0.service`). If you run the
   binary manually, stop the getty first so it doesn't fight over the port:

```bash
systemctl stop serial-getty@ttyS0.service
/usr/bin/pingpong -i /dev/ttyS0 -f /dev/fb0 -t /dev/tty1
```

2. **Send controls over that UART** from your host (match the console baud,
   typically 115200 for the T113):

```bash
picocom -b 115200 /dev/ttyUSB0    # then press the control keys
# or a quick one-off:
stty -F /dev/ttyUSB0 115200 && printf 'w' > /dev/ttyUSB0
```

> To use a **different/second** UART instead of the console one, enable that
> `serialN`/`uartN` node in the device tree and point the service's `-i` flag at
> the resulting `/dev/ttySx`.
