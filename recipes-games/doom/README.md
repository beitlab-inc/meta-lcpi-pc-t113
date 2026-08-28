# doom — framebuffer Doom for the LCPI-PC-T113 LCD

No USB keyboard needed. From SSH (OTG) or serial:

```bash
game start doom        # LCD switches to Doom; this shell stays usable
game ctl               # type here; look at the panel. Ctrl-] detaches.
```

Do **not** press Ctrl-C in the shell that only ran `game start` — that
does not stop the game. To quit:

```bash
game stop              # kills Doom and brings the LCD login back
```

## Controls (`game ctl`)

| Key | Action |
|-----|--------|
| W A S D or arrows | move / turn |
| j / f / k | fire |
| Space | use / open doors |
| Enter | menu select / start |
| Esc | menu |
| 1–9 | weapons |
| Ctrl-] | detach this terminal (Doom keeps running) |

At the Freedoom title screen: **Enter** to start, arrows to move in menus.

A USB keyboard on USB-A still works if you have one (Ctrl = fire, F10 quits).

## If the LCD is stuck (old image)

From the serial/SSH shell:

```bash
game stop
# or:
systemctl stop doom pingpong
game --restore-console
```
