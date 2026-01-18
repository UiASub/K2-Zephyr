# K2-Zephyr

Board docs: <https://docs.zephyrproject.org/4.2.0/boards/st/nucleo_f767zi/doc/index.html>

- **west**: 1.5.0
- **Zephyr SDK**: 0.17.2
- **Zephyr project**: v4.2.0
- **Python**: 3.12

## Installation guide

See [Install Guide](INSTALL_GUIDE.md) for installation guide.
In-depht guide: <https://docs.zephyrproject.org/4.2.0/develop/getting_started/index.html>

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
```

or manually inside PowerShell

```powershell
Set-Location "$HOME\zephyrproject\K2-Zephyr"
# activate venv if needed
. .\.venv\Scripts\Activate.ps1
west build -b nucleo_f767zi
west flash
```

If you prefer a Unix shell on Windows, run `build.sh` from WSL, Git Bash, or MSYS2.

> **Note**: `west flash` uses STM32CubeProgrammer by default when it is available on your PATH (it's not used unless installed and found). Linux users can install it manually or use OpenOCD fallback.

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
