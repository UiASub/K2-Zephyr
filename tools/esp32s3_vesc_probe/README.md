# ESP32-S3 VESC UART probe

Standalone Zephyr probe for the ESP32-S3 DevKitC-style board currently enumerating as an ESP32-S3 QFN56 with 8 MB flash.

## Wiring

- ESP32-S3 `IO17` / `GPIO17` -> VESC RX
- ESP32-S3 `IO18` / `GPIO18` -> VESC TX
- ESP32-S3 `GND` -> VESC GND

Do not use the board `TX`/`RX` pins for this probe. Zephyr's `esp32s3_devkitc` board keeps the console and flashing UART on `GPIO43`/`GPIO44`; this app uses UART1 on `GPIO17`/`GPIO18` for the VESC.

## Build and flash

```sh
PATH=/home/dl/.local/share/pipx/venvs/west/bin:$PATH \
west build --sysbuild -p always -d build-esp32s3-vesc-probe-sys \
  -b esp32s3_devkitc/esp32s3/procpu tools/esp32s3_vesc_probe

PATH=/home/dl/.local/share/pipx/venvs/west/bin:$PATH \
west flash -d build-esp32s3-vesc-probe-sys --esp-device /dev/ttyACM1
```

Logs are on the ESP32-S3 USB serial device at 115200 baud.

## Behavior

The probe scans common VESC UART baud rates with `COMM_FW_VERSION`. After a valid VESC packet is received, it sends repeated 10% duty pulses for 700 ms followed by stop packets for 1500 ms. If no valid packet is received, it restores 115200 baud and does not send duty commands.
