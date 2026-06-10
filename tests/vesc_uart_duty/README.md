# VESC UART duty test

Standalone Zephyr app for sending a fixed `COMM_SET_DUTY` command to a VESC
over PB6/PB7 on the NUCLEO-H755ZI-Q.

Connections:

- Nucleo PB6 / Arduino D1 / LPUART1 TX -> VESC RX
- Nucleo PB7 / Arduino D0 / LPUART1 RX -> VESC TX
- Nucleo GND -> VESC GND

Build and flash:

```powershell
west build -p -b nucleo_h755zi_q/stm32h755xx/m7 -d build-vesc-uart-duty tests/vesc_uart_duty
west flash -d build-vesc-uart-duty
```

The app sends duty `0.20` every 100 ms after a short startup delay. Remove the
propeller or keep the motor unloaded while testing.
