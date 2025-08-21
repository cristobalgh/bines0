## Mezclador de bines agua y AFS40
qsa jeje
```
# apt-get update
# apt-get install -y git
```
 
 ### Orange Pi 4/4B/4 LTS

 ```
 +------+-----+----------+------+---+OrangePi 4+---+---+--+----------+-----+------+
 | GPIO | wPi |   Name   | Mode | V | Physical | V | Mode | Name     | wPi | GPIO |
 +------+-----+----------+------+---+----++----+---+------+----------+-----+------+
 |      |     |     3.3V |      |   |  1 || 2  |   |      | 5V       |     |      |
 |   64 |   0 | I2C2_SDA | ALT3 | 0 |  3 || 4  |   |      | 5V       |     |      |
 |   65 |   1 | I2C2_SCL | ALT3 | 0 |  5 || 6  |   |      | GND      |     |      |
 |  150 |   2 |     PWM1 |   IN | 0 |  7 || 8  | 1 | ALT2 | I2C3_SCL | 3   | 145  |
 |      |     |      GND |      |   |  9 || 10 | 1 | ALT2 | I2C3_SDA | 4   | 144  |
 |   33 |   5 | GPIO1_A1 |   IN | 0 | 11 || 12 | 1 | IN   | GPIO1_C2 | 6   | 50   |
 |   35 |   7 | GPIO1_A3 |  OUT | 0 | 13 || 14 |   |      | GND      |     |      |
 |   92 |   8 | GPIO2_D4 |  OUT | 1 | 15 || 16 | 0 | IN   | GPIO1_C6 | 9   | 54   |
 |      |     |     3.3V |      |   | 17 || 18 | 0 | IN   | GPIO1_C7 | 10  | 55   |
 |   40 |  11 | SPI1_TXD | ALT2 | 1 | 19 || 20 |   |      | GND      |     |      |
 |   39 |  12 | SPI1_RXD | ALT2 | 1 | 21 || 22 | 0 | IN   | GPIO1_D0 | 13  | 56   |
 |   41 |  14 | SPI1_CLK | ALT3 | 1 | 23 || 24 | 1 | ALT3 | SPI1_CS  | 15  | 42   |
 |      |     |      GND |      |   | 25 || 26 | 0 | IN   | GPIO4_C5 | 16  | 149  |
 |   64 |  17 | I2C2_SDA | ALT3 | 0 | 27 || 28 | 0 | ALT3 | I2C2_SCL | 18  | 65   |
 |      |     |  I2S0_RX |      |   | 29 || 30 |   |      | GND      |     |      |
 |      |     |  I2S0_TX |      |   | 31 || 32 |   |      | I2S_CLK  |     |      |
 |      |     | I2S0_SCK |      |   | 33 || 34 |   |      | GND      |     |      |
 |      |     | I2S0_SI0 |      |   | 35 || 36 |   |      | I2S0_SO0 |     |      |
 |      |     | I2S0_SI1 |      |   | 37 || 38 |   |      | I2S0_SI2 |     |      |
 |      |     |      GND |      |   | 39 || 40 |   |      | I2S0_SI3 |     |      |
 +------+-----+----------+------+---+----++----+---+------+----------+-----+------+
 | GPIO | wPi |   Name   | Mode | V | Physical | V | Mode | Name     | wPi | GPIO |
 +------+-----+----------+------+---+OrangePi 4+---+---+--+----------+-----+------+
```
hola
chao
