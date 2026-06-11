# VESC all-thruster duty test

Standalone Zephyr app for sending fixed `+0.20` duty to all eight VESCs:

- local UART VESC: `COMM_SET_DUTY`
- CAN VESC IDs `1..7`: `COMM_CAN_FORWARD` + `COMM_SET_DUTY`

Connections:

- Nucleo PB6 / Arduino D1 / LPUART1 TX -> local VESC RX
- Nucleo PB7 / Arduino D0 / LPUART1 RX -> local VESC TX
- Nucleo GND -> VESC/CAN network GND

Build and flash:

```powershell
west build -p -b nucleo_h755zi_q/stm32h755xx/m7 -d build-vesc-all-duty tests/vesc_all_duty
west flash -d build-vesc-all-duty
```

The app sends `0.0` to all thrusters for 3 seconds, then sends `+0.20` to all
thrusters every 100 ms. Remove propellers or keep thrusters safely restrained.
