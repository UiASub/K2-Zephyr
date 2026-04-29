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

## Notes
- All pins are on STM32H755ZI (Nucleo-144 board).
