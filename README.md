# NUCLEO-F767ZI + Zephyr

Board docs: <https://docs.zephyrproject.org/latest/boards/st/nucleo_f767zi/doc/index.html>

Setup Guide: <https://docs.zephyrproject.org/latest/develop/getting_started/index.html>

**IDE Setup:** [VS Code](#vscode-config) | [CLion](#clion-configuration)

- **west**: 1.5.0
- **Zephyr**: v4
- **Zephyr SDK**: 0.17.4
- **Python**: 3.12

## Quick Installation

Install script using `curl`

### Linux and MacOS in terminal

```bash
curl -O https://raw.githubusercontent.com/UiASub/K2-Zephyr/main/install_zephyr.sh
chmod +x install_zephyr.sh
./install_zephyr.sh
```

use `./install_zephyr.sh -h` to see help

### Windows in cmd or PowerShell

```bash
curl -O https://raw.githubusercontent.com/UiASub/K2-Zephyr/main/install_zephyr.ps1
powershell -ExecutionPolicy Bypass -File install_zephyr.ps1
```

You need [winget](https://aka.ms/getwinget) to install dependencies.

## Manual Installation

- **west**: 1.5.0
- **Zephyr SDK**: 0.17.4
- **Python**: 3.12

### Dependencies

**Windows**: Use [winget](https://aka.ms/getwinget)
then run this in ps or cmd:

```bash
winget install Kitware.CMake Ninja-build.Ninja oss-winget.gperf Python.Python.3.12 Git.Git oss-winget.dtc wget 7zip.7zip STMicroelectronics.STM32CubeProgrammer
```

>You may need to add the 7zip and STM32CubeProgrammer installation folders to your PATH.

**Ubuntu**: Use apt:

```bash
sudo apt install --no-install-recommends git cmake ninja-build gperf \
  ccache dfu-util device-tree-compiler wget python3.12 python3.12-dev python3.12-venv python3-tk \
  xz-utils file make gcc gcc-multilib g++-multilib libsdl2-dev libmagic1 openocd
```

> **Note**: Linux users can install STM32CubeProgrammer manually from [STMicroelectronics](https://www.st.com/en/development-tools/stm32cubeprog.html) for faster flashing. OpenOCD (installed above) works as an alternative.

verify:

```bash
cmake --version
python3 --version
dtc --version
```

**MacOS**: Use [Homebrew](https://brew.sh/)

```bash
brew install cmake ninja gperf python@3.12 ccache qemu dtc libmagic wget
brew install --cask stm32cubeprogrammer
```

### Get Zephyr and install Python dependencies

Using `pip` on *Linux* or *MacOS*:

```bash
python3.12 -m venv ~/zephyrproject/.venv
source ~/zephyrproject/.venv/bin/activate
pip install west==1.5.0
west init ~/zephyrproject
cd ~/zephyrproject
west update
west zephyr-export
west packages pip --install
cd ~/zephyrproject/zephyr
west sdk install --version 0.17.4
```

Using `uv`:

```bash
cd ~/zephyrproject
uv venv --python 3.12
source .venv/bin/activate
uv pip install west==1.5.0
west init ~/zephyrproject
west update
west zephyr-export
west packages pip --install
cd ~/zephyrproject/zephyr
west sdk install --version 0.17.4
```

in Powershell on *Windows*:

```bash
cd $Env:HOMEPATH
python -m venv zephyrproject\.venv
zephyrproject\.venv\Scripts\Activate.ps1
pip install west==1.5.0
west init zephyrproject
cd zephyrproject
west update
west zephyr-export
west packages pip --install
cd $Env:HOMEPATH\zephyrproject\zephyr
west sdk install --version 0.17.4
```

## Build & flash

Run the build helper for your platform or run `west` directly from the project folder.

- macOS / Linux / WSL / Git Bash:

```bash
./build.sh
# or manually inside the repo
cd ~/zephyrproject/K2-Zephyr
west build -b nucleo_f767zi
west flash
```

- Windows (PowerShell):

Use the included `build.ps1` which performs the equivalent steps in PowerShell. Run from the repository root in PowerShell:

```powershell
./build.ps1
# or manually inside PowerShell
Set-Location "$HOME\zephyrproject\K2-Zephyr"
# activate venv if needed
. .\.venv\Scripts\Activate.ps1
west build -b nucleo_f767zi
west flash
```

If you prefer a Unix shell on Windows, run `build.sh` from WSL, Git Bash, or MSYS2.

> **Note**: `west flash` uses STM32CubeProgrammer by default when it is available on your PATH (it's not used unless installed and found). Linux users can install it manually or use OpenOCD fallback.

### STM32CubeProgrammer PATH (if installed from ST website)

If you install STM32CubeProgrammer from ST's website (<https://www.st.com/en/development-tools/stm32cubeprog.html>) the installer may not add the CLI tools to your PATH. If `west flash` doesn't find STM32CubeProgrammer, you can add it manually.

- macOS (example):

1. Find the installed CLI binary (common name: `STM32_Programmer_CLI`).

```bash
which STM32_Programmer_CLI || find / -name STM32_Programmer_CLI -type f 2>/dev/null
```

1. Add its directory to your shell profile (example path below, adjust to match the location you discovered):

```bash
echo 'export PATH="/Applications/STMicroelectronics/STM32Cube/STM32CubeProgrammer/STM32CubeProgrammer.app/Contents/Resources/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

- Linux (example):

1. Find the installed CLI binary (common name: `STM32_Programmer_CLI`).

```bash
which STM32_Programmer_CLI || find / -name STM32_Programmer_CLI -type f 2>/dev/null
```

1. Add its directory to your shell profile (example path below, adjust to match the location you discovered):

```bash
echo 'export PATH="/opt/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

- Windows (example — use the exact install path you have; PowerShell example):

Open System Properties -> Environment Variables and add the STM32CubeProgrammer `bin` directory to your PATH, or use PowerShell:

```powershell
setx PATH "$env:PATH;C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin"
```

After adding the path, re-open your shell (or sign out & sign back in on Windows) so `west flash` can detect the STM32CubeProgrammer CLI.

To explicitly use OpenOCD:

```bash
west flash --runner openocd
```

Monitor serial (115200 baud):

```bash
# Linux
minicom -D /dev/ttyACM0 -b 115200

# macOS
minicom -D /dev/tty.usbmodem* -b 115200
# or use screen
screen /dev/tty.usbmodem* 115200
```

## vscode config

> This is so no errors appear in c library for zephyr

In `.vscode` folder, add this and customize to your need

`c_cpp_properties.json`

```json
{
    "configurations": [
        {
            "name": "Zephyr ARM",
            "includePath": [
                "${workspaceFolder}/build/zephyr/include/generated",
                "${workspaceFolder}/**",
                "~/zephyrproject/zephyr/include",
                "~/zephyrproject/zephyr/include/zephyr",
                "~/zephyrproject/zephyr/lib/libc/common/include",
                "~/zephyrproject/zephyr/lib/libc/minimal/include",
                "~/zephyr-sdk-VERSION/arm-zephyr-eabi/arm-zephyr-eabi/include",
                "~/zephyrproject/zephyr/soc/st/stm32/stm32f7x",
                "~/zephyrproject/zephyr/soc/st/stm32/common",
                "~/zephyrproject/zephyr/modules/cmsis",
                "~/zephyrproject/zephyr/modules/cmsis_6",
                "~/zephyrproject/modules/hal/stm32/stm32cube/stm32f7xx/soc",
                "~/zephyrproject/modules/hal/stm32/stm32cube/stm32f7xx/drivers/include",
                "~/zephyrproject/modules/hal/stm32/stm32cube/common_ll/include"
            ],
            "forcedInclude": [
                "${workspaceFolder}/build/zephyr/include/generated/zephyr/autoconf.h"
            ],
            "defines": [
                "CONFIG_BOARD_NUCLEO_F767ZI",
                "__ZEPHYR__=1",
                "CONFIG_ARM=1",
                "CONFIG_CPU_CORTEX_M7=1",
                "CONFIG_SOC_STM32F767XX=1",
                "CONFIG_SYS_CLOCK_TICKS_PER_SEC=10000"
            ],
            "compilerPath": "~/zephyr-sdk-VERSION/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc",
            "cStandard": "c11",
            "cppStandard": "c++17",
            "intelliSenseMode": "gcc-arm"
        }
    ],
    "version": 4
}
```

## CLion Configuration

CLion doesn't have native support for `west`-based Zephyr projects. You have two options:

### Option 1: Use Terminal within CLion

The simplest approach is to use CLion's built-in terminal with the virtual environment activated:

1. Open the project in CLion
2. Open the terminal (View → Tool Windows → Terminal)
3. Activate the virtual environment:

   ```bash
   source ~/zephyrproject/.venv/bin/activate  # macOS/Linux
   # or
   . ~\zephyrproject\.venv\Scripts\Activate.ps1  # Windows PowerShell
   ```

4. Build and flash using `west` commands:

   ```bash
   west build -b nucleo_f767zi
   west flash
   ```

### Option 2: Open as Makefile Project

Alternatively, you can configure CLion to recognize it as a Makefile project and create custom build configurations:

1. Open **Run → Edit Configurations**
2. Click **+** → **Makefile Application**
3. Set **Executable**: `west`
4. Set **Program arguments**: `build -b nucleo_f767zi`
5. Set **Working directory**: `/Users/USERNAME/zephyrproject/K2-Zephyr`
6. Set **Environment variables**: `PATH=/Users/USERNAME/zephyrproject/.venv/bin:$PATH`

Note: CLion is primarily designed for CMake-based projects. For full Zephyr integration, VS Code with the Zephyr extension may provide a better experience.
