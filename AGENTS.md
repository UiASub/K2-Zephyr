# K2-Zephyr Agent Context

This file is repo-local context for future Codex/agent sessions. Read it first
before changing, building, flashing, or reviewing this project.

## Project Identity

- Project: K2-Zephyr, firmware/operating software for the K2 underwater ROV.
- Hardware target: ST NUCLEO-H755ZI-Q, Cortex-M7 target
  `nucleo_h755zi_q/stm32h755xx/m7`.
- RTOS/tooling: Zephyr 4.2.0, Zephyr SDK 0.17.2. Python 3.12 is recommended.
- F7 support is sunset. Treat H755 M7 as the supported board unless the user
  explicitly asks about old F7 files.
- Codebase style: C/Zephyr application, English code comments, Norwegian user
  conversation is normal.

## Build And OTA

- Normal build path is OTA/sysbuild:
  - Windows: `.\build.ps1`
  - Linux/macOS: `./build.sh`
- The scripts default to MCUboot OTA output in `build-h755-ota`.
- Signed app image path:
  `build-h755-ota/K2-Zephyr/zephyr/zephyr.signed.bin`.
- First-time provisioning or recovery can use USB flashing:
  `west flash -d build-h755-ota`.
- Normal field updates should use Ethernet OTA helpers:
  - Windows: `.\tools\k2-ota.ps1`
  - Linux/macOS: `./tools/k2-ota.sh`
- MCUmgr UDP runs on port `1337` from `ota.conf`.
- Existing `build-h755` may be a non-OTA build directory. Do not assume it is
  the current desired output just because it exists.

## Important Top-Level Files

- `README.md`: setup, build, release, OTA overview.
- `ETHERNET_OTA.md`: direct Ethernet link setup and MCUmgr workflow.
- `PINOUT.md`: hardware pin reference.
- `CMakeLists.txt`: app source list and local DTS root.
- `prj.conf`: Zephyr app configuration.
- `ota.conf`: extra config for MCUmgr/OTA.
- `sysbuild.conf`: enables MCUboot sysbuild.
- `boards/nucleo_h755zi_q_stm32h755xx_m7.overlay`: active H7 hardware overlay.
- `boards/nucleo_h755zi_q_stm32h755xx_m7_partitions.overlay`: MCUboot slots.

## Runtime Startup Flow

Entry point: `src/main.c`.

Startup order:
1. `rov_control_init()`
2. `network_init()`
3. UDP log backend activation
4. `rov_control_start()`
5. `resource_monitor_start()`
6. `display_start()` (stubbed unless `CONFIG_K2_OLED=y`)
7. `sensor_sender_start()`
8. `udp_server_start()`
9. `pid_config_start()`
10. `axis_config_start()`
11. `control_telemetry_start()`
12. `setpoint_override_start()`
13. `system_control_start()`
14. `ota_confirm_init()`

The main loop mostly monitors network readiness. Real work happens in Zephyr
threads and callbacks.

## Hardware Mapping

Defined mainly in `boards/nucleo_h755zi_q_stm32h755xx_m7.overlay` and
documented in `PINOUT.md`.

- Ethernet: static IPv4 `10.77.0.2/24`.
- Topside/broadcast destination: `10.77.0.255`.
- VESC UART: `LPUART1`, TX PB6 / Arduino D1, RX PB7 / Arduino D0,
  115200 baud. This mapping is based on the working
  `tests/vesc_uart_duty` hardware test.
- VN-100S IMU: `SPI3`, SCK PC10, MISO PC11, MOSI PC12, CS PA4.
- Optional SSD1306 OLED: `I2C4`, SCL PD12, SDA PD13. Disabled by default with
  `CONFIG_K2_OLED=n`; `src/display/oled.h` provides stubs.
- ROV light PWM: `TIM1_CH1` on PA8/D6, exposed as devicetree node `rov_light`.

## Network Contract

Network constants live in `src/net/net.h`.

- MCU IP: `10.77.0.2`
- Topside/broadcast IP: `10.77.0.255`
- Command UDP: `12345`
- Resource telemetry UDP: `12346`
- IMU JSON UDP: `5002`
- PID config UDP: `5003`
- Axis config UDP: `5004`
- Control telemetry UDP: `5005`
- UDP log backend: `5006`
- Setpoint override UDP: `5007`
- System control/reset UDP: `5008`
- MCUmgr OTA UDP: `1337`

