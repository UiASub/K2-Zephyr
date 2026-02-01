# NUCLEO-F767ZI + Zephyr

Board docs: <https://docs.zephyrproject.org/4.2.0/boards/st/nucleo_f767zi/doc/index.html>

UiASub Setup Guide: <https://wiki.uiasub.no/k2zephyr/>

Official Setup Guide: <https://docs.zephyrproject.org/4.2.0/develop/getting_started/index.html>

## Required versions

- **Zephyr project**: v4.2.0
- **Zephyr SDK**: 0.17.2

### Recommended version

- **Python**: 3.11

## Quick Installation

See [intsall guide](https://wiki.uiasub.no/k2zephyr/INSTALL_GUIDE/) for STM32CubeProgrammer (if installed, continue)

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

## Release

To create a new release, push a tag:

```bash
git tag v0.X.X
git push origin v0.X.X
```

This triggers the GitHub Actions workflow to build and release the firmware.
