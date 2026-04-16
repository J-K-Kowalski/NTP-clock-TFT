/*
 * ESP32-C3-ZERO — Zegar NTP
 * Wyświetlacz ST7789 320x170 (landscape)
 * Czcionka: DSEG7 Classic Bold 55px
 *
 * Biblioteki: Adafruit ST7789 + Adafruit GFX + Adafruit BusIO + Adafruit NeoPixel
 * Pliki projektu: config.h, DSEG7Classic_Bold_55.h
 */
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <time.h>
#include <esp_sntp.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_NeoPixel.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include "Fonts/DSEG7Classic_Bold_55.h"
#include "config.h"
// -------------------------------------------------------
// Obiekty
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
// Kolory RGB565
#define C_BG        0x0000
#define C_TIME      0x07FF   // cyan
#define C_DATE      0xFD20   // pomarańczowy
#define C_DST_OK    0x07E0   // zielony
#define C_SYNC_WAIT 0xFFE0   // żółty
#define C_SYNC_ERR  0xF800   // czerwony
// -------------------------------------------------------
// Bufor sprite dla obszaru czasu
// Obszar czasu: cały ekran szerokość x 90px wysokości (y=0..89)
// 320 * 90 * 2 bajty = 57600 B — mieści się w heap ESP32-C3 (400KB RAM)
// -------------------------------------------------------
#define SPR_W  TFT_W          // 320
#define SPR_H  Y_CANVAS_H     // wysokość strefy czasu
#define SPR_Y  0              // y startowe na ekranie
static uint16_t* spriteBuf = nullptr;
// GFXcanvas16 — Adafruit canvas rysuje do bufora RGB565
static GFXcanvas16* canvas = nullptr;
// -------------------------------------------------------
// Stan
// -------------------------------------------------------
static int  s_H=-1, s_M=-1, s_S=-1;
static int  s_Y=-1, s_Mo=-1, s_D=-1;
static bool s_dst=false, s_synced=false;
static unsigned long neoOnMs=0;
static bool neoIsOn=false;
static unsigned long lastSerialLog = 0;
static time_t         lastEpochLog  = 0;
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
    Serial.printf("[TIME] delta od poprzedniego logu: %lld s\n", d);
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
void neoOff()   { neo.setPixelColor(0,0); neo.show(); neoIsOn=false; }
void neoFlash() {
  neo.setPixelColor(0, neo.Color(NEO_BRIGHTNESS, NEO_BRIGHTNESS, NEO_BRIGHTNESS));
  neo.show(); neoIsOn=true; neoOnMs=millis();
}
// -------------------------------------------------------
// Wyśrodkuj na ekranie (dla daty — rysowanej bezpośrednio)
// -------------------------------------------------------
int16_t centerX(const char* txt, const GFXfont* font) {
  tft.setFont(font);
  tft.setTextSize(1);
  int16_t x1,y1; uint16_t w,h;
  tft.getTextBounds(txt, 0, 80, &x1, &y1, &w, &h);
  return (TFT_W - (int16_t)w) / 2 - x1;
}
// Wyśrodkuj na canvasie
int16_t centerXCanvas(const char* txt, const GFXfont* font) {
  canvas->setFont(font);
  canvas->setTextSize(1);
  int16_t x1,y1; uint16_t w,h;
  canvas->getTextBounds(txt, 0, SPR_H-10, &x1, &y1, &w, &h);
  return (SPR_W - (int16_t)w) / 2 - x1;
}
// -------------------------------------------------------
// Rysuj czas do canvasa → pushImage na ekran (zero migotania)
// -------------------------------------------------------
void renderTime(int H, int M, int S) {
  // Wyczyść canvas czarnym
  canvas->fillScreen(C_BG);
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", H, M, S);
  canvas->setFont(&DSEG7Classic_Bold_55);
  canvas->setTextSize(1);
  canvas->setTextColor(C_TIME);   // canvas nie potrzebuje bg — już wyczyszczony
  // baseline: czcionka 55px, canvas 90px → y=75 (55px od góry canvasa)
  canvas->setCursor(centerXCanvas(buf, &DSEG7Classic_Bold_55), Y_TIME_BASELINE);
  canvas->print(buf);
  // Prześlij bufor canvasa na ekran jednym blokiem
  tft.drawRGBBitmap(0, SPR_Y, canvas->getBuffer(), SPR_W, SPR_H);
}
// -------------------------------------------------------
// Rysuj datę bezpośrednio (zmienia się raz na dobę — migotanie nieistotne)
// -------------------------------------------------------
void renderDate(int Y, int Mo, int D) {
  char buf[11];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", Y, Mo, D);
  // Wymaż strefę daty
  tft.fillRect(0, Y_DATE_AREA, TFT_W-44, Y_DATE_AREA_H, C_BG);
  tft.setFont(&FreeSans18pt7b);
  tft.setTextSize(1);
  tft.setTextColor(C_DATE, C_BG);
  tft.setCursor(centerX(buf, &FreeSans18pt7b), Y_DATE_BASELINE);
  tft.print(buf);
}
// -------------------------------------------------------
void renderDST(bool dst) {
  tft.setFont(NULL); tft.setTextSize(1);
  tft.setTextColor(C_DST_OK, C_BG);
  // prawy róg, wyrównany do baseline daty (y=125, czcionka null=8px → y=118)
  tft.fillRect(TFT_W-44, Y_DATE_AREA, 44, Y_DATE_AREA_H, C_BG);
  tft.setCursor(TFT_W-36, Y_DATE_BASELINE - 7);
  tft.print(dst ? "CEST" : "CET ");
}
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
  // Czas — przez canvas (bez migotania)
  if (force || H!=s_H || M!=s_M || S!=s_S) {
    s_H=H; s_M=M; s_S=S;
    renderTime(H, M, S);
  }
  // Data — raz na dobę
  if (force || Y!=s_Y || Mo!=s_Mo || D!=s_D) {
    s_Y=Y; s_Mo=Mo; s_D=D;
    renderDate(Y, Mo, D);
  }
  // CET/CEST — rysuj zawsze po renderTime (canvas mógł przykryć ten obszar)
  if (force || dst!=s_dst) {
    s_dst=dst;
  }
  renderDST(s_dst);  // zawsze na końcu, po canvas i dacie
  // Pasek statusu — tylko przy force (dane statyczne, nie zmieniają się)
  if (force) {
    renderStatusBar();
  }
}
// -------------------------------------------------------
// Pasek statusu na dole: NTP server (lewo) | SSID (prawo)
// y=135..170 — mała czcionka NULL size 1 (8px)
// -------------------------------------------------------
void renderStatusBar() {
  tft.fillRect(0, Y_STATUS_AREA, TFT_W, Y_STATUS_AREA_H, C_BG);

  // Linia separator — jasno niebieska
  tft.drawFastHLine(0, Y_STATUS_LINE, TFT_W, 0x3DDF);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextSize(1);
  tft.setTextColor(0xFFFF, C_BG);

  // NTP server — lewa strona
  tft.setCursor(2, Y_STATUS_TEXT);
  tft.print(cfg.ntp1.c_str());

  // SSID — prawa strona
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
// Próba połączenia z jedną siecią, timeout WIFI_TIMEOUT_MS
bool tryConnect(const char* ssid, const char* pass) {
  Serial.printf("\nWiFi: %s ...", ssid);
  tft.fillRect(0, 0, TFT_W, 60, C_BG);
  tft.setFont(NULL); tft.setTextSize(1);
  tft.setTextColor(C_SYNC_WAIT, C_BG);
  tft.setCursor(4, 4); tft.print("WiFi: "); tft.print(ssid);
  WiFi.disconnect(true);
  delay(100);
  WiFi.begin(ssid, pass);
  unsigned long t0  = millis();
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
// NVS / Preferences — konfiguracja zapisywana w partycji NVS
// -------------------------------------------------------
void loadConfigNVS() {
  prefs.begin("clockcfg", true);
  cfg.wifi1_ssid = prefs.getString("w1_ssid", WIFI_SSID);
  cfg.wifi1_pass = prefs.getString("w1_pass", WIFI_PASSWORD);
  cfg.wifi2_ssid = prefs.getString("w2_ssid", WIFI_SSID2);
  cfg.wifi2_pass = prefs.getString("w2_pass", WIFI_PASSWORD2);
  cfg.ntp1       = prefs.getString("ntp1", NTP_SERVER);
  cfg.ntp2       = prefs.getString("ntp2", NTP_SERVER2);
  prefs.end();
}
bool saveConfigNVS() {
  prefs.begin("clockcfg", false);
  bool ok = true;
  ok &= prefs.putString("w1_ssid", cfg.wifi1_ssid) > 0;
  ok &= prefs.putString("w1_pass", cfg.wifi1_pass) > 0;
  ok &= prefs.putString("w2_ssid", cfg.wifi2_ssid) > 0;
  ok &= prefs.putString("w2_pass", cfg.wifi2_pass) > 0;
  ok &= prefs.putString("ntp1", cfg.ntp1) > 0;
  ok &= prefs.putString("ntp2", cfg.ntp2) > 0;
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
}
void printConfigHelp() {
  Serial.println("Komendy UART:");
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
  if (cmd.equalsIgnoreCase("showcfg")) { showConfig(); return; }
  if (cmd.equalsIgnoreCase("helpcfg")) { printConfigHelp(); return; }
  if (cmd.equalsIgnoreCase("clearcfg")) {
    clearConfigNVS();
    loadConfigNVS();
    Serial.println("NVS wyczyszczone, zaladowano domyslne z config.h");
    showConfig();
    return;
  }
  if (cmd.startsWith("savecfg|")) {
    String parts[7];
    int part = 0;
    int start = 0;
    while (part < 7) {
      int pos = cmd.indexOf('|', start);
      if (pos < 0) {
        parts[part++] = cmd.substring(start);
        break;
      }
      parts[part++] = cmd.substring(start, pos);
      start = pos + 1;
    }
    if (part < 7) {
      Serial.println("BLAD: format savecfg|ssid1|pass1|ssid2|pass2|ntp1|ntp2");
      return;
    }
    cfg.wifi1_ssid = parts[1];
    cfg.wifi1_pass = parts[2];
    cfg.wifi2_ssid = parts[3];
    cfg.wifi2_pass = parts[4];
    cfg.ntp1       = parts[5];
    cfg.ntp2       = parts[6];
    if (saveConfigNVS()) {
      Serial.println("Config zapisany do NVS");
      showConfig();
    } else {
      Serial.println("BLAD zapisu NVS");
    }
    return;
  }
}
void connectWiFi() {
  tft.fillScreen(C_BG);
  WiFi.mode(WIFI_STA);
  // Próba 1 — sieć główna
  if (!tryConnect(cfg.wifi1_ssid.c_str(), cfg.wifi1_pass.c_str())) {
    // Próba 2 — sieć zapasowa
    tft.setFont(NULL); tft.setTextSize(1);
    tft.setTextColor(C_SYNC_WAIT, C_BG);
    tft.setCursor(4, 36); tft.print("Proba zapasowej...");
    if (!tryConnect(cfg.wifi2_ssid.c_str(), cfg.wifi2_pass.c_str())) {
      // Obie sieci niedostępne — czekaj i próbuj od nowa
      tft.setFont(NULL); tft.setTextSize(1);
      tft.setTextColor(C_SYNC_ERR, C_BG);
      tft.setCursor(4, 52); tft.print("Brak WiFi! Ponawiam...");
      Serial.println("Obie sieci niedostepne, ponawianie za 10s");
      delay(10000);
      connectWiFi();   // rekurencja — spróbuj ponownie od początku
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
  loadConfigNVS();
  showConfig();
  printConfigHelp();
  // Alokuj canvas
  canvas = new GFXcanvas16(SPR_W, SPR_H);
  if (!canvas) {
    Serial.println("BLAD: brak RAM na canvas!");
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
  sntp_restart();                  // zastosuj nowy interwał od razu
  Serial.printf("NTP sync co %lu ms...\n", (unsigned long)NTP_INTERVAL);
  tft.fillScreen(C_BG);
  tft.setFont(NULL); tft.setTextSize(2);
  tft.setTextColor(C_SYNC_WAIT,C_BG);
  tft.setCursor(55,70); tft.print("Sync NTP...");
  struct tm t;
  for (int i=0; i<20 && !getLocalTime(&t); i++) {
    delay(500); Serial.print(".");
    neo.setPixelColor(0, neo.Color(NEO_BRIGHTNESS,NEO_BRIGHTNESS>>1,0));
    neo.show(); delay(80); neoOff();
  }
  if (getLocalTime(&t))
    Serial.printf("\nSync OK: %04d-%02d-%02d %02d:%02d:%02d %s\n",
      t.tm_year+1900,t.tm_mon+1,t.tm_mday,
      t.tm_hour,t.tm_min,t.tm_sec,t.tm_isdst?"CEST":"CET");
  else
    Serial.println("\nSync FAILED");
  drawClock(true);
  renderStatusBar();
  serialTimeLog();
}
// -------------------------------------------------------
void loop() {
  handleSerialConfig();
  handleSerialConfig();
  unsigned long now = millis();
  static unsigned long lastChk=0;
  if (now-lastChk >= 1000) {
    lastChk=now;
    struct tm t;
    if (getLocalTime(&t) && t.tm_sec!=s_S) {
      drawClock(false);
      neoFlash();
    }
  }
  if (neoIsOn && (now-neoOnMs >= (unsigned long)NEO_FLASH_MS))
    neoOff();
  // Log na UART co 10 minut
  if (now - lastSerialLog >= 600000UL) {
    lastSerialLog = now;
    serialTimeLog();
  }
  // Pełne odświeżenie co 5 minut
  static unsigned long lastFull=0;
  if (now-lastFull >= 300000UL) {
    lastFull=now;
    drawClock(true);
  }
}