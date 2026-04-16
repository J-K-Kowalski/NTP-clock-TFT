// config.h
#ifndef CONFIG_H
#define CONFIG_H

// -------------------------------------------------------
// WiFi
// -------------------------------------------------------
#define WIFI_SSID         "YOUR SSID1"
#define WIFI_PASSWORD     "YOUR KEY1"

#define WIFI_SSID2        "YOUR SSID2"   // zapasowa sieć
#define WIFI_PASSWORD2    "YOUR KEY1"      // hasło zapasowej sieci

#define WIFI_TIMEOUT_MS   15000   // czas oczekiwania na połączenie [ms]

// -------------------------------------------------------
// NTP
// -------------------------------------------------------
#define NTP_SERVER        "tempus1.gum.gov.pl"
#define NTP_SERVER2       "tempus2.gumgov.pl"          // zapasowy
#define NTP_INTERVAL      640000    // interwał odpytywania NTP [ms], min 15000

// -------------------------------------------------------
// NeoPixel (wbudowana dioda Waveshare ESP32-C3-Zero)
// -------------------------------------------------------
#define NEO_PIN           10     // GPIO wbudowanej diody WS2812
#define NEO_COUNT          1     // 1 dioda na płytce
#define NEO_BRIGHTNESS   100     // Jasność 0-255
#define NEO_FLASH_MS     200     // Czas błysku po każdej sekundzie [ms]

// -------------------------------------------------------
// TFT ST7789 170x320
// -------------------------------------------------------
#define TFT_SCLK           4
#define TFT_MOSI           6
#define TFT_CS             7
#define TFT_DC             2
#define TFT_RST            3
#define TFT_W            320     // po obrocie landscape
#define TFT_H            170     // po obrocie landscape
#define TFT_SPI_FREQ  20000000UL // 20 MHz

// -------------------------------------------------------
// Strefa czasowa Polska (CET/CEST) — POSIX TZ string
// Zmiana na CEST: ostatnia niedziela marca o 2:00
// Powrót do CET:  ostatnia niedziela października o 3:00
// -------------------------------------------------------
#define TZ_POLAND         "CET-1CEST,M3.5.0,M10.5.0/3"

// -------------------------------------------------------
// Pozycje Y elementów na ekranie (landscape 320x170)
// -------------------------------------------------------
#define Y_TIME_BASELINE   77   // baseline czcionki czasu (canvas h=90)
#define Y_CANVAS_H        82   // wysokość canvasa czasu [px]
#define Y_DATE_AREA       85   // y górna krawędź strefy daty
#define Y_DATE_BASELINE  115   // baseline czcionki daty
#define Y_DATE_AREA_H     45   // wysokość strefy daty [px]
#define Y_STATUS_LINE    135   // y linii separatora paska statusu
#define Y_STATUS_TEXT    158   // baseline tekstu paska statusu (NTP / SSID)
#define Y_STATUS_AREA    133   // y górna krawędź paska statusu
#define Y_STATUS_AREA_H   37   // wysokość paska statusu [px]

#endif // CONFIG_H
