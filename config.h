// config.h
#ifndef CONFIG_H
#define CONFIG_H

// -------------------------------------------------------
// WiFi
// -------------------------------------------------------
#define WIFI_SSID         "YOUR SSID1"
#define WIFI_PASSWORD     "YOUR KEY1"

#define WIFI_SSID2        "YOUR SSID2"   // backup SSID
#define WIFI_PASSWORD2    "YOUR KEY2"  // backup password

#define WIFI_TIMEOUT_MS 15000            // connection timeout [ms]

// -------------------------------------------------------
// NTP
// -------------------------------------------------------
#define NTP_SERVER   "tempus1.gum.gov.pl"
#define NTP_SERVER2  "pool.ntp.org"      // backup
#define NTP_INTERVAL  640000             // NTP poll interval [ms], min 15000

// -------------------------------------------------------
// NeoPixel (built-in Waveshare ESP32-C3-Zero LED)
// -------------------------------------------------------
#define NEO_PIN        10    // GPIO of WS2812 LED
#define NEO_COUNT       1    // 1 LED on the board
#define NEO_BRIGHTNESS 100   // brightness 0-255
#define NEO_FLASH_MS   200   // flash duration per second tick [ms]

// -------------------------------------------------------
// TFT ST7789 170x320
// -------------------------------------------------------
#define TFT_SCLK        4
#define TFT_MOSI        6
#define TFT_CS          7
#define TFT_DC          2
#define TFT_RST         3
#define TFT_W         320    // after landscape rotation
#define TFT_H         170    // after landscape rotation
#define TFT_SPI_FREQ  20000000UL  // 20 MHz

// -------------------------------------------------------
// Timezone Poland (CET/CEST) — POSIX TZ string
// Switch to CEST: last Sunday of March at 02:00
// Switch to CET:  last Sunday of October at 03:00
// -------------------------------------------------------
#define TZ_POLAND "CET-1CEST,M3.5.0,M10.5.0/3"

// -------------------------------------------------------
// Screen layout — Y positions (landscape 320x170)
// -------------------------------------------------------
#define Y_TIME_BASELINE  77   // time font baseline (canvas h=82)
#define Y_CANVAS_H       82   // time canvas height [px]
#define Y_DATE_AREA      85   // top edge of date zone
#define Y_DATE_BASELINE 115   // date font baseline
#define Y_DATE_AREA_H    45   // date zone height [px]
#define Y_STATUS_LINE   135   // y of status bar separator line
#define Y_STATUS_TEXT   158   // text baseline of status bar (NTP / SSID)
#define Y_STATUS_AREA   133   // top edge of status bar
#define Y_STATUS_AREA_H  37   // status bar height [px]

// -------------------------------------------------------
// Weather — Open-Meteo (no API key required)
// https://open-meteo.com
// Location: Warsaw, Poland
// -------------------------------------------------------
#define WEATHER_LAT          52.145746f // latitude  [decimal degrees]
#define WEATHER_LON          21.067542f // longitude [decimal degrees]
#define WEATHER_INTERVAL_MS  900000UL   // fetch interval: 900 000 ms = 15 min

#endif // CONFIG_H
