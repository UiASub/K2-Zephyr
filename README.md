# NUCLEO-F767ZI + Zephyr

Board docs: <https://docs.zephyrproject.org/latest/boards/st/nucleo_f767zi/doc/index.html>

UiASub Setup Guide: <https://wiki.uiasub.no/k2zephyr/>

Official Setup Guide: <https://docs.zephyrproject.org/latest/develop/getting_started/index.html>

## Required versions

- **Zephyr project**: v4.2.0
- **Zephyr SDK**: 0.17.2

### Recommended version

- **Python**: 3.11

## Quick Installation

### Windows in PowerShell (Admin)

```bash
Invoke-WebRequest https://raw.githubusercontent.com/UiASub/K2-Zephyr/main/install_zephyr.ps1 -OutFile install_zephyr.ps1
# Use script:
Set-ExecutionPolicy -ExecutionPolicy Bypass -File install_zephyr.ps1
```

You need [winget](https://aka.ms/getwinget) to install dependencies.

### Linux and MacOS in terminal

Install script using `curl`

```bash
curl -O https://raw.githubusercontent.com/UiASub/K2-Zephyr/main/install_zephyr.sh
chmod +x install_zephyr.sh
./install_zephyr.sh
```

use `./install_zephyr.sh -h` to see help
