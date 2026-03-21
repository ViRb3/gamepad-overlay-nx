# Gamepad Overlay

A Nintendo Switch Tesla overlay that shows a compact live preview gamepad inputs.

## Features

- Live controller preview
- Compact corner HUD designed to stay open over gameplay
- Purple pressed-state highlight by default
- Customizable corner, scale, highlight color, and backdrop alpha

## Requirements

- [devkitPro](https://devkitpro.org/) with `devkitA64` and `libnx`
- [nx-ovlloader](https://github.com/WerWolv/nx-ovlloader)
- [Tesla-Menu](https://github.com/WerWolv/Tesla-Menu)

## Build

```bash
export DEVKITPRO=/opt/devkitpro
export PATH=$DEVKITPRO/devkitA64/bin:$PATH
make
```

## Install

Copy `gamepadoverlay.ovl` to `/switch/.overlays/` on the SD card.
