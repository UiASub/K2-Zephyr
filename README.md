# K2 Minimal ESC Debug Firmware

This branch intentionally builds a stripped Zephyr app for debugging ESC UART
communication. It does not start Ethernet, IMU, PID, thruster mapping, OTA, or
OLED code.

The firmware:

- configures the `vesc-uart` device at 115200 8N1
- sends VESC `COMM_GET_VALUES` once per second
- sends a zero-duty `COMM_SET_DUTY` frame every fifth query
- logs every TX and RX byte sequence as a hex dump on the console

## Building

```bash
./build.sh
./build.sh --f7
./build.sh --h7
./build.sh --esp32-supermini
```

On Windows:

```powershell
.\build.ps1
.\build.ps1 --F7
.\build.ps1 --H7
.\build.ps1 --esp32-supermini
```

The default target is F7 (`nucleo_f767zi`).

## UART Wiring

F7 and H7 use the existing `vesc-uart` alias on USART6:

- TX: PC6 / D1
- RX: PC7 / D0

ESP32-C3 SuperMini uses UART1 while keeping logs on USB serial:

- TX: GPIO21
- RX: GPIO20
