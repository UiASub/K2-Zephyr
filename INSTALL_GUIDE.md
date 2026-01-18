# Installation Guides

In-depht installation guide: [docs.zephyrproject](https://docs.zephyrproject.org/4.2.0/develop/getting_started/index.htm)

Board docs: [nucleo_f767zi](https://docs.zephyrproject.org/4.2.0/boards/st/nucleo_f767zi/doc/index.html)

If you have [STM32CubeProgrammer PATH problems](#stm32cubeprogrammer-path-if-installed-from-st-website), you can use [OpenOCD](#openocd) instead.

## Quick Installation

### Linux and MacOS in terminal

Install script using `curl`

```bash
curl -O https://raw.githubusercontent.com/UiASub/K2-Zephyr/main/install_zephyr.sh
chmod +x install_zephyr.sh
./install_zephyr.sh
```

use `./install_zephyr.sh -h` to see help

### Windows in CMD or Powershell (Admin)

Install script using `powershell`

You will need [winget](https://aka.ms/getwinget) to install dependencies.

```bash
powershell -NoProfile -ExecutionPolicy Bypass -Command "Invoke-WebRequest https://raw.githubusercontent.com/UiASub/K2-Zephyr/main/install_zephyr.ps1 -OutFile install_zephyr.ps1"
# Use script:
powershell -ExecutionPolicy Bypass -File install_zephyr.ps1
```

Default install path is ***$HOME\zephyrproject***
> Edit default *Zephyr Path* with `-ZephyrPath`
Example:

```powershell
# Use script:
powershell -ExecutionPolicy Bypass -File install_zephyr.ps1 -ZephyrPath "C:\Users\you\zephyrproject"
```

## Manual Installation

**[Windows](#windows)** |
**[Ubuntu](#ubuntu)** |
**[MacOS](#macos)**

---

### Windows

**Windows**: Use [winget](https://aka.ms/getwinget)
then run this in ps or cmd:

```bash
winget install Kitware.CMake Ninja-build.Ninja oss-winget.gperf Python.Python.3.12 Git.Git oss-winget.dtc wget 7zip.7zip STMicroelectronics.STM32CubeProgrammer
```

>You may need to add the 7zip and STM32CubeProgrammer installation folders to your PATH.

#### Get Zephyr and install Python dependencies in Windows

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
west sdk install --version 0.17.2
```

### Ubuntu

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

#### Get Zephyr and install Python dependencies in Ubuntu

Using `pip` on *Linux*:

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
west sdk install --version 0.17.2
```

Or using `uv`:

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
west sdk install --version 0.17.2
```

### MacOS

**MacOS**: Use [Homebrew](https://brew.sh/)

```bash
brew install cmake ninja gperf python@3.12 ccache qemu dtc libmagic wget
brew install --cask stm32cubeprogrammer
```

#### Get Zephyr and install Python dependencies in Mac

Using `pip` on *MacOS*:

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
west sdk install --version 0.17.2
```

Or using `uv`:

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
west sdk install --version 0.17.2
```

## STM32CubeProgrammer PATH (if installed from ST website)

If you install STM32CubeProgrammer from [ST's website](https://www.st.com/en/development-tools/stm32cubeprog.html), the installer may not add the CLI tools to your PATH.

If `west flash` doesn't find STM32CubeProgrammer, you can add it manually.

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

## OpenOCD

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
