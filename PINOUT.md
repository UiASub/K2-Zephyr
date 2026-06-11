# K2-Zephyr Pinout Reference

## IMU (VN-100S) - SPI3
| Function | Pin | Nucleo Label | Direction |
|----------|-----|--------------|-----------|
| SCK      | PC10| SPI_SCK      | Output    |
| MISO     | PC11| SPI_MISO     | Input     |
| MOSI     | PC12| SPI_MOSI     | Output    |
| CS       | PA4 | SPI_CS       | Output    |

**Interface**: SPI3, 1 MHz max
**Data**: Yaw/Pitch/Roll, angular rates, gravity-compensated acceleration

---

## ESC Control - UART
| Function | Pin | Nucleo Label | Baud | Direction |
|----------|-----|--------------|------|-----------|
| TX       | PC6 | D1 (CN10.2)  | 115200 | Output    |
| RX       | PC7 | D0 (CN10.1)  | 115200 | Input     |

**Device**: USART6
**Protocol**: VESC binary protocol

---

## Ethernet (Network)
| Function | Pin | Direction |
|----------|-----|-----------|
| MDC      | PC1 | Output    |
| MDIO     | PA2 | Bidir     |
| RXD0     | PC4 | Input     |
| RXD1     | PC5 | Input     |
| RX_DV    | PA7 | Input     |
| REF_CLK  | PA1 | Input     |
| TX_EN    | PG11| Output    |
| TXD0     | PG13| Output    |
| TXD1     | PB13| Output    |

**Interface**: Ethernet MAC + PHY
**IP**: 10.77.0.2

---

## Light (Dimmable LED driver) - PWM
| Function | Pin | Connector | Timer | Direction |
|----------|-----|-----------|-------|-----------|
| PWM      | PE5 | ST Morpho | TIM15_CH1 | Output |

**Frequency**: 1 kHz
**Control**: brightness 0-255 from the command packet `light` byte -> duty cycle.
A single PWM signal drives the external LED driver that powers both light LEDs.

---

## Manipulator - PWM
| Function | Pin | Connector | Timer | Direction |
|----------|-----|-----------|-------|-----------|
| PWM      | PD15 | D9 / ST Morpho | TIM4_CH4 | Output |

**Frequency**: 200 Hz
**Control**: signed manipulator command byte `-128..127` maps to a 1000-2000 us pulse, with 1500 us neutral.
TIM4_CH4/PD15 is separate from the enabled UART, SPI, I2C, Ethernet, and light PWM pins.

---

## Notes
- All pins are on STM32H755ZI (Nucleo-144 board).
