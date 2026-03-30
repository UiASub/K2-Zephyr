# K2-Zephyr Pinout Reference

## IMU (VN-100S) - SPI Bit-Bang
| Function | Pin | Nucleo Label | Direction |
|----------|-----|--------------|-----------|
| CLK      | PA5 | D13          | Output    |
| MOSI     | PF14| D4           | Output    |
| MISO     | PA6 | D12          | Input     |
| CS       | PD14| D10          | Output    |

**Interface**: SPI (bit-bang), 1 MHz max
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
**IP**: 192.168.1.100

---

## Status LED - GPIO
| Function | Pin | Nucleo Label | Direction |
|----------|-----|--------------|-----------|
| LED      | PA5 | D13          | Output    |

---

## Notes
- **PA5** is shared between IMU CLK (SPI) and LED. Currently configured as SPI CLK.
- All pins are on STM32F767ZI (Nucleo-144 board).