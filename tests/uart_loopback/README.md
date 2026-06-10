# UART loopback test

Standalone Zephyr app for checking UART loopback on the NUCLEO-H755ZI-Q.

Wire PB6 directly to PB7, then build and flash:

```powershell
west build -p -b nucleo_h755zi_q/stm32h755xx/m7 -d build-uart-loopback tests/uart_loopback
west flash -d build-uart-loopback
```

Open the board console over ST-LINK VCP at 115200 baud. The test sends one
binary packet per second on LPUART1 TX PB6 and verifies that LPUART1 RX PB7
reads the exact same bytes back.
