# NTP-clock-TFT v3
The clock retrieves time from NTP and weather data from https://open-meteo.com.

It displays the local time and date, as well as the temperature and pressure from the location specified in the configuration.

<img src="ntpclock2.jpg" width=30%><br>

## Use

1. ESP32C3 micro
2. Waveshare 1.9 inch LCD Module 170x320 pixels

## Connection:

ESP32 – LCD<br>
GND - GND<br>
3V3 - VCC<br>
3V3 - BL<br>
GPIO3 – RST<br>
GPIO2 – DC<br>
GPIO7 – CS<br>
GPIO4 - CLK<br>
GPIO6 - DIN<br>

<i>This program was designed for the Arduino IDE C++.</i>
