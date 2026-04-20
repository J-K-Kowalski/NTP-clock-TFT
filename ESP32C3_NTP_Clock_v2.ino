/*
 * ESP32-C3-ZERO — NTP Clock + Weather
 * Display ST7789 320x170 (landscape)
 * Font: DSEG7 Classic Bold 55px
 * Flicker-free: RAM buffer -> pushImage() in one SPI transfer
 *
 * Libraries: Adafruit ST7789 + Adafruit GFX + Adafruit BusIO +
 *            Adafruit NeoPixel + ArduinoJson
 * Project files: config.h, DSEG7Classic_Bold_55.h
 *
 * Weather: Open-Meteo (no API key required)
 *   https://open-meteo.com
 *   Displays temperature and pressure in the date area,
 *   left-aligned, small font.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include <esp_sntp.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_NeoPixel.h>
#include <Fonts/FreeSans18pt7b.h>
#include "Fonts/FreeSans9pt7be.h"  // extended: 0x7F = degree symbol °
#include "Fonts/DSEG7Classic_Bold_55.h"
#include "config.h"
// -------------------------------------------------------
// Objects
// -------------------------------------------------------
SPIClass          hspi(FSPI);
Adafruit_ST7789   tft = Adafruit_ST7789(&hspi, TFT_CS, TFT_DC, TFT_RST);
Adafruit_NeoPixel neo = Adafruit_NeoPixel(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);
Preferences prefs;
struct RuntimeConfig {
  String wifi1_ssid;
  String wifi1_pass;
  String wifi2_ssid;
  String wifi2_pass;
  String ntp1;
  String ntp2;
};
RuntimeConfig cfg;
// -------------------------------------------------------
// RGB565 colour definitions
// -------------------------------------------------------
#define C_BG        0x0000
#define C_TIME      0x07FF   // cyan
#define C_DATE      0xFD20   // orange
#define C_DST_OK    0x07E0   // green
#define C_SYNC_WAIT 0xFFE0   // yellow
#define C_SYNC_ERR  0xF800   // red
#define C_WEATHER   0xFFFF   // white — T: / P: labels
// -------------------------------------------------------
// Sprite buffer for the time area
// Time area: full screen width x Y_CANVAS_H px height (y=0..Y_CANVAS_H-1)
// 320 * 82 * 2 bytes = 52480 B — fits in ESP32-C3 heap (400 KB RAM)
// -------------------------------------------------------
#define SPR_W  TFT_W          // 320
#define SPR_H  Y_CANVAS_H     // time zone height
#define SPR_Y  0              // start y on screen
static uint16_t*  spriteBuf = nullptr;
// GFXcanvas16 — Adafruit canvas renders into an RGB565 buffer
static GFXcanvas16* canvas  = nullptr;
// -------------------------------------------------------
// State — clock
// -------------------------------------------------------
static int  s_H=-1, s_M=-1, s_S=-1;
static int  s_Y=-1, s_Mo=-1, s_D=-1;
static bool s_dst=false, s_synced=false;
static unsigned long neoOnMs=0;
static bool neoIsOn=false;
static unsigned long lastSerialLog = 0;
static time_t         lastEpochLog  = 0;
// -------------------------------------------------------
// State — weather
// -------------------------------------------------------
static float wx_temp   = -999.0f;   // °C  (sentinel = not yet fetched)
static float wx_press  = -999.0f;   // hPa
static bool  wx_valid  = false;     // true after first successful fetch
static unsigned long lastWx = 0;       // timestamp of last weather fetch
static float wx_disp_temp  = -999.0f;  // last displayed temperature
static float wx_disp_press = -999.0f;  // last displayed pressure
// -------------------------------------------------------
void serialTimeLog() {
  struct tm t;
  time_t now;
  time(&now);
  if (!localtime_r(&now, &t)) return;
  Serial.printf(
    "[TIME] %04d-%02d-%02d %02d:%02d:%02d %s | epoch=%lld | WiFi=%s | SNTP interval=%lu ms\n",
    t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
    t.tm_hour, t.tm_min, t.tm_sec,
    (t.tm_isdst > 0) ? "CEST" : "CET",
    (long long)now,
    (WiFi.status() == WL_CONNECTED) ? "OK" : "DOWN",
    (unsigned long)sntp_get_sync_interval()
  );
  if (lastEpochLog != 0) {
    long long d = (long long)now - (long long)lastEpochLog;
    Serial.printf("[TIME] delta from previous log: %lld s\n", d);
  }
  lastEpochLog = now;
}
// -------------------------------------------------------
void hwReset() {
  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, HIGH); delay(10);
  digitalWrite(TFT_RST, LOW);  delay(50);
  digitalWrite(TFT_RST, HIGH); delay(150);
}
void neoOff()    { neo.setPixelColor(0,0); neo.show(); neoIsOn=false; }
void neoFlash()  {
  neo.setPixelColor(0, neo.Color(NEO_BRIGHTNESS, NEO_BRIGHTNESS, NEO_BRIGHTNESS));
  neo.show(); neoIsOn=true; neoOnMs=millis();
}
// -------------------------------------------------------
// Centre on screen (for date — rendered directly)
// -------------------------------------------------------
int16_t centerX(const char* txt, const GFXfont* font) {
  tft.setFont(font);
  tft.setTextSize(1);
  int16_t x1,y1; uint16_t w,h;
  tft.getTextBounds(txt, 0, 80, &x1, &y1, &w, &h);
  return (TFT_W - (int16_t)w) / 2 - x1;
}
// Centre on canvas
int16_t centerXCanvas(const char* txt, const GFXfont* font) {
  canvas->setFont(font);
  canvas->setTextSize(1);
  int16_t x1,y1; uint16_t w,h;
  canvas->getTextBounds(txt, 0, SPR_H-10, &x1, &y1, &w, &h);
  return (SPR_W - (int16_t)w) / 2 - x1;
}
// -------------------------------------------------------
// Render time into canvas -> pushImage to display (flicker-free)
// -------------------------------------------------------
void renderTime(int H, int M, int S) {
  // Clear canvas to black
  canvas->fillScreen(C_BG);
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", H, M, S);
  canvas->setFont(&DSEG7Classic_Bold_55);
  canvas->setTextSize(1);
  canvas->setTextColor(C_TIME); // canvas needs no bg colour — already cleared
  // baseline: font 55 px, canvas 82 px -> Y_TIME_BASELINE
  canvas->setCursor(centerXCanvas(buf, &DSEG7Classic_Bold_55), Y_TIME_BASELINE);
  canvas->print(buf);
  // Push canvas buffer to display in one SPI block transfer
  tft.drawRGBBitmap(0, SPR_Y, canvas->getBuffer(), SPR_W, SPR_H);
}
// -------------------------------------------------------
// Render date directly (changes once a day — flicker irrelevant)
// -------------------------------------------------------
void renderDate(int Y, int Mo, int D) {
  char buf[11];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", Y, Mo, D);
  // Clear date area
  tft.fillRect(0, Y_DATE_AREA, TFT_W-44, Y_DATE_AREA_H, C_BG);
  tft.setFont(&FreeSans18pt7b);
  tft.setTextSize(1);
  tft.setTextColor(C_DATE, C_BG);
  tft.setCursor(centerX(buf, &FreeSans18pt7b) + 20, Y_DATE_BASELINE); // shifted 20px right
  tft.print(buf);
}
// -------------------------------------------------------
void renderDST(bool dst) {
  tft.setFont(NULL); tft.setTextSize(1);
  tft.setTextColor(C_DST_OK, C_BG);
  // right corner, aligned to date baseline (y=115, null font=8px)
  tft.fillRect(TFT_W-44, Y_DATE_AREA, 44, Y_DATE_AREA_H, C_BG);
  tft.setCursor(TFT_W-36, Y_DATE_BASELINE - 7);
  tft.print(dst ? "CEST" : "CET ");
}
// -------------------------------------------------------
// Render weather: temperature and pressure
// Font: FreeSans9pt7be (cap height ~13px, line spacing ~18px)
// Left-aligned at x=2, in the left part of the date area.
// Date "YYYY-MM-DD" in FreeSans18pt is ~190px wide, centred on 320px
//   => left edge ~65px — weather text stays within x=0..64 (safe).
// T: baseline = Y_DATE_AREA + 14  (cap height from top of zone)
// P: baseline = Y_DATE_AREA + 32  (+ 18px line spacing)
// Formats:  "DD.D°C"   and   "XXXX hPa"
// -------------------------------------------------------
void renderWeather(bool force) {
  // Redraw only when values changed or a full-screen refresh is requested.
  // This prevents flickering caused by clearing and redrawing every second.
  bool tempChanged  = (wx_temp  != wx_disp_temp);
  bool pressChanged = (wx_press != wx_disp_press);
  if (!force && !tempChanged && !pressChanged) return;

  const int16_t wx_x  = 2;
  const int16_t wx_y1 = Y_DATE_AREA + 14;  // T: baseline
  const int16_t wx_y2 = Y_DATE_AREA + 32;  // P: baseline
  const int16_t wx_w  = 68;                // clear width (safe left of date)
  const int16_t wx_h  = Y_DATE_AREA_H;     // full date zone height

  tft.fillRect(wx_x - 2, Y_DATE_AREA, wx_w, wx_h, C_BG);
  tft.setFont(&FreeSans9pt7be);  // struct name from FreeSans9pt7be.h
  tft.setTextSize(1);
  tft.setTextColor(C_WEATHER, C_BG);

  char buf[16];
  if (wx_valid) {
    // Temperature: format DD.D°C  (degree symbol = char 0x7F in ext font)
    snprintf(buf, sizeof(buf), "%.1f \x7F" "C", wx_temp);  // 0x7F = degree symbol in ext font
    tft.setCursor(wx_x, wx_y1);
    tft.print(buf);
    // Pressure: format XXXX hPa
    snprintf(buf, sizeof(buf), "%.0f hPa", wx_press);
    tft.setCursor(wx_x, wx_y2);
    tft.print(buf);
  } else {
    // Placeholder before first successful fetch
    tft.setCursor(wx_x, wx_y1); tft.print("--.- \x7F" "C");  // 0x7F = degree symbol in ext font
    tft.setCursor(wx_x, wx_y2); tft.print("---- hPa");
  }
  tft.setFont(NULL); // restore default font

  // Save displayed values to detect future changes
  wx_disp_temp  = wx_temp;
  wx_disp_press = wx_press;
}
// -------------------------------------------------------
// Fetch weather from Open-Meteo (no API key required)
// URL built from WEATHER_LAT / WEATHER_LON defined in config.h
// On success: updates wx_temp, wx_press, wx_valid and logs to Serial.
// -------------------------------------------------------
void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WX] WiFi not connected — skipping fetch");
    return;
  }
  char url[256];
  snprintf(url, sizeof(url),
    "http://api.open-meteo.com/v1/forecast"
    "?latitude=%.6f&longitude=%.6f"
    "&current=temperature_2m,surface_pressure"
    "&timezone=Europe%%2FWarsaw",
    (double)WEATHER_LAT, (double)WEATHER_LON
  );
  Serial.printf("[WX] Fetching: %s\n", url);
  HTTPClient http;
  http.begin(url);
  http.setTimeout(10000);
  int httpCode = http.GET();
  Serial.printf("[WX] HTTP response code: %d\n", httpCode);
  if (httpCode == 200) {
    String body = http.getString();
    Serial.printf("[WX] Response length: %d bytes\n", body.length());
    // Parse JSON
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (!err) {
      wx_temp  = doc["current"]["temperature_2m"].as<float>();
      wx_press = doc["current"]["surface_pressure"].as<float>();
      wx_valid = true;
      Serial.printf("[WX] OK — T=%.1f C  P=%.0f hPa\n", wx_temp, wx_press);
    } else {
      Serial.printf("[WX] JSON parse error: %s\n", err.c_str());
    }
  } else if (httpCode < 0) {
    Serial.printf("[WX] Connection error: %s\n", http.errorToString(httpCode).c_str());
  } else {
    Serial.printf("[WX] Unexpected HTTP code: %d\n", httpCode);
  }
  http.end();
}
// -------------------------------------------------------
// drawClock
// -------------------------------------------------------
void drawClock(bool force) {
  struct tm t;
  if (!getLocalTime(&t)) return;
  int H=t.tm_hour, M=t.tm_min, S=t.tm_sec;
  int Y=t.tm_year+1900, Mo=t.tm_mon+1, D=t.tm_mday;
  bool dst=(t.tm_isdst>0);
  if (force || !s_synced) {
    tft.fillScreen(C_BG);
    s_synced=true;
    s_H=s_M=s_S=-1; s_Y=s_Mo=s_D=-1;
  }
  // Time — via canvas (flicker-free)
  if (force || H!=s_H || M!=s_M || S!=s_S) {
    s_H=H; s_M=M; s_S=S;
    renderTime(H, M, S);
  }
  // Date — once per day
  if (force || Y!=s_Y || Mo!=s_Mo || D!=s_D) {
    s_Y=Y; s_Mo=Mo; s_D=D;
    renderDate(Y, Mo, D);
  }
  // CET/CEST — always draw after renderTime (canvas may have overwritten this area)
  if (force || dst!=s_dst) {
    s_dst=dst;
  }
  renderDST(s_dst);   // always last, after canvas and date
  // Weather — always after date (date clears the area)
  renderWeather(force);
  // Status bar — only on force (static data, does not change every second)
  if (force) {
    renderStatusBar();
  }
}
// -------------------------------------------------------
// Bottom status bar: NTP server (left) | SSID (right)
// y=133..170 — small NULL font size 1 (8 px)
// -------------------------------------------------------
void renderStatusBar() {
  tft.fillRect(0, Y_STATUS_AREA, TFT_W, Y_STATUS_AREA_H, C_BG);

  // Separator line — light blue
  tft.drawFastHLine(0, Y_STATUS_LINE, TFT_W, 0x3DDF);

  tft.setFont(&FreeSans9pt7be);
  tft.setTextSize(1);
  tft.setTextColor(0xFFFF, C_BG);

  // NTP server — left side
  tft.setCursor(2, Y_STATUS_TEXT);
  tft.print(cfg.ntp1.c_str());

  // SSID — right side
  char ssidbuf[32];
  String activeSsid = WiFi.SSID();
  if (activeSsid.length() == 0) activeSsid = cfg.wifi1_ssid;
  snprintf(ssidbuf, sizeof(ssidbuf), "%.16s", activeSsid.c_str());
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(ssidbuf, 0, Y_STATUS_TEXT, &x1, &y1, &w, &h);
  tft.setCursor(TFT_W - (int16_t)w - 2, Y_STATUS_TEXT);
  tft.print(ssidbuf);

  tft.setFont(NULL);
}
// -------------------------------------------------------
// Attempt connection to one network, timeout WIFI_TIMEOUT_MS
// -------------------------------------------------------
bool tryConnect(const char* ssid, const char* pass) {
  Serial.printf("\nWiFi: %s ...", ssid);
  tft.fillRect(0, 0, TFT_W, 60, C_BG);
  tft.setFont(NULL); tft.setTextSize(1);
  tft.setTextColor(C_SYNC_WAIT, C_BG);
  tft.setCursor(4, 4); tft.print("WiFi: "); tft.print(ssid);
  WiFi.disconnect(true);
  delay(100);
  WiFi.begin(ssid, pass);
  unsigned long t0 = millis();
  int col = 0;
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > WIFI_TIMEOUT_MS) {
      Serial.println(" TIMEOUT");
      return false;
    }
    delay(400); Serial.print(".");
    tft.setCursor(4 + col * 6, 18); tft.print(".");
    col = (col + 1) % 52;
    if (col == 0) tft.fillRect(0, 18, TFT_W, 10, C_BG);
  }
  return true;
}
// -------------------------------------------------------
// NVS / Preferences — configuration stored in the NVS partition
// -------------------------------------------------------
void loadConfigNVS() {
  prefs.begin("clockcfg", true);
  cfg.wifi1_ssid = prefs.getString("w1_ssid", WIFI_SSID);
  cfg.wifi1_pass = prefs.getString("w1_pass",  WIFI_PASSWORD);
  cfg.wifi2_ssid = prefs.getString("w2_ssid",  WIFI_SSID2);
  cfg.wifi2_pass = prefs.getString("w2_pass",  WIFI_PASSWORD2);
  cfg.ntp1       = prefs.getString("ntp1",     NTP_SERVER);
  cfg.ntp2       = prefs.getString("ntp2",     NTP_SERVER2);
  prefs.end();
}
bool saveConfigNVS() {
  prefs.begin("clockcfg", false);
  bool ok = true;
  ok &= prefs.putString("w1_ssid", cfg.wifi1_ssid) > 0;
  ok &= prefs.putString("w1_pass", cfg.wifi1_pass)  > 0;
  ok &= prefs.putString("w2_ssid", cfg.wifi2_ssid)  > 0;
  ok &= prefs.putString("w2_pass", cfg.wifi2_pass)  > 0;
  ok &= prefs.putString("ntp1",    cfg.ntp1)         > 0;
  ok &= prefs.putString("ntp2",    cfg.ntp2)         > 0;
  prefs.end();
  return ok;
}
void clearConfigNVS() {
  prefs.begin("clockcfg", false);
  prefs.clear();
  prefs.end();
}
void showConfig() {
  Serial.println("--- CONFIG ---");
  Serial.printf("wifi1_ssid = %s\n", cfg.wifi1_ssid.c_str());
  Serial.printf("wifi1_pass = %s\n", cfg.wifi1_pass.c_str());
  Serial.printf("wifi2_ssid = %s\n", cfg.wifi2_ssid.c_str());
  Serial.printf("wifi2_pass = %s\n", cfg.wifi2_pass.c_str());
  Serial.printf("ntp1       = %s\n", cfg.ntp1.c_str());
  Serial.printf("ntp2       = %s\n", cfg.ntp2.c_str());
  Serial.printf("weather    = lat=%.4f lon=%.4f interval=%lu ms\n",
                (double)WEATHER_LAT, (double)WEATHER_LON,
                (unsigned long)WEATHER_INTERVAL_MS);
}
void printConfigHelp() {
  Serial.println("UART commands:");
  Serial.println("  showcfg");
  Serial.println("  savecfg|ssid1|pass1|ssid2|pass2|ntp1|ntp2");
  Serial.println("  clearcfg");
  Serial.println("  helpcfg");
}
void handleSerialConfig() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (!cmd.length()) return;
  if (cmd.equalsIgnoreCase("showcfg"))  { showConfig(); return; }
  if (cmd.equalsIgnoreCase("helpcfg"))  { printConfigHelp(); return; }
  if (cmd.equalsIgnoreCase("clearcfg")) {
    clearConfigNVS();
    loadConfigNVS();
    Serial.println("NVS cleared, defaults loaded from config.h");
    showConfig();
    return;
  }
  if (cmd.startsWith("savecfg|")) {
    String parts[7];
    int part = 0, start = 0;
    while (part < 7) {
      int pos = cmd.indexOf('|', start);
      if (pos < 0) { parts[part++] = cmd.substring(start); break; }
      parts[part++] = cmd.substring(start, pos);
      start = pos + 1;
    }
    if (part < 7) {
      Serial.println("ERROR: use savecfg|ssid1|pass1|ssid2|pass2|ntp1|ntp2");
      return;
    }
    cfg.wifi1_ssid = parts[1]; cfg.wifi1_pass = parts[2];
    cfg.wifi2_ssid = parts[3]; cfg.wifi2_pass = parts[4];
    cfg.ntp1 = parts[5];       cfg.ntp2 = parts[6];
    if (saveConfigNVS()) { Serial.println("Config saved to NVS"); showConfig(); }
    else                   Serial.println("ERROR writing NVS");
    return;
  }
}
void connectWiFi() {
  tft.fillScreen(C_BG);
  WiFi.mode(WIFI_STA);
  // Attempt 1 — primary network
  if (!tryConnect(cfg.wifi1_ssid.c_str(), cfg.wifi1_pass.c_str())) {
    // Attempt 2 — backup network
    tft.setFont(NULL); tft.setTextSize(1);
    tft.setTextColor(C_SYNC_WAIT, C_BG);
    tft.setCursor(4, 36); tft.print("Trying backup...");
    if (!tryConnect(cfg.wifi2_ssid.c_str(), cfg.wifi2_pass.c_str())) {
      // Both networks unavailable — wait and retry
      tft.setFont(NULL); tft.setTextSize(1);
      tft.setTextColor(C_SYNC_ERR, C_BG);
      tft.setCursor(4, 52); tft.print("No WiFi! Retrying...");
      Serial.println("Both networks unavailable, retrying in 10s");
      delay(10000);
      connectWiFi(); // recursion — retry from the beginning
      return;
    }
  }
  Serial.println("\nIP: " + WiFi.localIP().toString());
  Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
  tft.setFont(NULL); tft.setTextSize(1);
  tft.setTextColor(C_DST_OK, C_BG);
  tft.setCursor(4, 32); tft.print("IP: ");
  tft.print(WiFi.localIP().toString().c_str());
  tft.setCursor(4, 44); tft.print("SSID: ");
  tft.print(WiFi.SSID().c_str());
  delay(800);
}
// -------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== NTP Clock ESP32-C3-Zero ===");
  loadConfigNVS();
  showConfig();
  printConfigHelp();
  // Allocate canvas
  canvas = new GFXcanvas16(SPR_W, SPR_H);
  if (!canvas) {
    Serial.println("ERROR: not enough RAM for canvas!");
    while(1) delay(1000);
  }
  Serial.printf("Canvas OK: %dx%d (%d B)\n", SPR_W, SPR_H, SPR_W*SPR_H*2);
  neo.begin(); neo.setBrightness(NEO_BRIGHTNESS); neoOff();
  hwReset();
  hspi.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.init(170, 320, SPI_MODE3);
  tft.setSPISpeed(TFT_SPI_FREQ);
  tft.invertDisplay(true);
  tft.setRotation(1);
  tft.fillScreen(C_BG);
  tft.setFont(NULL); tft.setTextSize(2);
  tft.setTextColor(C_TIME, C_BG);
  tft.setCursor(70,55); tft.print("NTP Clock");
  tft.setTextSize(1); tft.setTextColor(C_DATE, C_BG);
  tft.setCursor(82,90); tft.print("ESP32-C3-Zero");
  delay(1200);
  connectWiFi();
  configTzTime(TZ_POLAND, cfg.ntp1.c_str(), cfg.ntp2.c_str());
  sntp_set_sync_interval(NTP_INTERVAL);
  sntp_restart();  // apply new interval immediately
  Serial.printf("NTP sync every %lu ms\n", (unsigned long)NTP_INTERVAL);
  tft.fillScreen(C_BG);
  tft.setFont(NULL); tft.setTextSize(2);
  tft.setTextColor(C_SYNC_WAIT, C_BG);
  tft.setCursor(55,70); tft.print("Syncing NTP...");
  struct tm t;
  for (int i=0; i<20 && !getLocalTime(&t); i++) {
    delay(500); Serial.print(".");
    neo.setPixelColor(0, neo.Color(NEO_BRIGHTNESS, NEO_BRIGHTNESS>>1, 0));
    neo.show(); delay(80); neoOff();
  }
  if (getLocalTime(&t))
    Serial.printf("\nSync OK: %04d-%02d-%02d %02d:%02d:%02d %s\n",
      t.tm_year+1900, t.tm_mon+1, t.tm_mday,
      t.tm_hour, t.tm_min, t.tm_sec, t.tm_isdst ? "CEST" : "CET");
  else
    Serial.println("\nSync FAILED");
  // First weather fetch right after WiFi is up
  fetchWeather();
  lastWx = millis(); // start interval timer after initial fetch
  drawClock(true);
  renderWeather(true); // show fetched weather immediately
  renderStatusBar();
  serialTimeLog();
}
// -------------------------------------------------------
void loop() {
  handleSerialConfig();
  unsigned long now = millis();
  // Clock update — every second
  static unsigned long lastChk = 0;
  if (now - lastChk >= 1000) {
    lastChk = now;
    struct tm t;
    if (getLocalTime(&t) && t.tm_sec != s_S) {
      drawClock(false);
      neoFlash();
    }
  }
  if (neoIsOn && (now - neoOnMs >= (unsigned long)NEO_FLASH_MS))
    neoOff();
  // Serial log every 10 minutes
  if (now - lastSerialLog >= 600000UL) {
    lastSerialLog = now;
    serialTimeLog();
  }
  // Weather fetch every WEATHER_INTERVAL_MS (default 15 min)
  if (now - lastWx >= (unsigned long)WEATHER_INTERVAL_MS) {
    lastWx = now;
    fetchWeather();
    renderWeather(false);
  }
  // Full refresh every 60 minutes
  static unsigned long lastFull = 0;
  if (now - lastFull >= 3600000UL) { // 60 min full refresh
    lastFull = now;
    drawClock(true);
  }
}
