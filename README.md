# NUCLEO-F767ZI + Zephyr

Board docs: <https://docs.zephyrproject.org/latest/boards/st/nucleo_f767zi/doc/index.html>

Setup Guide: <https://docs.zephyrproject.org/latest/develop/getting_started/index.html>

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

### Dependencies

**Windows**: Use [winget](https://aka.ms/getwinget)
then run this in ps or cmd:

```bash
winget install Kitware.CMake Ninja-build.Ninja oss-winget.gperf Python.Python.3.12 Git.Git oss-winget.dtc wget 7zip.7zip
```

>You may need to add the 7zip installation folder to your PATH.

**Ubuntu**: Use apt:

```bash
sudo apt install --no-install-recommends git cmake ninja-build gperf \
  ccache dfu-util device-tree-compiler wget python3-dev python3-venv python3-tk \
  xz-utils file make gcc gcc-multilib g++-multilib libsdl2-dev libmagic1
```

verify:

```bash
cmake --version
python3 --version
dtc --version
```

**MacOS**: Use [Homebrew](https://brew.sh/)

```bash
brew install cmake ninja gperf python3 python-tk ccache qemu dtc libmagic wget openocd
```

then set Homebrew Python folder to the path:

```bash
(echo; echo 'export PATH="'$(brew --prefix)'/opt/python/libexec/bin:$PATH"') >> ~/.zprofile
source ~/.zprofile
```

### Get Zephyr and install Python dependencies

Using `pip` on *Linux* or *MacOS*:

```bash
python3 -m venv ~/zephyrproject/.venv
source ~/zephyrproject/.venv/bin/activate
pip install west
west init ~/zephyrproject
cd ~/zephyrproject
west update
west zephyr-export
west packages pip --install
cd ~/zephyrproject/zephyr
west sdk install
```

Using `uv`:

```bash
cd ~/zephyrproject
uv venv --python 3.11
source .venv/bin/activate
uv pip install west
west init ~/zephyrproject
west update
west zephyr-export
west packages pip --install
cd ~/zephyrproject/zephyr
west sdk install
```

in Powershell on *Windows*:

```bash
cd $Env:HOMEPATH
python -m venv zephyrproject\.venv
zephyrproject\.venv\Scripts\Activate.ps1
pip install west
west init zephyrproject
cd zephyrproject
west update
west zephyr-export
west packages pip --install
cd $Env:HOMEPATH\zephyrproject\zephyr
west sdk install
```

## Build & flash

Run `build.sh` or inside of `/K2-Zephyr` run:

```bash
west build -b nucleo_f767zi
west flash
```

Optionally on WSL or if STM32CubeProgrammer not installed:

```bash
west flash -d build/app --runner openocd
```

Monitor serial (115200 baud):

```bash
minicom -D /dev/ttyACM0 -b 115200
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
                "${workspaceFolder}/**",
                "/Users/USERNAME/zephyrproject/zephyr/include",
                "/Users/USERNAME/zephyrproject/zephyr/include/zephyr",
                "/Users/USERNAME/zephyrproject/zephyr/lib/libc/common/include",
                "/Users/USERNAME/zephyrproject/zephyr/lib/libc/minimal/include",
                "${workspaceFolder}/build/zephyr/include/generated",
                "/Users/USERNAME/zephyr-sdk-VERSION/arm-zephyr-eabi/arm-zephyr-eabi/include"
            ],
            "defines": [
                "CONFIG_BOARD_NUCLEO_F767ZI",
                "__ZEPHYR__=1"
            ],
            "compilerPath": "/Users/USERNAME/zephyr-sdk-VERSION/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc",
            "cStandard": "c11",
            "cppStandard": "c++17",
            "intelliSenseMode": "gcc-arm"
        }
    ],
    "version": 4
}
```
