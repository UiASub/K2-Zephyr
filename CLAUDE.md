# K2-Zephyr — Project Context for Claude

> Read this first. It captures the load-bearing facts about this project so
> Claude doesn't have to re-discover them every conversation. Keep it
> short — when something here goes stale, update it (or ask the
> `context-keeper` agent to).

## What this is

ROV (remotely operated underwater vehicle) firmware running on Zephyr RTOS,
targeting an STM32 Nucleo-144 dev board. Receives 6-DOF stick commands over
UDP from a topside computer, runs PID control, and drives 8 thrusters via
VESC motor controllers (1 over UART, 7 over CAN).

- **Org**: UiASub (University of Agder submarine team)
- **Primary board**: `nucleo_h755zi_q/stm32h755xx/m7` (dual-core H7, Cortex-M7 only) — actual deploy target
- **Secondary board**: `nucleo_f767zi`
- **Zephyr**: v4.2.0  |  **SDK**: 0.17.4 (at `~/zephyr-sdk-0.17.4`)  |  **Python**: 3.12 recommended (3.13 works)

## Build & flash

Workspace lives at `~/zephyrproject/` (a west workspace). This repo is
`~/zephyrproject/K2-Zephyr`. The working venv lives at
`~/zephyrproject/zephyrproject/.venv` (nested path) — activate it
manually before building:

```
& "$env:USERPROFILE\zephyrproject\zephyrproject\.venv\Scripts\Activate.ps1"
```

Note: `build.ps1` / `build.sh` look at `~/zephyrproject/.venv` (non-nested,
doesn't exist) and fall back to global `west`. The global Python lacks
`pyelftools`, so the fallback path **will fail** at `gen_kobject_list.py`
— always activate the nested venv yourself.

The SDK is not registered with CMake's package registry; set
`$env:ZEPHYR_SDK_INSTALL_DIR = "$env:USERPROFILE\zephyr-sdk-0.17.4"`
before `west build` if CMake can't find it.

```
# Build (pristine, H755ZI-Q M7 core — default deploy target):
west build -p -b nucleo_h755zi_q/stm32h755xx/m7

# Build for F767ZI:
west build -p -b nucleo_f767zi

# Flash (default runner is OpenOCD, set in CMakeLists.txt):
west flash
```

Convenience wrappers: `build.ps1` (Windows) and `build.sh` (Linux/macOS).
Both default to H755 (`--h7`); pass `--f7` for the F767. Add `--ota` for
MCUboot + Ethernet OTA images.

Flash tool for STM32: STM32CubeProgrammer must be installed; OpenOCD ships
with the Zephyr SDK.

## Layout

```
src/
  main.c               # init order: control → net → log_udp → threads
  control.{c,h}        # ROV control loop, setpoint mux, telemetry snapshot
  imu/
    vn100s.{c,h}       # VectorNav VN-100S driver (SPI bit-bang, 1 MHz)
    axis_config.{c,h}  # Runtime axis remap / inversion (UDP-configurable)
  net/
    net.{c,h}          # UDP command server (stick input from topside)
    control_telemetry.{c,h}   # Sends control_telemetry_t back to topside
    setpoint_override.{c,h}   # Topside-driven setpoint injection for testing
    log_backend_udp.c  # Custom UDP log backend (LOG_* → topside)
    resource_monitor.{c,h}    # CPU/heap/stack telemetry
  pid/
    pid_controller.{c,h}      # Generic PID, derivative-on-measurement
    pid_config.{c,h}          # Runtime gain tuning over UDP
  vesc/
    vesc_protocol.{c,h}       # VESC binary protocol (frame + CRC)
    vesc_uart_zephyr.{c,h}    # UART transport (USART6, 115200)
    thruster_mapping.{c,h}    # 6-DOF → 8 thruster duty cycles

boards/                # Per-board DTS overlays
dts/bindings/sensor/   # VN-100S binding
prj.conf               # Kconfig — logging, net, GPIO, stacks, heap
PINOUT.md              # Pin assignments (authoritative)
test_udp_simple.py     # Topside-side smoke test
```

## Architecture notes worth remembering

- **Static IP** `192.168.1.100`. No DHCP. Topside is typically `.1`.
- **UDP sockets in use** (8): command, telem, pid_config, axis_config,
  setpoint_override, resource_monitor, log_udp, ctrl_telem. Limits in
  `prj.conf` (`CONFIG_ZVFS_OPEN_MAX=16`, `CONFIG_NET_MAX_CONTEXTS=10`)
  have headroom but not much — adding another socket means bumping these.
- **Logging is deferred** (`CONFIG_LOG_MODE_DEFERRED=y`). LOG_* calls
  don't block. UDP log backend starts *after* `network_init()` — order
  matters in `main()`.
- **Per-module log levels** are set inline via
  `LOG_MODULE_REGISTER(name, LOG_LEVEL_xxx)`. Global default is WARN.
  `CONFIG_LOG_RUNTIME_FILTERING=n` — no shell override.
- **Thruster mapping** assumes a specific physical layout (see
  `thruster_mapping.h`). One thruster is reverse-soldered (BRF) and is
  compensated in software. Don't "fix" the sign without checking.
- **PID disabled when all gains == 0** (`pid_is_disabled`). Used as a
  passthrough mode; treat it as intentional, not a bug.
- **Setpoint override** (`control_set_override`) is for bench/test use —
  it bypasses stick input. Make sure it's cleared before water trials.
- **PA5 conflict**: shared between the user LED (D13) and IMU SPI CLK.
  Currently wired as SPI CLK; LED is dead.

## Conventions / preferences

- Comments: explain *why*, not *what*. Existing code over-comments in
  places (legacy); don't perpetuate it in new code.
- Use `zsock_*` socket names explicitly (POSIX names disabled).
- Prefer editing existing files. The structure is already settled.
- Safety-critical paths (`src/pid/`, `src/vesc/`, `src/control.c`) get
  reviewed by the `rov-safety-reviewer` agent before merging.

## Agents available in `.claude/agents/`

- **zephyr-build** — runs `west build` / `west flash`, parses errors,
  knows board-specific quirks.
- **zephyr-config** — `prj.conf`, board overlays, DTS, Kconfig lookups
  against the upstream Zephyr tree.
- **rov-safety-reviewer** — review-only; checks diffs touching control,
  PID, thruster mapping, and VESC code for safety regressions.
- **context-keeper** — keeps this file accurate; invoke when the
  architecture shifts or you finish a feature that changes the picture.

## External references

- Zephyr docs (v4.2.0): https://docs.zephyrproject.org/4.2.0/
- Board: https://docs.zephyrproject.org/4.2.0/boards/st/nucleo_f767zi/
- UiASub wiki: https://wiki.uiasub.no/k2zephyr/
- Upstream Zephyr tree (locally): `~/zephyrproject/zephyr/`
