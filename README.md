# NUCLEO-H755ZI-Q + Zephyr

Default board docs: <https://docs.zephyrproject.org/4.2.0/boards/st/nucleo_h755zi_q/doc/index.html>

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

## Building

Default target is H7 (`nucleo_h755zi_q/stm32h755xx/m7`).

```bash
./build.sh
./build.sh --h7
./build.sh --f7
```

On Windows:

```powershell
.\build.ps1
.\build.ps1 --H7
.\build.ps1 --F7
```

## Release

To create a new release, push a tag:

```bash
git tag v0.X.X
git push origin v0.X.X
```

This triggers the GitHub Actions workflow to build and release the firmware.
