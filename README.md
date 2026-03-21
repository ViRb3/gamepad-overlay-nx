# Gamepad Overlay

A Nintendo Switch Tesla overlay that shows a compact live preview gamepad inputs.

## Features

- Live controller preview
- Compact corner HUD designed to stay open over gameplay
- Purple pressed-state highlight by default
- Customizable corner, scale, highlight color, and backdrop alpha

![2026032120374100-899BF99A6A4D5CD93E86FC3EDF92F308](https://github.com/user-attachments/assets/95356a4a-edaf-4da5-a3b0-40ced10bcd0c)
![2026032120375000-899BF99A6A4D5CD93E86FC3EDF92F308](https://github.com/user-attachments/assets/23e0ccd4-bbe3-4f1e-a5cb-659fa37987f4)

## Requirements

- [devkitPro](https://devkitpro.org/)
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
