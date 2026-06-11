# MS5837 depth sensor test

Standalone Zephyr app for reading an MS5837-30BA depth sensor on PB8/PB9 on the
NUCLEO-H755ZI-Q. The bus is intentionally bit-banged at about 10 kHz for
debugging sensor modules that behave poorly at standard 100 kHz I2C.

Connections:

- Sensor SCL / green -> PB8
- Sensor SDA / white -> PB9
- Sensor VCC / red -> 3.3 V or 5 V according to the sensor board label
- Sensor GND / black -> GND

Build and flash:

```powershell
west build -p -b nucleo_h755zi_q/stm32h755xx/m7 -d build-depth-sensor tests/depth_sensor
west flash -d build-depth-sensor
```

Open the ST-LINK serial console at 115200 baud. The app tries I2C addresses
`0x76` and `0x77`, reads calibration PROM values, then prints raw ADC,
temperature, pressure, and depth relative to startup pressure.