Packet integrity mostly uses the shared IEEE CRC32 helper in `src/net/net.c`.
Some packets store integer fields in network byte order, but several float
payloads are native little-endian by design. Check each packet struct/header
before changing topside compatibility.

## Control Architecture

Core control is in `src/control.c`.

- Control loop period: `20 ms` / `50 Hz`.
- Communication timeout: `2000 ms`; on timeout, all thruster outputs are killed.
- Incoming pilot command payload is unpacked into:
  `surge`, `sway`, `heave`, `roll`, `pitch`, `yaw`, `light`, `manipulator`.
- PID axes: surge, sway, heave, roll, pitch, yaw.
- PID gains start at zero. A disabled PID axis runs passthrough from stick
  input and resets PID state.
- Roll, pitch, yaw are angle-tracking PIDs using VN-100S YPR.
- Surge and sway estimate speed by leaky-integrating acceleration.
- Heave has a depth-control stub until a real depth sensor is integrated.
- Setpoint override on UDP `5007` can force per-axis setpoints for testing.
- Control telemetry on UDP `5005` sends setpoint, output, and error arrays.

## Thrusters And VESC

- 6DOF-to-8-thruster mixing is in `src/vesc/thruster_mapping.c`.
- Thruster order / IDs:
  - `0` TLF, local UART
  - `1` TLB, CAN
  - `2` BLB, CAN
  - `3` BLF, CAN
  - `4` BRF, CAN
  - `5` BRB, CAN
  - `6` TRB, CAN
  - `7` TRF, CAN
- `MAX_DUTY` is currently `0.5f` for safety/testing.
- `src/vesc/vesc_protocol.c` builds VESC binary packets:
  - `COMM_SET_DUTY`
  - `COMM_CAN_FORWARD`
- `src/vesc/vesc_uart_zephyr.c` uses polling UART TX, matching the
  `tests/vesc_uart_duty` path that verified VESC communication on PB6/PB7.

## IMU And Axis Configuration

- VN-100S driver/task: `src/imu/vn100s.c`.
- The IMU task is started with `K_THREAD_DEFINE`, so it runs independently of
  `main()` startup.
- It reads VN register `239` for YPR, angular rates, and linear acceleration.
- It rejects NaN/Inf samples and reinitializes if data is stale.
- Axis remapping and IMU offset compensation are configured over UDP `5004` in
  `src/imu/axis_config.c`.
- Devicetree binding is local:
  `dts/bindings/sensor/vectornav,vn100s.yaml`.

## Diagnostics

- Resource monitor: `src/net/resource_monitor.c`, sends CPU, stack/RAM proxy,
  thread count, and UDP counters on port `12346`.
- UDP log backend: `src/net/log_backend_udp.c`, sends formatted Zephyr logs to
  port `5006`. Do not add `LOG_*` calls inside that file because it would
  recurse through the logging backend.
- System reset command: `src/net/system_control.c`, listens on port `5008` for
  `RST1` packets with CRC.

## Safety And Editing Notes

- Treat thruster behavior as safety-sensitive. Preserve comms timeout behavior
  unless the user explicitly asks to change it.
- Be careful with packet layouts marked `__attribute__((packed))`; topside code
  likely depends on exact byte layout.
- Be careful with byte order. Do not "clean up" native float packet formats
  without coordinating topside changes.
- Do not raise `MAX_DUTY`, remove CRC checks, or bypass timeout logic as an
  incidental refactor.
- Prefer small, focused edits that follow existing Zephyr patterns.
- If changing devicetree, verify aliases used by code:
  `vesc-uart`, `vn100s`, `imu-oled`, and node label `rov_light`.
- Local untracked/editor files may exist. Do not revert or remove user-owned
  changes unless explicitly requested.

## First Files To Read For Common Tasks

- Understand startup: `src/main.c`.
- Change control behavior: `src/control.c`, `src/pid/*`, `src/imu/axis_config.*`.
- Change command/telemetry protocol: `src/net/net.h`, `src/net/net.c`,
  `src/net/control_telemetry.*`, `src/net/resource_monitor.*`.
- Change thruster mapping: `src/vesc/thruster_mapping.*`.
- Change VESC transport/protocol: `src/vesc/vesc_uart_zephyr.*`,
  `src/vesc/vesc_protocol.*`.
- Change hardware pins/peripherals:
  `boards/nucleo_h755zi_q_stm32h755xx_m7.overlay` and `PINOUT.md`.
- Change OTA/build flow: `build.ps1`, `build.sh`, `ota.conf`,
  `sysbuild.conf`, `ETHERNET_OTA.md`.
