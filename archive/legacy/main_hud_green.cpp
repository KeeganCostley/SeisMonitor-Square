#include <Arduino.h>

/*
 * ═══════════════════════════════════════════════════════════════════════════
 * SEISMONITOR V2.0 - PRODUCTION EARTHQUAKE MONITORING SYSTEM
 * ═══════════════════════════════════════════════════════════════════════════
 * 
 * Professional, Swiss-minimalist earthquake monitor for ESP32 + TFT display
 * 
 * Features:
 * - Multi-region support (NZ, Japan, China, California, Global)
 * - Three refined visual themes (Elegant, Contrast, Mono)
 * - Real-time seismograph animation
 * - Web-based configuration portal
 * - Earthquake alerts with magnitude thresholds
 * - Clean, modular architecture
 * 
 * Hardware: ESP32-S3 (QDTFT ES3C28P)
 * Display: 2.8" IPS 320x240 ILI9341  |  Touch: FT6336G capacitive I2C
 * 
 * Version: 2.0 - Production Ready
 * ═══════════════════════════════════════════════════════════════════════════
 */

// ═══════════════════════════════════════════════════════════════════════════
// LIBRARIES
// ═══════════════════════════════════════════════════════════════════════════

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <Free_Fonts.h>   // Adafruit GFX sans-serif — much cleaner than built-in bitmap
#include <FS.h>
#include <time.h>
using namespace fs;
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <Wire.h>           // FT6336G capacitive touch (I2C) + ES8311 codec
#include <driver/i2s.h>    // I2S audio output

// ═══════════════════════════════════════════════════════════════════════════
// HARDWARE CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

const int BUTTON_PIN     = 0;    // BOOT button - physical fallback
const int TFT_BL_PIN     = 45;   // Backlight enable - HIGH = on
// I2S Audio (ES8311 codec) — dedicated GPIO block, no SPI conflict
#define I2S_MCLK     4
#define I2S_BCLK     5
#define I2S_DOUT     6
#define I2S_WS       7
#define AMP_EN       1   // AUDIO_EN: LOW = amp on, HIGH = amp off
#define ES8311_ADDR 0x18
#define AUDIO_SR    16000
#define AUDIO_PORT  I2S_NUM_0

// FT6336G capacitive touch (I2C)
const int TOUCH_SDA      = 16;
const int TOUCH_SCL      = 15;
const int TOUCH_INT      = 17;
const int TOUCH_RST      = 18;
const uint8_t TOUCH_ADDR = 0x38;

// Typography — GFX Free Sans (anti-aliased appearance, much cleaner than bitmap)
#define FONT_LABEL  &FreeSans9pt7b       // Headers, labels
#define FONT_DATA   &FreeSans9pt7b        // Magnitude numbers — medium weight

const int SCREEN_WIDTH  = 240;   // Portrait mode — 240×240 visible square
const int SCREEN_HEIGHT = 240;

// ═══════════════════════════════════════════════════════════════════════════
// TIMING CONSTANTS
// ═══════════════════════════════════════════════════════════════════════════

const unsigned long API_POLL_INTERVAL = 30000;      // 30 seconds
const unsigned long SEISMO_UPDATE_INTERVAL = 100;   // 100ms (was 50ms - slowed by 50%)
const unsigned long DISPLAY_CYCLE_INTERVAL = 60000; // 60 seconds
const unsigned long REST_MODE_TIMEOUT = 45000;      // 45 seconds
const unsigned long ALERT_DURATION = 25000;         // 25 seconds
const unsigned long DEBOUNCE_DELAY = 300;           // 300ms
const int HTTP_TIMEOUT = 5000;                      // 5 seconds
const byte DNS_PORT = 53;

// ═══════════════════════════════════════════════════════════════════════════
// DISPLAY LAYOUT — Asymmetric: data left, square map right, seismograph bottom
// ═══════════════════════════════════════════════════════════════════════════

const int HEADER_H = 18;

// Left column - Data readouts (latest + highest stacked)
const int DATA_X = 2;
const int DATA_Y = HEADER_H;
const int DATA_WIDTH = 76;
const int DATA_HEIGHT = 158;

// Right area - Square map
const int MAP_X = 82;
const int MAP_Y = HEADER_H;
const int MAP_WIDTH = 156;
const int MAP_HEIGHT = 158;

// Bottom strip - seismograph background area (full-width)
const int SEISMO_X = 4;
const int SEISMO_Y = 180;
const int SEISMO_WIDTH = 232;
const int SEISMO_HEIGHT = 56;
const int SEISMO_CENTER_Y = SEISMO_Y + (SEISMO_HEIGHT / 2);
const int SEISMO_MAX_AMPLITUDE = 50;

// Floating line area — 70% of screen width, centred
const int SEISMO_LINE_W = (SCREEN_WIDTH * 7) / 10;        // 168 px
const int SEISMO_LINE_X = (SCREEN_WIDTH - SEISMO_LINE_W) / 2;  // 36 px

// ═══════════════════════════════════════════════════════════════════════════
// REGIONAL BOUNDARIES
// ═══════════════════════════════════════════════════════════════════════════

struct RegionBounds {
  float latMin;
  float latMax;
  float lonMin;
  float lonMax;
};

const RegionBounds BOUNDS_NZ = {-48.0, -33.5, 165.5, 180.5};
const RegionBounds BOUNDS_JAPAN = {30.0, 45.5, 129.0, 146.0};
const RegionBounds BOUNDS_CHINA = {18.0, 54.0, 73.0, 135.0};  // Entire mainland China
const RegionBounds BOUNDS_CALIFORNIA = {32.5, 42.0, -124.5, -114.0};
const RegionBounds BOUNDS_GLOBAL = {-60.0, 75.0, -180.0, 180.0};

// ═══════════════════════════════════════════════════════════════════════════
// API ENDPOINTS - REGION SPECIFIC
// ═══════════════════════════════════════════════════════════════════════════

const char* API_NZ = "https://api.geonet.org.nz/quake?MMI=2";

// USGS Region-Specific Feeds (much better than global!)
const char* API_JAPAN = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/2.5_day.geojson"; // Will filter manually
const char* API_CHINA = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/2.5_day.geojson"; // Global feed, filtered by bounds
const char* API_CALIFORNIA = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/2.5_day.geojson"; // Will filter manually
const char* API_GLOBAL = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/4.5_day.geojson"; // Only M4.5+ globally

// ═══════════════════════════════════════════════════════════════════════════
// DATA STRUCTURES
// ═══════════════════════════════════════════════════════════════════════════

struct Theme {
  uint16_t background;
  uint16_t border;
  uint16_t divider;
  uint16_t textPrimary;
  uint16_t textSecondary;
  uint16_t textAccent;
  uint16_t seismoLine;
  uint16_t seismoGrid;
  uint16_t mapOutline;
  uint16_t mapCity;
  uint16_t mapOcean;   // Ocean fill background
  uint16_t mapLand;    // Land fill colour
  uint16_t dataLatest;  // LATEST earthquake data + map marker
  uint16_t dataHighest; // HIGHEST 24h data + map marker
};

struct Config {
  char wifiSSID[64];
  char wifiPassword[64];
  char region[16];
  float magThreshold;
  int fontSize;
  bool showRecentQuakes;
  bool showCityDots;
  int soundMode;    // 0=off, 1=retro, 2=rumble
};

struct EarthquakeData {
  float magnitude;
  float latitude;
  float longitude;
  float depth;
  char location[128];
  unsigned long timestamp;
  bool isValid;
  
  void clear() {
    magnitude = latitude = longitude = depth = 0;
    location[0] = '\0';
    timestamp = 0;
    isValid = false;
  }
};

struct QuakeHistory {
  float lat;
  float lon;
  float mag;
  bool valid;
};

// ═══════════════════════════════════════════════════════════════════════════
// GLOBAL STATE
// ═══════════════════════════════════════════════════════════════════════════

Config config;
Theme currentTheme;

EarthquakeData latestQuake;
EarthquakeData highestRegionalQuake;
QuakeHistory recentQuakes[20];
int recentQuakeCount = 0;

int displayMode = 0;
bool isRestMode = false;
bool isConfigMode = false;
bool showingAlert = false;



int seismoX = SEISMO_LINE_X;
int seismoLastY = SEISMO_CENTER_Y;

unsigned long lastAPICheck = 0;
unsigned long lastSeismoUpdate = 0;
unsigned long lastDisplaySwitch = 0;
unsigned long lastActivity = 0;
unsigned long lastButtonPress = 0;
unsigned long alertStartTime  = 0;
unsigned long lastTouchTime   = 0;   // millis() of last confirmed tap
bool          prevTouching    = false;

String lastQuakeID = "";

// ── SEISMOGRAPH WAVE SIMULATION STATE ───────────────────────────────────────
enum SeismoPhase { SEISMO_QUIET, SEISMO_P_WAVE, SEISMO_S_WAVE, SEISMO_SURFACE, SEISMO_CODA };
SeismoPhase seismoPhase   = SEISMO_QUIET;
unsigned long seismoPWaveStart  = 0;   // millis() when P-wave arrives
unsigned long seismoSWaveStart  = 0;   // millis() when S-wave arrives
unsigned long seismoSurfStart   = 0;   // millis() when surface waves arrive
unsigned long seismoCodaStart   = 0;   // millis() when coda starts
unsigned long seismoEventEnd    = 0;   // millis() when event fully decays
float seismoPAmp    = 0;  // P-wave peak pixel amplitude
float seismoSAmp    = 0;  // S-wave peak pixel amplitude
float seismoSurfAmp = 0;  // Surface-wave peak pixel amplitude
String lastTriggeredQuakeID = "";

// ═══════════════════════════════════════════════════════════════════════════
// THEME DEFINITIONS
// ═══════════════════════════════════════════════════════════════════════════

Theme createHUDTheme() {
  Theme t;
  t.background    = 0x0000;  // Black
  t.border        = 0x0160;  // Very dark green
  t.divider       = 0x0180;  // Dark green
  t.textPrimary   = 0x07E0;  // Phosphor green
  t.textSecondary = 0x03A0;  // Medium green ~50%
  t.textAccent    = 0x07E0;
  t.seismoLine    = 0x07E0;  // Phosphor green
  t.seismoGrid    = 0x00E0;  // Very dim green
  t.mapOutline    = 0x0460;  // Medium green
  t.mapCity       = 0x07E0;
  t.mapOcean      = 0x0000;  // Black
  t.mapLand       = 0x01A0;  // Dark green
  t.dataLatest    = 0x07E0;  // Full phosphor green
  t.dataHighest   = 0x87E0;  // Chartreuse — visually distinct from latest
  return t;
}


// ═══════════════════════════════════════════════════════════════════════════
// CONFIGURATION MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════════

void loadConfig() {
  preferences.begin("seismonitor", true);
  
  preferences.getString("wifi_ssid", config.wifiSSID, sizeof(config.wifiSSID));
  preferences.getString("wifi_pass", config.wifiPassword, sizeof(config.wifiPassword));
  preferences.getString("region", config.region, sizeof(config.region));
  config.magThreshold = preferences.getFloat("mag_thresh", 2.0);
  config.fontSize = preferences.getInt("font_size", 2);
  config.showRecentQuakes = preferences.getBool("show_recent", true);
  config.showCityDots = preferences.getBool("show_cities", false);
  config.soundMode = preferences.getInt("sound_mode", 0);

  preferences.end();

  if (strlen(config.region) == 0) strcpy(config.region, "NZ");
  Serial.println("Config loaded");
}

void saveConfig() {
  preferences.begin("seismonitor", false);
  
  preferences.putString("wifi_ssid", config.wifiSSID);
  preferences.putString("wifi_pass", config.wifiPassword);
  preferences.putString("region", config.region);
  preferences.putFloat("mag_thresh", config.magThreshold);
  preferences.putInt("font_size", config.fontSize);
  preferences.putBool("show_recent", config.showRecentQuakes);
  preferences.putBool("show_cities", config.showCityDots);
  preferences.putInt("sound_mode", config.soundMode);

  preferences.end();
  Serial.println("Config saved");
}

// ═══════════════════════════════════════════════════════════════════════════
// REGIONAL FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

RegionBounds getRegionBounds(const char* region) {
  if (strcmp(region, "Japan") == 0) return BOUNDS_JAPAN;
  if (strcmp(region, "China") == 0) return BOUNDS_CHINA;
  if (strcmp(region, "California") == 0) return BOUNDS_CALIFORNIA;
  if (strcmp(region, "Global") == 0) return BOUNDS_GLOBAL;
  return BOUNDS_NZ;
}

bool isInRegion(float lat, float lon, const char* region) {
  if (strcmp(region, "Global") == 0) return true;
  RegionBounds bounds = getRegionBounds(region);
  return (lat >= bounds.latMin && lat <= bounds.latMax &&
          lon >= bounds.lonMin && lon <= bounds.lonMax);
}

const char* getAPIEndpoint(const char* region) {
  if (strcmp(region, "NZ") == 0) return API_NZ;
  if (strcmp(region, "Japan") == 0) return API_JAPAN;
  if (strcmp(region, "China") == 0) return API_CHINA;
  if (strcmp(region, "California") == 0) return API_CALIFORNIA;
  return API_GLOBAL;  // Global region
}

bool isUsingNZAPI(const char* region) {
  return strcmp(region, "NZ") == 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// MAP PROJECTION
// ═══════════════════════════════════════════════════════════════════════════

// Equirectangular projection with cos(centerLat) correction so that
// 1° of longitude maps to the correct fraction of 1° of latitude in pixels.
static void getMapAspect(const RegionBounds& db,
                         float& scale, float& offX, float& offY, float& cosLat) {
  float latSpan = db.latMax - db.latMin;
  float lonSpan = db.lonMax - db.lonMin;
  float centerLat = (db.latMax + db.latMin) * 0.5f;
  cosLat = cos(radians(centerLat));
  float effectiveLonSpan = lonSpan * cosLat;
  scale = min((float)MAP_WIDTH / effectiveLonSpan, (float)MAP_HEIGHT / latSpan);
  offX  = (MAP_WIDTH  - scale * effectiveLonSpan) * 0.5f;
  offY  = (MAP_HEIGHT - scale * latSpan) * 0.5f;
}

int mapLatToScreen(float lat) {
  RegionBounds db = getRegionBounds(config.region);
  float scale, offX, offY, cosLat;
  getMapAspect(db, scale, offX, offY, cosLat);
  return MAP_Y + (int)(offY + (db.latMax - lat) * scale);
}

int mapLonToScreen(float lon) {
  RegionBounds db = getRegionBounds(config.region);
  float scale, offX, offY, cosLat;
  getMapAspect(db, scale, offX, offY, cosLat);
  return MAP_X + (int)(offX + (lon - db.lonMin) * scale * cosLat);
}

// ═══════════════════════════════════════════════════════════════════════════
// UTILITY FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

String getTimeAgo(unsigned long timestampSec) {
  // timestampSec is epoch seconds (not milliseconds)
  time_t now = time(nullptr);
  if (timestampSec == 0 || now < 1600000000) return "?";
  unsigned long elapsed = (unsigned long)now - timestampSec;

  if (elapsed < 60) return String(elapsed) + "s";
  if (elapsed < 3600) return String(elapsed / 60) + "m";
  if (elapsed < 86400) return String(elapsed / 3600) + "h";
  return String(elapsed / 86400) + "d";
}

// Parse GeoNet ISO 8601 timestamp "2024-01-15T12:30:00.000Z" to epoch seconds
unsigned long parseISOToEpoch(const char* iso) {
  if (!iso || strlen(iso) < 19) return 0;
  struct tm t;
  memset(&t, 0, sizeof(t));
  int year, month, day, hour, minute, second;
  if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) >= 6) {
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    return (unsigned long)mktime(&t);
  }
  return 0;
}

uint16_t getMagnitudeColor(float magnitude) {
  // Cool→warm severity ramp: seafoam → pale aqua → dusty coral → deep terracotta
  if (magnitude >= 7.0) return 0xA208;  // Deep burnt sienna
  if (magnitude >= 6.0) return 0xCB0B;  // Dusty coral/terracotta
  if (magnitude >= 5.0) return 0xAE9C;  // Pale aqua
  if (magnitude >= 4.0) return 0x7DBA;  // Muted seafoam
  return currentTheme.textAccent;
}

// ═══════════════════════════════════════════════════════════════════════════
// FORWARD DECLARATIONS
// ═══════════════════════════════════════════════════════════════════════════

void drawUI();
void drawSeismograph();
void drawDataPanel();
int  drawNarrowText(const char* text, int x, int y, int maxW, uint16_t color);
void drawMap();
void drawMapGraticule(uint16_t color);

void drawNZMap();
void drawJapanMap();
void drawChinaMap();
void drawCaliforniaMap();
void drawGlobalMap();
void animateSeismograph();
void checkForEarthquakes();
void displayEarthquakeAlert(EarthquakeData* quake);
void handleButton();
void setupConfigPortal();
void setupWebServer();
void handleWebRoot();
void handleWebSave();
void handleWebNotFound();
void handleTestSound();
void handleTestQuake();
void playStartupRumble();

// ═══════════════════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
  // Amp off immediately — prevents any power-on pop through the speaker
  pinMode(AMP_EN, OUTPUT);
  digitalWrite(AMP_EN, HIGH);

  // Backlight first — if this lights up, setup() is running
  pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, HIGH);

  delay(3000);
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n══════════════════════════════");
  Serial.println("   SEISMONITOR V2.0");
  Serial.println("══════════════════════════════\n");

  loadConfig();
  currentTheme = createHUDTheme();

  tft.init();
  tft.setRotation(0);      // Portrait — 240×320; UI uses top 240×240 square
  tft.invertDisplay(true);
  tft.fillScreen(currentTheme.background);

  // Capacitive touch (FT6336G)
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);  delay(10);
  digitalWrite(TOUCH_RST, HIGH); delay(100);
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(400000);

  // Physical BOOT button fallback
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  if (strlen(config.wifiSSID) == 0) {
    Serial.println("No WiFi - setup mode");
    isConfigMode = true;
    setupConfigPortal();
    return;
  }
  
  tft.setTextColor(currentTheme.textPrimary);
  tft.drawCentreString("SEISMONITOR", 120, 100, 4);
  tft.setTextColor(currentTheme.textSecondary);
  tft.drawCentreString("Connecting...", 120, 132, 2);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.wifiSSID, config.wifiPassword);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi failed - setup mode");
    isConfigMode = true;
    setupConfigPortal();
    return;
  }
  
  Serial.println("\nWiFi connected");
  Serial.println("IP: " + WiFi.localIP().toString());
  
  if (MDNS.begin("seismonitor")) {
    Serial.println("mDNS: http://seismonitor.local");
  }
  
  // Sync time via NTP so earthquake timestamps (UTC epoch ms) can be interpreted
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  int ttries = 0;
  while (time(nullptr) < 1600000000 && ttries < 10) {
    delay(500);
    ttries++;
  }

  setupWebServer();
  
  tft.drawCentreString("Connected", 120, 155, 2);
  delay(2000);
  
  checkForEarthquakes();
  drawUI();
  playStartupRumble();

  lastActivity = millis();
  isRestMode = true;

  Serial.println("Ready!\n");
}

// ═══════════════════════════════════════════════════════════════════════════
// MAIN LOOP
// ═══════════════════════════════════════════════════════════════════════════

void loop() {
  server.handleClient();
  
  if (isConfigMode) {
    dnsServer.processNextRequest();
    delay(10);
    return;
  }
  
  unsigned long now = millis();
  
  handleButton();
  
  if (showingAlert) {
    if (now - alertStartTime > ALERT_DURATION) {
      showingAlert = false;
      drawUI();
      lastActivity = now;
    }
    return;
  }
  
  if (now - lastSeismoUpdate > SEISMO_UPDATE_INTERVAL) {
    animateSeismograph();
    lastSeismoUpdate = now;
  }

  if (now - lastAPICheck > API_POLL_INTERVAL) {
    checkForEarthquakes();
    lastAPICheck = now;
  }
  
  if (!isRestMode && (now - lastActivity > REST_MODE_TIMEOUT)) {
    isRestMode = true;
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// PARTIAL SCREEN UPDATE HELPERS
// ═══════════════════════════════════════════════════════════════════════════

void updateSeismographRegion() {
  tft.fillRect(SEISMO_X, SEISMO_Y, SEISMO_WIDTH, SEISMO_HEIGHT, currentTheme.background);
  drawSeismograph();
}

void updateDataRegion() {
  tft.fillRect(DATA_X, DATA_Y, DATA_WIDTH, DATA_HEIGHT, currentTheme.background);
  drawDataPanel();
}

void updateMapEarthquakeMarkers() {
  tft.fillRect(MAP_X, MAP_Y, MAP_WIDTH, MAP_HEIGHT, currentTheme.background);
  drawMap();
}

// ═══════════════════════════════════════════════════════════════════════════
// TEXT WRAPPING HELPER
// ═══════════════════════════════════════════════════════════════════════════

void drawWrappedText(const char* text, int startX, int startY, int maxWidth, int maxHeight, int lineHeight, uint16_t textColor = 0xFFFF) {
  tft.setTextFont(2);
  tft.setTextColor(textColor);

  int len = strlen(text);
  int pos = 0;
  int y = startY;
  char line[64];

  while (pos < len && y + lineHeight <= maxHeight) {
    // First check if remaining text fits on one line
    int remaining = min(len - pos, 63);
    strncpy(line, text + pos, remaining);
    line[remaining] = '\0';

    if (tft.textWidth(line) <= maxWidth) {
      // It all fits - print and done
      tft.setCursor(startX, y);
      tft.print(line);
      break;
    }

    // Need to find break point - scan character by character
    int end = pos;
    int lastSpace = -1;

    for (int i = pos; i < len && i - pos < 63; i++) {
      if (text[i] == ' ') lastSpace = i;
      int segLen = i + 1 - pos;
      strncpy(line, text + pos, segLen);
      line[segLen] = '\0';
      if (tft.textWidth(line) > maxWidth) {
        end = (lastSpace > pos) ? lastSpace : i;
        break;
      }
      end = i + 1;
    }

    int printLen = end - pos;
    if (printLen <= 0) printLen = 1;
    strncpy(line, text + pos, printLen);
    line[printLen] = '\0';

    // Trim trailing spaces
    while (printLen > 0 && line[printLen - 1] == ' ') line[--printLen] = '\0';

    tft.setCursor(startX, y);
    tft.print(line);

    pos = end;
    while (pos < len && text[pos] == ' ') pos++;
    y += lineHeight;
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// UI DRAWING
// ═══════════════════════════════════════════════════════════════════════════

void drawUI() {
  tft.fillScreen(currentTheme.background);
  
  // Header bar
  tft.drawFastHLine(0, HEADER_H - 1, SCREEN_WIDTH, currentTheme.border);
  tft.setFreeFont(FONT_LABEL);
  
  // Divider between data column and map
  tft.drawFastVLine(DATA_X + DATA_WIDTH + 2, DATA_Y, DATA_HEIGHT, currentTheme.divider);
  
  // Divider above seismograph
  tft.drawFastHLine(0, SEISMO_Y - 4, SCREEN_WIDTH, currentTheme.border);
  
  drawDataPanel();
  drawMap();
  drawSeismograph();
}

void drawSeismograph() {
  // Faint horizontal grid dots — only within the floating line area
  for (int y = SEISMO_Y; y <= SEISMO_Y + SEISMO_HEIGHT; y += 10) {
    for (int x = SEISMO_LINE_X; x < SEISMO_LINE_X + SEISMO_LINE_W; x += 8) {
      tft.drawPixel(x, y, currentTheme.seismoGrid);
    }
  }

  // Center baseline — floating width only
  tft.drawFastHLine(SEISMO_LINE_X, SEISMO_CENTER_Y, SEISMO_LINE_W, currentTheme.seismoGrid);
}

// Auto-sizing location text for the narrow data column.
// First attempts FreeSans9pt7b (max 2 lines); if the text needs more than
// 2 lines at that size it falls back to the 8px bitmap font (max 3 lines)
// so that the full string is always visible.
// Returns total pixel height consumed including a 2px bottom gap.
int drawNarrowText(const char* text, int x, int y, int maxW, uint16_t color) {
  int len = strlen(text);
  char line[64];

  // ── Pass 1: count lines needed at FreeSans9pt7b ──────────────────────────
  tft.setFreeFont(FONT_LABEL);
  int simLines = 0, pos = 0;
  while (pos < len && simLines < 3) {
    int rem = min(len - pos, 63);
    strncpy(line, text + pos, rem); line[rem] = '\0';
    if (tft.textWidth(line) <= maxW) { simLines++; break; }
    int end = pos, lastSpace = -1;
    for (int i = pos; i < len && i - pos < 63; i++) {
      if (text[i] == ' ') lastSpace = i;
      int seg = i + 1 - pos;
      strncpy(line, text + pos, seg); line[seg] = '\0';
      if (tft.textWidth(line) > maxW) { end = (lastSpace > pos) ? lastSpace : i; break; }
      end = i + 1;
    }
    if (end == pos) end = pos + 1;
    pos = end; while (pos < len && text[pos] == ' ') pos++;
    simLines++;
  }

  // ── Pass 2: draw with chosen font ────────────────────────────────────────
  bool useFree = (simLines <= 2);
  int lineH    = useFree ? 13 : 10;
  int baseline = useFree ? 11 :  0;
  int maxLines = useFree ?  2 :  3;

  if (useFree) tft.setFreeFont(FONT_LABEL);
  else         tft.setTextFont(1);
  tft.setTextColor(color);

  pos = 0; int lines = 0;
  while (pos < len && lines < maxLines) {
    if (useFree) {
      int rem = min(len - pos, 63);
      strncpy(line, text + pos, rem); line[rem] = '\0';
      if (tft.textWidth(line) <= maxW) {
        tft.setCursor(x, y + lines * lineH + baseline);
        tft.print(line); lines++; break;
      }
      int end = pos, lastSpace = -1;
      for (int i = pos; i < len && i - pos < 63; i++) {
        if (text[i] == ' ') lastSpace = i;
        int seg = i + 1 - pos;
        strncpy(line, text + pos, seg); line[seg] = '\0';
        if (tft.textWidth(line) > maxW) { end = (lastSpace > pos) ? lastSpace : i; break; }
        end = i + 1;
      }
      int pl = end - pos; if (pl <= 0) pl = 1;
      strncpy(line, text + pos, pl); line[pl] = '\0';
      while (pl > 0 && line[pl - 1] == ' ') line[--pl] = '\0';
      tft.setCursor(x, y + lines * lineH + baseline);
      tft.print(line); lines++;
      pos = end; while (pos < len && text[pos] == ' ') pos++;
    } else {
      // Bitmap font 1 — fixed 6px character width
      int maxChars = maxW / 6;
      int end = min(pos + maxChars, len);
      if (end >= len) {
        tft.setCursor(x, y + lines * lineH);
        tft.print(text + pos); lines++; break;
      }
      int breakAt = end;
      while (breakAt > pos && text[breakAt] != ' ') breakAt--;
      if (breakAt == pos) breakAt = end;
      int seg = min(breakAt - pos, 63);
      strncpy(line, text + pos, seg); line[seg] = '\0';
      tft.setCursor(x, y + lines * lineH);
      tft.print(line); lines++;
      pos = breakAt; while (pos < len && text[pos] == ' ') pos++;
    }
  }
  return lines * lineH + 2;
}

void drawDataPanel() {
  tft.fillRect(DATA_X, DATA_Y, DATA_WIDTH, DATA_HEIGHT, currentTheme.background);

  int col = DATA_X + 3;
  int colW = DATA_WIDTH - 6;
  int y = DATA_Y + 4;

  // ── LATEST — tiny category label ────────────────
  tft.setTextFont(1);                          // 8px bitmap — small signpost
  tft.setTextColor(currentTheme.textSecondary);
  tft.setCursor(col, y);
  tft.print("LATEST");
  y += 11;

  if (latestQuake.isValid) {
    // Hero magnitude — big and bold
    tft.setTextColor(currentTheme.dataLatest);
    tft.setFreeFont(FONT_DATA);                // FreeSansBold9pt — dominant
    tft.setCursor(col, y + 12);
    tft.print("M");
    tft.print(latestQuake.magnitude, 1);
    y += 17;

    // Location — auto-sized wrap
    y += drawNarrowText(latestQuake.location, col, y, colW, currentTheme.dataLatest);

    // Time ago — quiet metadata
    tft.setTextFont(1);                        // 8px bitmap — demoted
    tft.setTextColor(currentTheme.textSecondary);
    tft.setCursor(col, y);
    tft.print(getTimeAgo(latestQuake.timestamp));
    tft.print(" ago");
    y += 11;
  } else {
    tft.setTextFont(1);
    tft.setTextColor(currentTheme.textSecondary);
    tft.setCursor(col, y);
    tft.print("NO DATA");
    y += 14;
  }

  // ── Divider ──────────────────────────────────────
  y += 3;
  tft.drawFastHLine(col, y, colW, currentTheme.divider);
  y += 6;

  // ── 24H HIGH — tiny category label ──────────────
  tft.setTextFont(1);                          // 8px bitmap — small signpost
  tft.setTextColor(currentTheme.textSecondary);
  tft.setCursor(col, y);
  tft.print("24H HIGH");
  y += 11;

  if (highestRegionalQuake.isValid) {
    // Hero magnitude — big and bold
    tft.setTextColor(currentTheme.dataHighest);
    tft.setFreeFont(FONT_DATA);                // FreeSansBold9pt — dominant
    tft.setCursor(col, y + 12);
    tft.print("M");
    tft.print(highestRegionalQuake.magnitude, 1);
    y += 17;

    // Location — auto-sized wrap
    y += drawNarrowText(highestRegionalQuake.location, col, y, colW, currentTheme.dataHighest);

    // Time ago — quiet metadata
    tft.setTextFont(1);                        // 8px bitmap — demoted
    tft.setTextColor(currentTheme.textSecondary);
    tft.setCursor(col, y);
    tft.print(getTimeAgo(highestRegionalQuake.timestamp));
    tft.print(" ago");
  } else {
    tft.setTextFont(1);
    tft.setTextColor(currentTheme.textSecondary);
    tft.setCursor(col, y);
    tft.print("NO DATA");
  }
}

void drawMap() {
  // Ocean base + graticule (land fill drawn inside each regional function)
  tft.fillRect(MAP_X, MAP_Y, MAP_WIDTH, MAP_HEIGHT, currentTheme.mapOcean);
  drawMapGraticule(currentTheme.seismoGrid);

  if (strcmp(config.region, "NZ") == 0) {
    drawNZMap();
  }
  else if (strcmp(config.region, "Japan") == 0) {
    drawJapanMap();
  }
  else if (strcmp(config.region, "China") == 0) {
    drawChinaMap();
  }
  else if (strcmp(config.region, "California") == 0) {
    drawCaliforniaMap();
  }
  else {
    drawGlobalMap();
  }
  
  // Plot recent quakes (can be disabled via web settings)
  if (config.showRecentQuakes) {
    for (int i = 0; i < recentQuakeCount; i++) {
      if (!recentQuakes[i].valid) continue;
      
      int x = mapLonToScreen(recentQuakes[i].lon);
      int y = mapLatToScreen(recentQuakes[i].lat);
      
      if (x < MAP_X || x > MAP_X + MAP_WIDTH || y < MAP_Y || y > MAP_Y + MAP_HEIGHT) continue;
      
      uint16_t color = getMagnitudeColor(recentQuakes[i].mag);
      int radius = (recentQuakes[i].mag >= 6.0) ? 2 : 1;

      tft.fillCircle(x, y, radius, color);
    }
  }
  
  // Highlight LATEST quake location (orange dot) - most recent event
  if (latestQuake.isValid) {
    int x = mapLonToScreen(latestQuake.longitude);
    int y = mapLatToScreen(latestQuake.latitude);
    
    if (x >= MAP_X && x <= MAP_X + MAP_WIDTH && y >= MAP_Y && y <= MAP_Y + MAP_HEIGHT) {
      tft.fillCircle(x, y, 2, currentTheme.dataLatest);
      tft.drawCircle(x, y, 3, currentTheme.dataLatest);
    }
  }

  // Highlight HIGHEST 24H quake location - strongest event in 24h
  if (highestRegionalQuake.isValid) {
    int x = mapLonToScreen(highestRegionalQuake.longitude);
    int y = mapLatToScreen(highestRegionalQuake.latitude);

    if (x >= MAP_X && x <= MAP_X + MAP_WIDTH && y >= MAP_Y && y <= MAP_Y + MAP_HEIGHT) {
      tft.fillCircle(x, y, 2, currentTheme.dataHighest);
      tft.drawCircle(x, y, 3, currentTheme.dataHighest);
    }
  }

  // Mask any coastline/marker bleed outside the map panel
  // (drawLine for coastlines isn't clipped — paint over any overflow)
  tft.fillRect(MAP_X - 4, MAP_Y - 4, MAP_WIDTH + 8, 4, currentTheme.background);
  tft.fillRect(MAP_X - 4, MAP_Y + MAP_HEIGHT, MAP_WIDTH + 8, 4, currentTheme.background);
  tft.fillRect(MAP_X - 4, MAP_Y, 4, MAP_HEIGHT, currentTheme.background);
  tft.fillRect(MAP_X + MAP_WIDTH, MAP_Y, 4, MAP_HEIGHT, currentTheme.background);
}

// ═══════════════════════════════════════════════════════════════════════════
// SEISMOGRAPH PHYSICS HELPERS
// ═══════════════════════════════════════════════════════════════════════════

float haversineKm(float lat1, float lon1, float lat2, float lon2) {
  const float R = 6371.0f;
  float dlat = radians(lat2 - lat1);
  float dlon = radians(lon2 - lon1);
  float a = sin(dlat/2)*sin(dlat/2) +
            cos(radians(lat1)) * cos(radians(lat2)) * sin(dlon/2)*sin(dlon/2);
  return R * 2.0f * atan2(sqrt(a), sqrt(1.0f - a));
}

void getRegionCenter(float &lat, float &lon) {
  if      (strcmp(config.region, "NZ")         == 0) { lat = -41.3f; lon =  174.8f; }
  else if (strcmp(config.region, "Japan")      == 0) { lat =  36.5f; lon =  137.5f; }
  else if (strcmp(config.region, "China")      == 0) { lat =  35.0f; lon =  105.0f; }
  else if (strcmp(config.region, "California") == 0) { lat =  37.0f; lon = -119.5f; }
  else                                               { lat =   0.0f; lon =    0.0f; }
}

// Call this whenever a new earthquake is detected.
// Schedules P/S/Surface/Coda phases with compressed display timing that
// preserves the real velocity ratios (P:6 km/s, S:3.5 km/s, Surface:3 km/s).
void triggerSeismicEvent(float quakeLat, float quakeLon, float mag) {
  float cLat, cLon;
  getRegionCenter(cLat, cLon);
  float distKm = haversineKm(cLat, cLon, quakeLat, quakeLon);
  distKm = max(distKm, 10.0f);

  // Real travel times (seconds) based on standard seismic velocities
  float pTime    = distKm / 6.0f;
  float sTime    = distKm / 3.5f;
  float surfTime = distKm / 3.0f;

  // Compress to display time using log-scale so distant quakes don't wait
  // forever: log10(10km)≈1→2s, log10(100km)≈2→4s, log10(1000km)≈3→6s
  float displayP    = log10(distKm) * 2.0f;
  float displayS    = displayP * (sTime    / pTime);   // preserves real P:S ratio
  float displaySurf = displayP * (surfTime / pTime);   // preserves real P:Surf ratio

  // Amplitude: Richter-style scaled to pixel range (exaggerated for visual drama).
  float halfH   = (SEISMO_HEIGHT / 2) + 8;  // allows wave to reach panel edges on big quakes
  float rawAmp  = pow(10.0f, 0.8f * mag)        / distKm;
  float refAmp  = pow(10.0f, 0.8f * 4.0f)       / 150.0f;  // reference: M4 at 150km fills ~40%
  float scaled  = constrain((rawAmp / refAmp) * halfH * 1.6f, 7.0f, (float)halfH);

  seismoPAmp    = scaled * 0.50f;   // P-waves are smallest
  seismoSAmp    = scaled * 1.00f;   // S-waves intermediate
  seismoSurfAmp = scaled * 1.50f;   // Surface waves largest — can rail the display

  unsigned long now = millis();
  seismoPWaveStart = now + (unsigned long)(displayP    * 1000.0f);
  seismoSWaveStart = now + (unsigned long)(displayS    * 1000.0f);
  seismoSurfStart  = now + (unsigned long)(displaySurf * 1000.0f);
  seismoCodaStart  = seismoSurfStart  + 12000UL;  // Surface waves last 12 s
  seismoEventEnd   = seismoCodaStart  + 20000UL;  // Coda decays over 20 s
  seismoPhase = SEISMO_QUIET;  // Will advance automatically in animateSeismograph
}

void animateSeismograph() {
  unsigned long now = millis();

  // Advance phase based on scheduled arrival times
  if (seismoPWaveStart > 0) {
    if      (now >= seismoEventEnd)  { seismoPhase = SEISMO_QUIET; seismoPWaveStart = 0; }
    else if (now >= seismoCodaStart) { seismoPhase = SEISMO_CODA; }
    else if (now >= seismoSurfStart) { seismoPhase = SEISMO_SURFACE; }
    else if (now >= seismoSWaveStart){ seismoPhase = SEISMO_S_WAVE; }
    else if (now >= seismoPWaveStart){ seismoPhase = SEISMO_P_WAVE; }
    // else: still in pre-arrival quiet
  }

  // Absolute time in seconds — keeps sin() phase continuous across ticks
  float t = now / 1000.0f;
  int displacement = 0;

  switch (seismoPhase) {

    case SEISMO_QUIET:
      // Continuous microseismic tremor — two overlapping slow sinusoids + jitter
      displacement = (int)(3.5f * sin(2.0f * PI * 0.13f * t) +
                           2.0f * sin(2.0f * PI * 0.37f * t) +
                           (random(100) < 35 ? random(-2, 3) : 0));
      break;

    case SEISMO_P_WAVE: {
      // High-frequency (~6 Hz), bell-envelope, small amplitude
      float elapsed  = (now - seismoPWaveStart) / 1000.0f;
      float duration = max((float)(seismoSWaveStart - seismoPWaveStart) / 1000.0f, 0.5f);
      float env = sin(PI * elapsed / duration);  // Bell envelope
      displacement = (int)(seismoPAmp * env * sin(2.0f * PI * 6.0f * t));
      displacement += random(-1, 2);
      break;
    }

    case SEISMO_S_WAVE: {
      // Mid-frequency (~3 Hz), bell-envelope, larger amplitude
      float elapsed  = (now - seismoSWaveStart) / 1000.0f;
      float duration = max((float)(seismoSurfStart - seismoSWaveStart) / 1000.0f, 0.5f);
      float env = sin(PI * elapsed / duration);
      displacement = (int)(seismoSAmp * env * sin(2.0f * PI * 3.0f * t));
      displacement += random(-2, 3);
      break;
    }

    case SEISMO_SURFACE: {
      // Low-frequency (~0.4–0.55 Hz) Love+Rayleigh beating, largest amplitude
      float elapsed  = (now - seismoSurfStart) / 1000.0f;
      float duration = max((float)(seismoCodaStart - seismoSurfStart) / 1000.0f, 0.5f);
      float env = sin(PI * elapsed / duration);
      displacement = (int)(seismoSurfAmp * env *
                           (0.65f * sin(2.0f * PI * 0.40f * t) +
                            0.35f * sin(2.0f * PI * 0.55f * t)));
      break;
    }

    case SEISMO_CODA: {
      // Exponential decay — energy scattered back from crustal heterogeneities
      float elapsed    = (now - seismoCodaStart) / 1000.0f;
      float totalCoda  = max((float)(seismoEventEnd - seismoCodaStart) / 1000.0f, 1.0f);
      float decay      = exp(-3.0f * elapsed / totalCoda);
      displacement = (int)(seismoSurfAmp * decay * sin(2.0f * PI * 1.2f * t));
      if (decay > 0.1f) displacement += random(-2, 3);
      break;
    }
  }

  int newY = SEISMO_CENTER_Y - displacement;  // TFT Y-axis is inverted
  newY = constrain(newY, SEISMO_Y, SEISMO_Y + SEISMO_HEIGHT);

  // Draw 2-pixel-wide line segment (vintage feel)
  for (int dx = 0; dx < 2; dx++) {
    tft.drawLine(seismoX + dx, seismoLastY, seismoX + 1 + dx, newY, currentTheme.seismoLine);
  }

  // Erase strip 3 pixels ahead to create the scrolling window effect
  int eraseX = SEISMO_LINE_X + (seismoX - SEISMO_LINE_X + 3) % SEISMO_LINE_W;
  tft.fillRect(eraseX, SEISMO_Y, 2, SEISMO_HEIGHT, currentTheme.background);

  // Restore grid texture in erased strip
  for (int y = SEISMO_Y; y <= SEISMO_Y + SEISMO_HEIGHT; y += 10) {
    for (int x = eraseX; x < eraseX + 2; x++) {
      if ((x - SEISMO_LINE_X) % 8 == 0 && x >= SEISMO_LINE_X && x < SEISMO_LINE_X + SEISMO_LINE_W)
        tft.drawPixel(x, y, currentTheme.seismoGrid);
    }
  }

  seismoLastY = newY;
  seismoX++;
  if (seismoX >= SEISMO_LINE_X + SEISMO_LINE_W) seismoX = SEISMO_LINE_X;
}


// ═══════════════════════════════════════════════════════════════════════════
// MAP DRAWING HELPERS
// ═══════════════════════════════════════════════════════════════════════════

// Scanline polygon fill — pts[][0]=lat, pts[][1]=lon, max 100 points.
// Graticule drawn before land fill will be erased only over land (correct).
void fillMapPolygon(const float pts[][2], int n, uint16_t color) {
  static int16_t sx[100], sy[100];
  if (n > 100) n = 100;
  int yMin = MAP_Y + MAP_HEIGHT, yMax = MAP_Y;
  for (int i = 0; i < n; i++) {
    sx[i] = (int16_t)constrain(mapLonToScreen(pts[i][1]), MAP_X, MAP_X + MAP_WIDTH);
    sy[i] = (int16_t)constrain(mapLatToScreen(pts[i][0]), MAP_Y, MAP_Y + MAP_HEIGHT);
    if (sy[i] < yMin) yMin = sy[i];
    if (sy[i] > yMax) yMax = sy[i];
  }
  static int16_t xs[50];
  for (int y = yMin; y <= yMax; y++) {
    int xc = 0;
    for (int i = 0, j = n - 1; i < n; j = i++) {
      if (((sy[i] <= y) && (sy[j] > y)) || ((sy[j] <= y) && (sy[i] > y))) {
        if (xc < 50)
          xs[xc++] = sx[i] + (int16_t)((float)(y - sy[i]) / (float)(sy[j] - sy[i]) * (float)(sx[j] - sx[i]));
      }
    }
    for (int a = 0; a < xc - 1; a++)          // sort (tiny array — bubble ok)
      for (int b = a + 1; b < xc; b++)
        if (xs[a] > xs[b]) { int16_t t = xs[a]; xs[a] = xs[b]; xs[b] = t; }
    for (int a = 0; a < xc - 1; a += 2) {
      int x0 = max((int)xs[a],     MAP_X);
      int x1 = min((int)xs[a + 1], MAP_X + MAP_WIDTH - 1);
      if (x0 <= x1) tft.drawFastHLine(x0, y, x1 - x0 + 1, color);
    }
  }
}

// Dotted lat/lon graticule — step size adapts to region span.
// Draw this BEFORE land fill so grid is naturally hidden over land.
void drawMapGraticule(uint16_t color) {
  RegionBounds b = getRegionBounds(config.region);
  float latSpan = b.latMax - b.latMin;
  float lonSpan = b.lonMax - b.lonMin;
  float latStep = latSpan > 60 ? 20.0f : latSpan > 20 ? 10.0f : 5.0f;
  float lonStep = lonSpan > 120 ? 30.0f : lonSpan > 40 ? 10.0f : 5.0f;
  for (float lat = ceilf(b.latMin / latStep) * latStep; lat < b.latMax; lat += latStep) {
    int y = mapLatToScreen(lat);
    if (y < MAP_Y || y > MAP_Y + MAP_HEIGHT) continue;
    for (int x = MAP_X; x < MAP_X + MAP_WIDTH; x += 4) tft.drawPixel(x, y, color);
  }
  for (float lon = ceilf(b.lonMin / lonStep) * lonStep; lon < b.lonMax; lon += lonStep) {
    int x = mapLonToScreen(lon);
    if (x < MAP_X || x > MAP_X + MAP_WIDTH) continue;
    for (int y = MAP_Y; y < MAP_Y + MAP_HEIGHT; y += 4) tft.drawPixel(x, y, color);
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// REGIONAL MAPS - NEW ZEALAND
// ═══════════════════════════════════════════════════════════════════════════

void drawNZMap() {
  // North Island - clockwise from Cape Reinga
  const float northIsland[][2] = {
    {-34.42, 172.68},  // Cape Reinga
    {-34.45, 173.05},  // North Cape
    {-34.87, 173.38},  // Karikari Peninsula
    {-35.22, 174.05},  // Bay of Islands
    {-35.83, 174.55},  // Whangarei Heads / Bream Head
    {-36.30, 174.80},  // Leigh coast
    {-36.63, 174.83},  // Whangaparaoa
    {-36.84, 174.80},  // Auckland east
    {-37.05, 175.30},  // Firth of Thames
    {-36.50, 175.35},  // Coromandel tip
    {-36.85, 175.72},  // Whitianga
    {-37.22, 175.92},  // Whangamata
    {-37.63, 176.18},  // Tauranga / Mt Maunganui
    {-37.97, 177.00},  // Whakatane
    {-37.69, 178.55},  // East Cape
    {-38.67, 178.02},  // Gisborne
    {-39.10, 177.85},  // Mahia Peninsula
    {-39.48, 176.92},  // Napier
    {-39.65, 177.10},  // Cape Kidnappers
    {-40.35, 176.62},  // Porangahau
    {-40.90, 176.20},  // Castlepoint
    {-41.45, 175.40},  // Cape Palliser
    {-41.30, 174.78},  // Wellington
    {-41.15, 174.85},  // Porirua
    {-40.88, 175.00},  // Kapiti coast
    {-40.30, 175.00},  // Levin
    {-39.93, 175.05},  // Whanganui
    {-39.50, 174.25},  // South Taranaki
    {-39.28, 173.75},  // Cape Egmont
    {-39.05, 174.05},  // New Plymouth
    {-38.70, 174.55},  // North Taranaki
    {-38.30, 174.65},  // Mokau
    {-37.80, 174.75},  // Raglan
    {-37.40, 174.65},  // Port Waikato
    {-37.02, 174.52},  // Manukau
    {-36.85, 174.42},  // Auckland west
    {-36.40, 174.13},  // Kaipara
    {-35.93, 173.85},  // Dargaville
    {-35.55, 173.45},  // Maunganui Bluff
    {-35.20, 173.10},  // Hokianga
    {-34.75, 172.78},  // 90 Mile Beach
    {-34.42, 172.68},  // Close
  };

  // South Island - clockwise from Farewell Spit
  const float southIsland[][2] = {
    {-40.50, 172.68},  // Farewell Spit
    {-40.65, 172.90},  // Golden Bay
    {-40.78, 172.62},  // Takaka
    {-40.95, 173.00},  // Abel Tasman
    {-41.10, 173.45},  // Nelson
    {-41.10, 174.03},  // Pelorus Sound
    {-41.22, 174.15},  // Queen Charlotte Sound
    {-41.35, 174.30},  // Cape Jackson
    {-41.72, 174.18},  // Cape Campbell
    {-42.15, 173.88},  // North of Kaikoura
    {-42.42, 173.68},  // Kaikoura
    {-42.85, 173.45},  // Cheviot coast
    {-43.30, 172.88},  // Pegasus Bay
    {-43.55, 172.75},  // North Banks Peninsula
    {-43.62, 172.95},  // Banks Peninsula NE
    {-43.83, 173.08},  // Akaroa head
    {-43.82, 172.65},  // Banks Peninsula SW
    {-44.10, 172.00},  // Ashburton coast
    {-44.38, 171.50},  // Timaru
    {-44.72, 171.18},  // South Canterbury
    {-45.10, 170.98},  // Oamaru
    {-45.52, 170.68},  // North Dunedin
    {-45.78, 170.72},  // Otago Peninsula N
    {-45.88, 170.73},  // Otago Peninsula tip
    {-45.92, 170.50},  // South of Otago
    {-46.22, 170.05},  // Balclutha
    {-46.45, 169.45},  // Catlins
    {-46.67, 168.36},  // Slope Point
    {-46.60, 168.00},  // Riverton
    {-46.40, 167.55},  // Te Waewae Bay
    {-46.15, 166.60},  // Puysegur Point
    {-45.75, 166.50},  // Dusky Sound
    {-45.30, 166.85},  // Doubtful Sound
    {-44.63, 167.85},  // Milford Sound
    {-44.15, 168.08},  // Martins Bay
    {-43.98, 168.62},  // Jackson Bay
    {-43.88, 169.05},  // Haast
    {-43.47, 170.18},  // Fox Glacier coast
    {-42.72, 170.97},  // Hokitika
    {-42.45, 171.21},  // Greymouth
    {-41.75, 171.60},  // Westport
    {-41.25, 172.10},  // Karamea
    {-40.78, 172.15},  // Little Wanganui
    {-40.50, 172.68},  // Close
  };

  // Stewart Island
  const float stewartIsland[][2] = {
    {-46.65, 167.80}, {-46.72, 168.05}, {-46.82, 168.22},
    {-46.97, 168.15}, {-47.05, 167.80}, {-46.95, 167.45},
    {-46.80, 167.40}, {-46.68, 167.55}, {-46.65, 167.80},
  };

  int northPts   = sizeof(northIsland)   / sizeof(northIsland[0]);
  int southPts   = sizeof(southIsland)   / sizeof(southIsland[0]);
  int stewartPts = sizeof(stewartIsland) / sizeof(stewartIsland[0]);

  // Land fill
  fillMapPolygon(northIsland,   northPts,   currentTheme.mapLand);
  fillMapPolygon(southIsland,   southPts,   currentTheme.mapLand);
  fillMapPolygon(stewartIsland, stewartPts, currentTheme.mapLand);

  // Coastline outlines (crisp edges over fill)
  for (int i = 0; i < northPts - 1; i++)
    tft.drawLine(mapLonToScreen(northIsland[i][1]),   mapLatToScreen(northIsland[i][0]),
                 mapLonToScreen(northIsland[i+1][1]), mapLatToScreen(northIsland[i+1][0]), currentTheme.mapOutline);
  for (int i = 0; i < southPts - 1; i++)
    tft.drawLine(mapLonToScreen(southIsland[i][1]),   mapLatToScreen(southIsland[i][0]),
                 mapLonToScreen(southIsland[i+1][1]), mapLatToScreen(southIsland[i+1][0]), currentTheme.mapOutline);
  for (int i = 0; i < stewartPts - 1; i++)
    tft.drawLine(mapLonToScreen(stewartIsland[i][1]),   mapLatToScreen(stewartIsland[i][0]),
                 mapLonToScreen(stewartIsland[i+1][1]), mapLatToScreen(stewartIsland[i+1][0]), currentTheme.mapOutline);

  if (config.showCityDots) {
    tft.fillCircle(mapLonToScreen(174.78), mapLatToScreen(-41.28), 2, currentTheme.mapCity); // Wellington
    tft.fillCircle(mapLonToScreen(174.76), mapLatToScreen(-36.85), 2, currentTheme.mapCity); // Auckland
    tft.fillCircle(mapLonToScreen(172.64), mapLatToScreen(-43.53), 2, currentTheme.mapCity); // Christchurch
    tft.fillCircle(mapLonToScreen(170.50), mapLatToScreen(-45.87), 2, currentTheme.mapCity); // Dunedin
    tft.fillCircle(mapLonToScreen(176.24), mapLatToScreen(-37.68), 2, currentTheme.mapCity); // Tauranga
    tft.fillCircle(mapLonToScreen(176.92), mapLatToScreen(-39.48), 2, currentTheme.mapCity); // Napier
    tft.fillCircle(mapLonToScreen(168.35), mapLatToScreen(-45.03), 2, currentTheme.mapCity); // Queenstown
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// REGIONAL MAPS - JAPAN
// ═══════════════════════════════════════════════════════════════════════════

void drawJapanMap() {
  // Hokkaido - clockwise from northwest
  const float hokkaido[][2] = {
    {45.52, 141.94},  // Cape Soya (north tip)
    {45.33, 142.45},  // Okhotsk NW coast
    {44.92, 143.18},  // Okhotsk coast
    {44.38, 143.95},  // Shiretoko west
    {44.02, 144.72},  // Shiretoko Peninsula tip
    {43.38, 145.55},  // Nemuro / Cape Nosappu
    {43.05, 145.10},  // Kushiro
    {42.60, 144.30},  // Tokachi coast
    {42.30, 143.35},  // Hidaka
    {41.93, 143.24},  // Cape Erimo
    {42.10, 142.30},  // Hidaka west
    {42.05, 141.30},  // Tomakomai
    {41.77, 140.73},  // Hakodate
    {42.00, 140.05},  // Matsumae
    {42.50, 140.25},  // SW coast
    {43.07, 140.35},  // Otaru
    {43.33, 140.33},  // Cape Kamui
    {43.90, 141.15},  // Rumoi
    {44.40, 141.70},  // Teshio
    {44.92, 141.75},  // Wakkanai south
    {45.42, 141.67},  // Wakkanai
    {45.52, 141.94},  // Close
  };

  // Honshu - clockwise from Aomori north
  const float honshu[][2] = {
    {41.55, 140.92},  // Tsugaru NE
    {41.48, 141.28},  // Shimokita Peninsula east
    {41.00, 141.50},  // Shimokita base
    {40.53, 141.60},  // Hachinohe
    {39.64, 141.97},  // Miyako (ria coast)
    {39.10, 141.88},  // Kamaishi
    {38.27, 141.03},  // Sendai coast
    {37.80, 140.95},  // Fukushima
    {36.80, 140.85},  // Ibaraki
    {35.73, 140.82},  // Choshi
    {34.95, 139.88},  // Boso Peninsula tip
    {35.33, 139.72},  // Tokyo Bay east
    {35.10, 139.10},  // Odawara
    {34.60, 138.95},  // Izu Peninsula tip
    {34.70, 138.22},  // Suruga Bay
    {34.63, 137.00},  // Enshu coast
    {34.58, 136.85},  // Ise Bay east
    {34.22, 136.15},  // Kii Peninsula east
    {33.45, 135.77},  // Cape Shionomisaki (south tip)
    {33.90, 135.10},  // Kii west
    {34.38, 135.18},  // Osaka Bay
    {34.65, 134.68},  // Akashi
    {34.40, 133.85},  // Okayama
    {34.28, 133.15},  // Onomichi
    {34.10, 132.50},  // Hiroshima
    {34.00, 131.95},  // Iwakuni
    {33.95, 130.95},  // Shimonoseki
    {34.30, 131.38},  // Hagi (Sea of Japan)
    {35.12, 132.65},  // Izumo
    {35.60, 134.15},  // Tottori / Kinosaki
    {35.75, 135.35},  // Maizuru
    {36.07, 136.05},  // Tsuruga
    {36.55, 136.62},  // Kanazawa
    {37.43, 137.30},  // Noto Peninsula tip
    {36.80, 137.15},  // Toyama Bay
    {37.20, 138.30},  // Joetsu
    {37.92, 139.05},  // Niigata
    {38.93, 139.85},  // Sakata
    {39.73, 139.90},  // Akita
    {40.55, 139.95},  // Fukaura
    {41.00, 140.25},  // Tsugaru west
    {41.55, 140.25},  // Tsugaru NW
    {41.55, 140.92},  // Close
  };

  // Shikoku - clockwise from NE
  const float shikoku[][2] = {
    {34.25, 134.65},  // Naruto
    {33.92, 134.65},  // East coast
    {33.25, 134.18},  // Cape Muroto
    {33.00, 133.55},  // South coast
    {32.72, 133.02},  // Cape Ashizuri
    {33.10, 132.48},  // West coast
    {33.35, 132.02},  // Cape Sada
    {33.85, 132.72},  // Matsuyama
    {34.35, 133.60},  // Takamatsu
    {34.25, 134.65},  // Close
  };

  // Kyushu - clockwise from NE
  const float kyushu[][2] = {
    {33.95, 131.05},  // Kitakyushu
    {33.55, 131.65},  // Oita
    {33.28, 131.60},  // Saiki
    {32.75, 131.88},  // Nobeoka
    {31.93, 131.45},  // Miyazaki
    {31.40, 131.10},  // Shibushi
    {31.00, 130.67},  // Cape Sata (south)
    {31.25, 130.22},  // Ibusuki
    {31.80, 130.30},  // Kagoshima
    {32.18, 130.30},  // Amakusa
    {32.58, 129.85},  // Nagasaki
    {33.18, 129.72},  // Sasebo
    {33.55, 130.00},  // Karatsu
    {33.62, 130.40},  // Saga
    {33.88, 130.40},  // Fukuoka
    {33.95, 131.05},  // Close
  };

  int hokkaidoPts = sizeof(hokkaido) / sizeof(hokkaido[0]);
  int honshuPts   = sizeof(honshu)   / sizeof(honshu[0]);
  int shikokuPts  = sizeof(shikoku)  / sizeof(shikoku[0]);
  int kyushuPts   = sizeof(kyushu)   / sizeof(kyushu[0]);

  // Land fill
  fillMapPolygon(hokkaido, hokkaidoPts, currentTheme.mapLand);
  fillMapPolygon(honshu,   honshuPts,   currentTheme.mapLand);
  fillMapPolygon(shikoku,  shikokuPts,  currentTheme.mapLand);
  fillMapPolygon(kyushu,   kyushuPts,   currentTheme.mapLand);

  // Outlines
  for (int i = 0; i < hokkaidoPts - 1; i++)
    tft.drawLine(mapLonToScreen(hokkaido[i][1]),   mapLatToScreen(hokkaido[i][0]),
                 mapLonToScreen(hokkaido[i+1][1]), mapLatToScreen(hokkaido[i+1][0]), currentTheme.mapOutline);
  for (int i = 0; i < honshuPts - 1; i++)
    tft.drawLine(mapLonToScreen(honshu[i][1]),   mapLatToScreen(honshu[i][0]),
                 mapLonToScreen(honshu[i+1][1]), mapLatToScreen(honshu[i+1][0]), currentTheme.mapOutline);
  for (int i = 0; i < shikokuPts - 1; i++)
    tft.drawLine(mapLonToScreen(shikoku[i][1]),   mapLatToScreen(shikoku[i][0]),
                 mapLonToScreen(shikoku[i+1][1]), mapLatToScreen(shikoku[i+1][0]), currentTheme.mapOutline);
  for (int i = 0; i < kyushuPts - 1; i++)
    tft.drawLine(mapLonToScreen(kyushu[i][1]),   mapLatToScreen(kyushu[i][0]),
                 mapLonToScreen(kyushu[i+1][1]), mapLatToScreen(kyushu[i+1][0]), currentTheme.mapOutline);

  // Ryukyu Islands chain (Kyushu → Okinawa → Taiwan direction)
  const float ryukyu[][2] = {
    {31.60, 130.55}, {30.48, 130.20}, {29.88, 129.72}, {28.45, 129.60},
    {27.85, 128.25}, {26.62, 128.00}, {26.22, 127.68}, {25.80, 125.28},
    {24.48, 124.08}, {24.00, 123.60}, {23.00, 122.50},
  };
  for (int i = 0; i < 10; i++)
    tft.drawLine(mapLonToScreen(ryukyu[i][1]),   mapLatToScreen(ryukyu[i][0]),
                 mapLonToScreen(ryukyu[i+1][1]), mapLatToScreen(ryukyu[i+1][0]), currentTheme.mapOutline);

  // Japan Trench — subduction zone off the Pacific coast of Honshu (dotted cyan)
  const float japanTrench[][2] = {
    {40.50, 143.50}, {39.00, 143.80}, {37.50, 143.50},
    {36.00, 142.80}, {34.50, 141.80}, {33.00, 141.00},
  };
  // Nankai Trough — SW of Honshu/Shikoku (dotted cyan)
  const float nankaiTrough[][2] = {
    {33.80, 137.00}, {33.20, 136.20}, {32.60, 135.20},
    {31.80, 133.50}, {31.20, 132.00}, {30.80, 130.80},
  };
  auto drawDotted = [](const float pts[][2], int n, uint16_t col) {
    for (int i = 0; i < n - 1; i++) {
      int x0 = mapLonToScreen(pts[i][1]),   y0 = mapLatToScreen(pts[i][0]);
      int x1 = mapLonToScreen(pts[i+1][1]), y1 = mapLatToScreen(pts[i+1][0]);
      int steps = max(abs(x1-x0), abs(y1-y0));
      for (int s = 0; s <= steps; s += 2) {
        int px = x0 + s * (x1-x0) / max(steps,1);
        int py = y0 + s * (y1-y0) / max(steps,1);
        if (px >= MAP_X && px <= MAP_X+MAP_WIDTH && py >= MAP_Y && py <= MAP_Y+MAP_HEIGHT)
          tft.drawPixel(px, py, col);
      }
    }
  };
  drawDotted(japanTrench,  6, 0x3C14);   // Muted teal
  drawDotted(nankaiTrough, 6, 0x3C14);   // Muted teal

  if (config.showCityDots) {
    tft.fillCircle(mapLonToScreen(139.69), mapLatToScreen(35.68), 2, currentTheme.mapCity); // Tokyo
    tft.fillCircle(mapLonToScreen(135.50), mapLatToScreen(34.69), 2, currentTheme.mapCity); // Osaka
    tft.fillCircle(mapLonToScreen(130.42), mapLatToScreen(33.59), 2, currentTheme.mapCity); // Fukuoka
    tft.fillCircle(mapLonToScreen(141.35), mapLatToScreen(43.07), 2, currentTheme.mapCity); // Sapporo
    tft.fillCircle(mapLonToScreen(140.87), mapLatToScreen(38.27), 2, currentTheme.mapCity); // Sendai
    tft.fillCircle(mapLonToScreen(136.88), mapLatToScreen(35.17), 2, currentTheme.mapCity); // Nagoya
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// REGIONAL MAPS - TAIWAN
// ═══════════════════════════════════════════════════════════════════════════

void drawChinaMap() {
  // China border + coastline - clockwise from northeast
  const float china[][2] = {
    // Northeast (Russia border along Amur/Heilongjiang)
    {53.30, 125.00},  // Northern Heilongjiang
    {48.50, 135.00},  // Khabarovsk bend
    {45.50, 133.00},  // Ussuri area
    {43.00, 131.00},  // Vladivostok area
    // Korean border
    {42.00, 130.50},  // Tumen River
    {41.00, 126.50},  // Yalu River
    {40.00, 124.50},  // Dandong
    // Liaodong Peninsula & Bohai Bay
    {39.00, 121.80},  // Dalian / Liaodong tip
    {40.00, 121.60},  // Liaodong NE coast
    {39.00, 118.50},  // Tianjin / Bohai coast
    // Shandong Peninsula
    {37.40, 122.50},  // Weihai (Shandong tip)
    {36.10, 120.40},  // Qingdao
    {35.00, 119.50},  // Jiangsu north coast
    // East coast (Yangtze to South China Sea)
    {34.00, 120.00},  // Jiangsu coast
    {32.00, 121.80},  // Shanghai / Yangtze mouth
    {30.50, 122.00},  // Hangzhou Bay
    {28.50, 121.50},  // Wenzhou
    {26.00, 119.80},  // Fuzhou
    {24.50, 118.20},  // Xiamen
    {23.00, 116.50},  // Shantou
    {22.50, 114.20},  // Hong Kong
    {21.50, 110.50},  // Zhanjiang / Leizhou
    // South coast & Vietnam border
    {20.00, 110.00},  // Hainan Strait
    {21.50, 108.00},  // Vietnam border coast
    {22.50, 106.50},  // Guangxi / Vietnam
    // Southwest borders (Vietnam, Laos, Myanmar)
    {22.00, 101.00},  // Yunnan south
    {21.20, 100.00},  // Myanmar border
    // India / Nepal / Tibet border
    {27.50, 97.50},   // Myanmar/Tibet junction
    {28.50, 97.00},   // Arunachal Pradesh area
    {28.00, 87.00},   // Nepal border / Everest
    {32.00, 79.00},   // Western Tibet
    // Xinjiang west
    {36.00, 76.00},   // Karakoram Pass
    {39.50, 73.50},   // Tajikistan border
    // Northwest borders (Kazakhstan, Mongolia)
    {44.00, 80.00},   // Kazakhstan
    {47.50, 87.00},   // Altai Mountains
    // Mongolia border
    {49.50, 89.00},   // Mongolia west
    {49.00, 97.00},   // Mongolia central
    {46.00, 105.00},  // Gobi
    {42.00, 111.50},  // Inner Mongolia
    {42.50, 117.00},  // Hebei north
    {45.00, 119.00},  // Inner Mongolia east
    {47.00, 120.00},  // Hulunbuir
    {50.00, 119.80},  // North border
    {53.30, 125.00},  // Close
  };

  int pts = sizeof(china) / sizeof(china[0]);

  // Land fill
  fillMapPolygon(china, pts, currentTheme.mapLand);

  // Outline
  for (int i = 0; i < pts - 1; i++)
    tft.drawLine(mapLonToScreen(china[i][1]),   mapLatToScreen(china[i][0]),
                 mapLonToScreen(china[i+1][1]), mapLatToScreen(china[i+1][0]), currentTheme.mapOutline);

  // Taiwan
  const float taiwan[][2] = {
    {25.30, 121.55}, {24.00, 121.95}, {22.00, 120.85},
    {21.90, 120.50}, {22.80, 120.15}, {24.50, 120.85}, {25.30, 121.55},
  };
  fillMapPolygon(taiwan, 7, currentTheme.mapLand);
  for (int i = 0; i < 6; i++)
    tft.drawLine(mapLonToScreen(taiwan[i][1]),   mapLatToScreen(taiwan[i][0]),
                 mapLonToScreen(taiwan[i+1][1]), mapLatToScreen(taiwan[i+1][0]), currentTheme.mapOutline);

  // Hainan Island
  const float hainan[][2] = {
    {20.15, 110.10}, {19.60, 111.00}, {18.30, 110.55},
    {18.25, 109.60}, {19.20, 108.65}, {20.05, 109.55}, {20.15, 110.10},
  };
  fillMapPolygon(hainan, 7, currentTheme.mapLand);
  for (int i = 0; i < 6; i++)
    tft.drawLine(mapLonToScreen(hainan[i][1]),   mapLatToScreen(hainan[i][0]),
                 mapLonToScreen(hainan[i+1][1]), mapLatToScreen(hainan[i+1][0]), currentTheme.mapOutline);

  if (config.showCityDots) {
    tft.fillCircle(mapLonToScreen(116.40), mapLatToScreen(39.90), 2, currentTheme.mapCity); // Beijing
    tft.fillCircle(mapLonToScreen(121.47), mapLatToScreen(31.23), 2, currentTheme.mapCity); // Shanghai
    tft.fillCircle(mapLonToScreen(113.26), mapLatToScreen(23.13), 2, currentTheme.mapCity); // Guangzhou
    tft.fillCircle(mapLonToScreen(104.07), mapLatToScreen(30.67), 2, currentTheme.mapCity); // Chengdu
    tft.fillCircle(mapLonToScreen(106.55), mapLatToScreen(29.57), 2, currentTheme.mapCity); // Chongqing
    tft.fillCircle(mapLonToScreen(87.60),  mapLatToScreen(43.80), 2, currentTheme.mapCity); // Urumqi
    tft.fillCircle(mapLonToScreen(91.18),  mapLatToScreen(29.65), 2, currentTheme.mapCity); // Lhasa
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// REGIONAL MAPS - CALIFORNIA
// ═══════════════════════════════════════════════════════════════════════════

void drawCaliforniaMap() {
  // California coastline + state border - clockwise from NW coast
  const float california[][2] = {
    // Oregon border / north coast
    {42.00, -124.21},  // Oregon border at coast
    {41.75, -124.18},  // Del Norte coast
    {41.10, -124.15},  // Crescent City
    // Humboldt / Mendocino
    {40.80, -124.18},  // Eureka area
    {40.44, -124.40},  // Cape Mendocino
    {40.02, -124.08},  // Mendocino County
    {39.35, -123.80},  // Fort Bragg area
    {38.95, -123.65},  // Point Arena
    // Marin / SF Bay
    {38.35, -123.05},  // Point Reyes
    {37.82, -122.52},  // Golden Gate
    {37.55, -122.50},  // Pacifica / SF south
    // Half Moon Bay to Santa Cruz
    {37.18, -122.38},  // Half Moon Bay
    {36.97, -122.00},  // Santa Cruz
    // Monterey Bay
    {36.60, -121.90},  // Monterey
    {36.55, -121.95},  // Carmel
    // Big Sur coast
    {36.25, -121.80},  // Big Sur north
    {35.89, -121.48},  // Big Sur south
    // Central Coast
    {35.42, -120.90},  // San Luis Obispo
    {35.17, -120.72},  // Pismo Beach
    {34.92, -120.62},  // Point Sal
    // Point Conception (coast turns east!)
    {34.45, -120.47},  // Point Conception
    // Santa Barbara / Ventura
    {34.40, -119.85},  // Santa Barbara
    {34.05, -119.18},  // Oxnard
    // Los Angeles
    {33.95, -118.80},  // Malibu
    {33.72, -118.40},  // Santa Monica Bay
    {33.72, -118.28},  // LA / Long Beach
    // Orange County / San Diego
    {33.38, -117.60},  // San Clemente
    {33.05, -117.30},  // Oceanside
    {32.72, -117.17},  // San Diego
    {32.53, -117.12},  // Tijuana border coast
    // Mexico border east
    {32.53, -117.12},
    {32.54, -116.10},  // Tecate border
    {32.72, -114.72},  // Yuma / Colorado River
    // Arizona / Nevada border (east side)
    {33.00, -114.63},  // Parker area
    {34.00, -114.43},  // Needles area
    {35.00, -114.63},  // Below Las Vegas
    {36.00, -114.75},  // Nevada corner
    {36.20, -115.90},  // Nevada border jog
    {37.00, -117.00},  // Death Valley east
    {38.00, -118.00},  // Mono Lake area
    {39.00, -120.00},  // Lake Tahoe
    {40.00, -120.00},  // NE California
    {41.00, -120.00},  // Modoc area
    {42.00, -120.00},  // Oregon border east
    // Oregon border west
    {42.00, -121.50},
    {42.00, -123.00},
    {42.00, -124.21},  // Close
  };

  int pts = sizeof(california) / sizeof(california[0]);

  // Land fill
  fillMapPolygon(california, pts, currentTheme.mapLand);

  // Outline
  for (int i = 0; i < pts - 1; i++)
    tft.drawLine(mapLonToScreen(california[i][1]),   mapLatToScreen(california[i][0]),
                 mapLonToScreen(california[i+1][1]), mapLatToScreen(california[i+1][0]), currentTheme.mapOutline);

  // Cascadia Subduction Zone — offshore, NW California to Oregon border (dotted cyan)
  const float cascadia[][2] = {
    {42.00, -125.20}, {41.20, -125.00}, {40.40, -124.75},
    {39.60, -124.45}, {38.60, -123.90},
  };
  for (int i = 0; i < 4; i++) {
    int x0 = mapLonToScreen(cascadia[i][1]),   y0 = mapLatToScreen(cascadia[i][0]);
    int x1 = mapLonToScreen(cascadia[i+1][1]), y1 = mapLatToScreen(cascadia[i+1][0]);
    int steps = max(abs(x1-x0), abs(y1-y0));
    for (int s = 0; s <= steps; s += 2) {
      int px = x0 + s*(x1-x0)/max(steps,1), py = y0 + s*(y1-y0)/max(steps,1);
      if (px >= MAP_X && px <= MAP_X+MAP_WIDTH && py >= MAP_Y && py <= MAP_Y+MAP_HEIGHT)
        tft.drawPixel(px, py, 0x3C14);   // Muted teal
    }
  }

  // San Andreas Fault
  const float fault[][2] = {
    {40.30, -124.30}, {39.80, -123.70}, {39.20, -123.30},
    {38.50, -122.95}, {38.00, -122.55}, {37.70, -122.25},
    {37.40, -122.10}, {37.00, -121.85},
    {36.50, -121.20}, {36.00, -120.60}, {35.50, -120.10}, {35.00, -119.60},
    {34.80, -119.20}, {34.60, -118.85}, {34.40, -118.55},
    {34.20, -118.35}, {34.00, -118.20}, {33.80, -118.00},
    {33.60, -117.70}, {33.40, -117.35}, {33.20, -116.95},
    {33.00, -116.50}, {32.80, -116.10}, {32.60, -115.70},
  };

  int faultPts = sizeof(fault) / sizeof(fault[0]);
  for (int i = 0; i < faultPts - 1; i++) {
    tft.drawLine(mapLonToScreen(fault[i][1]), mapLatToScreen(fault[i][0]),
                 mapLonToScreen(fault[i+1][1]), mapLatToScreen(fault[i+1][0]), 0xCB0B);   // Dusty coral
  }

  if (config.showCityDots) {
    tft.fillCircle(mapLonToScreen(-122.42), mapLatToScreen(37.77), 2, currentTheme.mapCity); // San Francisco
    tft.fillCircle(mapLonToScreen(-118.24), mapLatToScreen(34.05), 2, currentTheme.mapCity); // Los Angeles
    tft.fillCircle(mapLonToScreen(-117.16), mapLatToScreen(32.72), 2, currentTheme.mapCity); // San Diego
    tft.fillCircle(mapLonToScreen(-121.49), mapLatToScreen(38.58), 2, currentTheme.mapCity); // Sacramento
    tft.fillCircle(mapLonToScreen(-119.82), mapLatToScreen(36.75), 2, currentTheme.mapCity); // Fresno
    tft.fillCircle(mapLonToScreen(-121.90), mapLatToScreen(37.34), 2, currentTheme.mapCity); // San Jose
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// REGIONAL MAPS - GLOBAL
// ═══════════════════════════════════════════════════════════════════════════

void drawGlobalMap() {
  // Flat equirectangular projection using BOUNDS_GLOBAL (-60..75 lat, -180..180 lon).
  // mapLonToScreen / mapLatToScreen work correctly — earthquake markers align with land.

  // ── Continent outlines (lat/lon, closed polygons) ───────────────────────

  // North America
  static const float na[][2] = {
    {71,-141},{60,-147},{57,-135},{50,-128},{42,-124},{37,-122},
    {29,-110},{22,-105},{15,-92},{9,-79},
    {10,-63},{17,-62},{25,-80},{35,-76},{41,-71},{45,-64},
    {48,-53},{58,-65},{63,-64},{68,-74},{73,-88},{71,-141},
  };
  // South America
  static const float sa[][2] = {
    {9,-79},{5,-77},{-5,-81},{-18,-72},{-33,-72},
    {-42,-64},{-55,-68},{-55,-65},{-42,-65},
    {-23,-44},{-5,-35},{5,-52},{8,-62},{9,-79},
  };
  // Europe + Scandinavia (rough blob)
  static const float eu[][2] = {
    {36,-9},{36,28},{42,36},{48,38},{55,28},
    {58,22},{65,14},{57,8},{51,2},{45,0},{36,-9},
  };
  // Africa
  static const float af[][2] = {
    {37,11},{37,37},{15,42},{0,42},{-10,38},
    {-25,34},{-35,25},{-35,18},{-10,13},
    {5,2},{15,-17},{22,-17},{30,-17},{37,11},
  };
  // Asia (main mass — Arabia + India + SE Asia + Siberia)
  static const float as[][2] = {
    {73,60},{73,140},{68,170},{50,142},{35,140},
    {22,120},{10,104},{5,100},{10,79},
    {20,70},{28,62},{36,36},{42,46},{48,58},{60,60},{73,60},
  };
  // Australia
  static const float au[][2] = {
    {-17,122},{-25,113},{-34,115},{-38,140},
    {-38,148},{-25,153},{-18,146},{-10,142},{-17,122},
  };
  // Greenland
  static const float gr[][2] = {
    {76,-18},{72,-22},{65,-40},{60,-44},{60,-46},
    {65,-50},{72,-52},{76,-40},{83,-30},{76,-18},
  };

  // Fill land, then outline
  fillMapPolygon(na, 22, currentTheme.mapLand);
  fillMapPolygon(sa, 14, currentTheme.mapLand);
  fillMapPolygon(eu, 11, currentTheme.mapLand);
  fillMapPolygon(af, 14, currentTheme.mapLand);
  fillMapPolygon(as, 16, currentTheme.mapLand);
  fillMapPolygon(au,  9, currentTheme.mapLand);
  fillMapPolygon(gr, 10, currentTheme.mapLand);

  auto drawPoly = [](const float pts[][2], int n, uint16_t col) {
    for (int i = 0; i < n - 1; i++)
      tft.drawLine(mapLonToScreen(pts[i][1]),   mapLatToScreen(pts[i][0]),
                   mapLonToScreen(pts[i+1][1]), mapLatToScreen(pts[i+1][0]), col);
  };
  drawPoly(na, 22, currentTheme.mapOutline);
  drawPoly(sa, 14, currentTheme.mapOutline);
  drawPoly(eu, 11, currentTheme.mapOutline);
  drawPoly(af, 14, currentTheme.mapOutline);
  drawPoly(as, 16, currentTheme.mapOutline);
  drawPoly(au,  9, currentTheme.mapOutline);
  drawPoly(gr, 10, currentTheme.mapOutline);

  // ── Ring of Fire (dotted red) — actual subduction zone coordinates ───────
  static const float rof[][2] = {
    // South America west coast → Central America
    {-50,-76},{-33,-72},{-18,-72},{-5,-81},{10,-84},{22,-106},
    // North America west coast → Alaska
    {29,-110},{38,-122},{50,-128},{55,-133},{60,-152},{64,-168},
    // Aleutians → Kamchatka → Japan
    {54,-168},{52,158},{50,154},{46,148},{42,142},{35,140},
    // Philippines → Indonesia → Solomon Islands → NZ
    {22,120},{10,125},{0,120},{-10,115},{-25,114},{-35,138},{-44,168},
  };
  for (int i = 0; i < 23; i++) {
    int x0 = mapLonToScreen(rof[i][1]),   y0 = mapLatToScreen(rof[i][0]);
    int x1 = mapLonToScreen(rof[i+1][1]), y1 = mapLatToScreen(rof[i+1][0]);
    int steps = max(abs(x1-x0), abs(y1-y0));
    for (int s = 0; s <= steps; s += 3) {
      int px = x0 + s*(x1-x0)/max(steps,1);
      int py = y0 + s*(y1-y0)/max(steps,1);
      if (px >= MAP_X && px <= MAP_X+MAP_WIDTH && py >= MAP_Y && py <= MAP_Y+MAP_HEIGHT)
        tft.drawPixel(px, py, 0xCB0B);   // Dusty coral
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// EARTHQUAKE DATA FETCHING
// ═══════════════════════════════════════════════════════════════════════════

void checkForEarthquakes() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  HTTPClient http;
  const char* apiURL = getAPIEndpoint(config.region);
  bool usingNZ = isUsingNZAPI(config.region);
  
  http.begin(apiURL);
  http.setTimeout(HTTP_TIMEOUT);
  
  int httpCode = http.GET();
  if (httpCode != 200) {
    http.end();
    return;
  }
  
  String payload = http.getString();
  http.end();
  
  // For USGS feeds, use streaming parser with filter to reduce memory
  DynamicJsonDocument filter(200);
  filter["features"][0]["geometry"]["coordinates"] = true;
  filter["features"][0]["properties"]["mag"] = true;
  filter["features"][0]["properties"]["place"] = true;
  filter["features"][0]["properties"]["time"] = true;
  filter["features"][0]["properties"]["magnitude"] = true;
  filter["features"][0]["properties"]["locality"] = true;
  filter["features"][0]["properties"]["depth"] = true;
  filter["features"][0]["properties"]["time"] = true;
  filter["features"][0]["properties"]["publicID"] = true;
  filter["features"][0]["id"] = true;
  
  DynamicJsonDocument doc(32768);  // 32KB with filtering is enough
  DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  
  if (error) {
    return;
  }
  
  JsonArray features = doc["features"];
  int totalFeatures = features.size();
  
  if (totalFeatures == 0) return;
  
  float highestMag = 0;
  int highestIdx = -1;
  int processedCount = 0;
  
  recentQuakeCount = 0;
  
  for (int i = 0; i < totalFeatures && processedCount < 20; i++) {
    JsonObject quake = features[i];
    
    float mag, lat, lon, depth;
    
    if (usingNZ) {
      lat = quake["geometry"]["coordinates"][1];
      lon = quake["geometry"]["coordinates"][0];
      mag = quake["properties"]["magnitude"];
      depth = quake["properties"]["depth"];
    } else {
      lon = quake["geometry"]["coordinates"][0];
      lat = quake["geometry"]["coordinates"][1];
      mag = quake["properties"]["mag"];
      depth = quake["geometry"]["coordinates"][2];
    }
    
    if (!isInRegion(lat, lon, config.region)) continue;
    
    if (recentQuakeCount < 20) {
      recentQuakes[recentQuakeCount].lat = lat;
      recentQuakes[recentQuakeCount].lon = lon;
      recentQuakes[recentQuakeCount].mag = mag;
      recentQuakes[recentQuakeCount].valid = true;
      recentQuakeCount++;
    }
    
    processedCount++;
    
    if (mag > highestMag) {
      highestMag = mag;
      highestIdx = i;
    }
  }
  
  Serial.println("In region: " + String(processedCount));
  
  if (processedCount == 0) {
    Serial.println("WARNING: No earthquakes found in selected region!");
    Serial.println("Region: " + String(config.region));
    Serial.println("Bounds: lat=" + String(getRegionBounds(config.region).latMin) + 
                   " to " + String(getRegionBounds(config.region).latMax) +
                   ", lon=" + String(getRegionBounds(config.region).lonMin) + 
                   " to " + String(getRegionBounds(config.region).lonMax));
  } else {
    Serial.println("Found " + String(processedCount) + " earthquakes in " + String(config.region));
  }
  
  if (highestIdx >= 0) {
    JsonObject h = features[highestIdx];
    
    highestRegionalQuake.magnitude = highestMag;
    
    if (usingNZ) {
      highestRegionalQuake.latitude = h["geometry"]["coordinates"][1];
      highestRegionalQuake.longitude = h["geometry"]["coordinates"][0];
      highestRegionalQuake.depth = h["properties"]["depth"];
      strncpy(highestRegionalQuake.location, h["properties"]["locality"], sizeof(highestRegionalQuake.location) - 1);
      highestRegionalQuake.timestamp = parseISOToEpoch(h["properties"]["time"].as<const char*>());
    } else {
      highestRegionalQuake.latitude = h["geometry"]["coordinates"][1];
      highestRegionalQuake.longitude = h["geometry"]["coordinates"][0];
      highestRegionalQuake.depth = h["geometry"]["coordinates"][2];
      strncpy(highestRegionalQuake.location, h["properties"]["place"], sizeof(highestRegionalQuake.location) - 1);
      highestRegionalQuake.timestamp = (unsigned long)(h["properties"]["time"].as<double>() / 1000.0);
    }
    
    highestRegionalQuake.isValid = true;
  }
  
  // Find the MOST RECENT earthquake WITHIN the selected region
  // (features[0] might be in Alaska even if we want California!)
  int latestInRegionIdx = -1;
  for (int i = 0; i < totalFeatures; i++) {
    JsonObject q = features[i];
    float lat, lon;
    
    if (usingNZ) {
      lat = q["geometry"]["coordinates"][1];
      lon = q["geometry"]["coordinates"][0];
    } else {
      lon = q["geometry"]["coordinates"][0];
      lat = q["geometry"]["coordinates"][1];
    }
    
    if (isInRegion(lat, lon, config.region)) {
      latestInRegionIdx = i;
      break;  // Feed is time-sorted, first match = most recent
    }
  }
  
  if (latestInRegionIdx >= 0) {
    JsonObject latest = features[latestInRegionIdx];
    String quakeID;
    
    if (usingNZ) {
      quakeID = latest["properties"]["publicID"].as<String>();
    } else {
      quakeID = latest["id"].as<String>();
    }
    
    bool isNewQuake = (quakeID != lastQuakeID && quakeID.length() > 0 && lastQuakeID.length() > 0);
    bool needsInitialData = (lastQuakeID.length() == 0);
    
    if (isNewQuake || needsInitialData) {
      float lat, lon, mag, depth;
      const char* loc;
      
      if (usingNZ) {
        lat = latest["geometry"]["coordinates"][1];
        lon = latest["geometry"]["coordinates"][0];
        mag = latest["properties"]["magnitude"];
        depth = latest["properties"]["depth"];
        loc = latest["properties"]["locality"];
      } else {
        lon = latest["geometry"]["coordinates"][0];
        lat = latest["geometry"]["coordinates"][1];
        mag = latest["properties"]["mag"];
        depth = latest["geometry"]["coordinates"][2];
        loc = latest["properties"]["place"];
      }
      
      latestQuake.magnitude = mag;
      latestQuake.latitude = lat;
      latestQuake.longitude = lon;
      latestQuake.depth = depth;
      strncpy(latestQuake.location, loc, sizeof(latestQuake.location) - 1);
      
      // Use actual earthquake timestamp from API - convert to epoch seconds
      if (usingNZ) {
        latestQuake.timestamp = parseISOToEpoch(latest["properties"]["time"].as<const char*>());
      } else {
        latestQuake.timestamp = (unsigned long)(latest["properties"]["time"].as<double>() / 1000.0);
      }
      latestQuake.isValid = true;

      // Trigger real-wave seismograph simulation for every detected quake
      if (lastTriggeredQuakeID != quakeID) {
        triggerSeismicEvent(lat, lon, mag);
        lastTriggeredQuakeID = quakeID;
      }

      if (isNewQuake && mag >= config.magThreshold) {
        displayEarthquakeAlert(&latestQuake);
      }
      
      if (needsInitialData || isNewQuake) {
        lastQuakeID = quakeID;
      }
      
      lastActivity = millis();
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// EARTHQUAKE ALERT
// ═══════════════════════════════════════════════════════════════════════════

void displayEarthquakeAlert(EarthquakeData* quake) {
  showingAlert = true;
  alertStartTime = millis();

  tft.fillScreen(currentTheme.background);

  uint16_t magColor = getMagnitudeColor(quake->magnitude);

  // ── Header with warning triangles ──
  tft.setFreeFont(FONT_LABEL);
  tft.setTextColor(magColor);
  int htw = tft.textWidth("EQ DETECTED");
  int htx = 120 - htw / 2;
  tft.setCursor(htx, 17);
  tft.print("EQ DETECTED");
  tft.fillTriangle(htx - 16, 20, htx - 10, 6, htx - 4, 20, magColor);
  tft.fillTriangle(120 + htw / 2 + 4, 20, 120 + htw / 2 + 10, 6, 120 + htw / 2 + 16, 20, magColor);

  // Thin divider
  tft.drawFastHLine(5, 28, 230, currentTheme.divider);

  // ── Radar / Target Reticle ──
  int cx = 120;
  int cy = 108;

  // Ring count scales with magnitude
  int ringCount = 2;
  if (quake->magnitude >= 5.0) ringCount = 3;
  if (quake->magnitude >= 7.0) ringCount = 4;

  int radii[] = {20, 38, 56, 72};
  int outerR = radii[ringCount - 1];

  // Crosshair lines (behind rings)
  tft.drawFastHLine(cx - outerR - 12, cy, (outerR + 12) * 2, currentTheme.divider);
  tft.drawFastVLine(cx, cy - outerR - 12, (outerR + 12) * 2, currentTheme.divider);

  // Concentric distance rings
  for (int r = 0; r < ringCount; r++) {
    tft.drawCircle(cx, cy, radii[r], currentTheme.border);
  }

  // 45-degree radial tick marks
  float diag = 0.7071f;
  for (int sx = -1; sx <= 1; sx += 2) {
    for (int sy = -1; sy <= 1; sy += 2) {
      int x1 = cx + (int)(diag * (outerR - 6) * sx);
      int y1 = cy + (int)(diag * (outerR - 6) * sy);
      int x2 = cx + (int)(diag * (outerR + 6) * sx);
      int y2 = cy + (int)(diag * (outerR + 6) * sy);
      tft.drawLine(x1, y1, x2, y2, currentTheme.divider);
    }
  }

  // Epicentre dot at center
  tft.fillCircle(cx, cy, 4, magColor);

  // Magnitude text overlaid on reticle
  char magStr[10];
  snprintf(magStr, sizeof(magStr), "M%.1f", quake->magnitude);
  int tw = tft.textWidth(magStr, 4);
  tft.fillRect(cx - tw / 2 - 3, cy - 14, tw + 6, 28, currentTheme.background);
  tft.setTextColor(magColor);
  tft.drawCentreString(magStr, cx, cy - 13, 4);

  // ── Data below reticle ──
  int dataY = cy + outerR + 16;

  // Location (truncate if too wide)
  tft.setFreeFont(FONT_LABEL);
  tft.setTextColor(currentTheme.textPrimary);
  char locBuf[64];
  strncpy(locBuf, quake->location, sizeof(locBuf) - 1);
  locBuf[sizeof(locBuf) - 1] = '\0';
  while (strlen(locBuf) > 3 && tft.textWidth(locBuf) > 220) {
    locBuf[strlen(locBuf) - 1] = '\0';
  }
  tft.setCursor(120 - tft.textWidth(locBuf) / 2, dataY + 11);
  tft.print(locBuf);

  // Depth
  char depthBuf[32];
  snprintf(depthBuf, sizeof(depthBuf), "Depth: %.0fkm", quake->depth);
  tft.setTextColor(currentTheme.textSecondary);
  tft.setCursor(120 - tft.textWidth(depthBuf) / 2, dataY + 26);
  tft.print(depthBuf);
}

// ═══════════════════════════════════════════════════════════════════════════
// CAPACITIVE TOUCH  (FT6336G, I2C 0x38) + BOOT BUTTON FALLBACK
// ═══════════════════════════════════════════════════════════════════════════

// Returns true if at least one finger is currently down.
// x/y are in native portrait coordinates (X: 0-239, Y: 0-319).
// UI is portrait, so coords map directly — UI square is top 240×240.
bool readTouch(int16_t &x, int16_t &y) {
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(0x02);  // TD_STATUS register
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)6);
  if (Wire.available() < 6) return false;

  uint8_t touches = Wire.read() & 0x0F;
  uint8_t xh = Wire.read();
  uint8_t xl = Wire.read();
  uint8_t yh = Wire.read();
  uint8_t yl = Wire.read();
  Wire.read();  // weight — discard

  if (touches == 0 || touches > 5) return false;
  x = (int16_t)(((xh & 0x0F) << 8) | xl);
  y = (int16_t)(((yh & 0x0F) << 8) | yl);
  return true;
}

void handleButton() {
  if (showingAlert) return;

  unsigned long now = millis();

  // ── Capacitive touch tap ─────────────────────────────────────────────────
  int16_t tx, ty;
  bool touching = readTouch(tx, ty);

  // Rising edge of touch (finger just placed down)
  if (touching && !prevTouching) {
    if (now - lastTouchTime > DEBOUNCE_DELAY) {
      lastTouchTime = now;
      lastActivity  = now;
      isRestMode    = false;
      // Tap triggers immediate data refresh
      checkForEarthquakes();
      updateDataRegion();
      updateMapEarthquakeMarkers();
      lastAPICheck = now;
      Serial.printf("Touch tap (%d,%d) — refreshed\n", tx, ty);
    }
  }
  prevTouching = touching;

  // ── Physical BOOT button fallback ────────────────────────────────────────
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (now - lastButtonPress > DEBOUNCE_DELAY) {
      lastButtonPress = now;
      lastActivity    = now;
      isRestMode      = false;
      checkForEarthquakes();
      updateDataRegion();
      updateMapEarthquakeMarkers();
      lastAPICheck = now;
      Serial.println("BOOT button — refreshed");
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// SOUND  (ES8311 codec via I2S — shares SPI/TFT pins, blocking during playback)
// ═══════════════════════════════════════════════════════════════════════════

void es8311Reg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

void es8311Init() {
  es8311Reg(0x00, 0x1F); delay(10);  // reset
  es8311Reg(0x00, 0x00); delay(10);  // normal op
  // Clock: MCLK from MCLK pin, no pre-division
  es8311Reg(0x01, 0x30);
  es8311Reg(0x02, 0x00);
  es8311Reg(0x03, 0x10);  // LRCK div = 256 for MCLK = 256*Fs
  es8311Reg(0x04, 0x10);
  es8311Reg(0x05, 0x00);
  es8311Reg(0x06, 0x03);
  es8311Reg(0x07, 0x00);
  es8311Reg(0x08, 0xFF);  // enable all clocks
  // Serial port: I2S 16-bit
  es8311Reg(0x0A, 0x0C);
  es8311Reg(0x0B, 0x00);
  // Power up
  es8311Reg(0x0D, 0x01);
  es8311Reg(0x0E, 0x02);
  es8311Reg(0x0F, 0xFF);
  // DAC on, volume max
  es8311Reg(0x31, 0x60);
  es8311Reg(0x37, 0x08);
  es8311Reg(0x44, 0x08);
  es8311Reg(0x45, 0xBF);
  delay(30);
}

void i2sBegin() {
  i2s_config_t cfg = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate          = AUDIO_SR,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 8,
    .dma_buf_len          = 64,
    .use_apll             = false,  // ESP32-S3 has no APLL
    .tx_desc_auto_clear   = true,
    .fixed_mclk           = 0,
  };
  i2s_pin_config_t pins = {
    .mck_io_num     = I2S_MCLK,
    .bck_io_num     = I2S_BCLK,
    .ws_io_num      = I2S_WS,
    .data_out_num   = I2S_DOUT,
    .data_in_num    = I2S_PIN_NO_CHANGE,
  };
  i2s_driver_install(AUDIO_PORT, &cfg, 0, NULL);
  i2s_set_pin(AUDIO_PORT, &pins);
  i2s_zero_dma_buffer(AUDIO_PORT);
}

void i2sEnd() {
  i2s_zero_dma_buffer(AUDIO_PORT);
  delay(20);
  i2s_driver_uninstall(AUDIO_PORT);
}

// Write a sine tone at `freq` Hz for `durationMs` ms, amplitude 0.0-1.0
void writeTone(float freq, float amp, int durationMs) {
  const int CHUNK = 128;
  int16_t buf[CHUNK * 2];
  size_t written;
  int total = (AUDIO_SR * durationMs) / 1000;
  for (int i = 0; i < total; i += CHUNK) {
    int n = min(CHUNK, total - i);
    for (int j = 0; j < n; j++) {
      float t = (float)(i + j) / AUDIO_SR;
      // Soft fade-in/out to avoid clicks (10ms ramps)
      float env = amp;
      int fadeS = (AUDIO_SR * 10) / 1000;
      if (i + j < fadeS)         env *= (float)(i + j) / fadeS;
      if (i + j > total - fadeS) env *= (float)(total - (i + j)) / fadeS;
      int16_t s = (int16_t)(env * 28000.0f * sinf(2.0f * PI * freq * t));
      buf[j * 2]     = s;
      buf[j * 2 + 1] = s;
    }
    i2s_write(AUDIO_PORT, buf, n * 4, &written, portMAX_DELAY);
  }
}

void writeSilence(int durationMs) {
  const int CHUNK = 128;
  int16_t buf[CHUNK * 2] = {0};
  size_t written;
  int total = (AUDIO_SR * durationMs) / 1000;
  for (int i = 0; i < total; i += CHUNK) {
    int n = min(CHUNK, total - i);
    i2s_write(AUDIO_PORT, buf, n * 4, &written, portMAX_DELAY);
  }
}

void playRetro() {
  writeTone(880,  0.55f, 120); writeSilence(55);
  writeTone(1047, 0.60f, 120); writeSilence(55);
  writeTone(1319, 0.65f, 200);
}

void playRumble() {
  // Sweeps frequency like P→S→Surface waves, gentle amplitude envelope
  const int CHUNK = 128;
  int16_t buf[CHUNK * 2];
  size_t written;
  int totalMs = 3400;
  int total   = (AUDIO_SR * totalMs) / 1000;

  for (int i = 0; i < total; i += CHUNK) {
    int n = min(CHUNK, total - i);
    for (int j = 0; j < n; j++) {
      float t    = (float)(i + j) / AUDIO_SR;
      float prog = t / (totalMs / 1000.0f);  // 0 → 1

      // Frequency arc: 50 → 115 → 45 Hz
      float freq;
      if      (prog < 0.35f) freq = 50.0f  + (prog / 0.35f) * 65.0f;
      else if (prog < 0.55f) freq = 115.0f - ((prog - 0.35f) / 0.20f) * 15.0f;
      else                   freq = 100.0f * (1.0f - (prog - 0.55f) / 0.45f) + 30.0f;

      // Amplitude envelope: gentle attack, sustain, long decay
      float amp;
      if      (prog < 0.12f) amp = prog / 0.12f;
      else if (prog < 0.52f) amp = 1.0f;
      else                   amp = 1.0f - (prog - 0.52f) / 0.48f;
      amp *= 0.55f;  // keep it gentle

      int16_t s  = (int16_t)(amp * 28000.0f * sinf(2.0f * PI * freq * t));
      buf[j * 2]     = s;
      buf[j * 2 + 1] = s;
    }
    i2s_write(AUDIO_PORT, buf, n * 4, &written, portMAX_DELAY);
  }
}

void playAlertSound() {
  if (config.soundMode == 0) return;

  digitalWrite(AMP_EN, LOW);
  es8311Init();
  i2sBegin();
  delay(20);

  if (config.soundMode == 1) playRetro();
  else                        playRumble();

  i2sEnd();
  delay(5);
  digitalWrite(AMP_EN, HIGH);
}

void playStartupRumble() {
  // Always plays on boot regardless of soundMode config setting
  digitalWrite(AMP_EN, LOW);
  es8311Init();
  i2sBegin();
  delay(20);
  playRumble();
  i2sEnd();
  delay(5);
  digitalWrite(AMP_EN, HIGH);
}

// ═══════════════════════════════════════════════════════════════════════════
// WEB SERVER - CONFIG PORTAL
// ═══════════════════════════════════════════════════════════════════════════

void setupConfigPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("SeisMonitor-Setup");
  
  IPAddress IP = WiFi.softAPIP();
  Serial.println("AP: " + IP.toString());
  
  dnsServer.start(DNS_PORT, "*", IP);
  
  tft.fillScreen(currentTheme.background);
  tft.setTextColor(currentTheme.textPrimary);
  tft.drawCentreString("SETUP MODE", 120, 75, 4);

  tft.setTextColor(currentTheme.textSecondary);
  tft.drawCentreString("WiFi: SeisMonitor-Setup", 120, 115, 2);
  tft.drawCentreString("URL: 192.168.4.1", 120, 138, 2);
  
  setupWebServer();
}

void setupWebServer() {
  server.on("/", handleWebRoot);
  server.on("/save", HTTP_POST, handleWebSave);
  server.on("/testsound", handleTestSound);
  server.on("/testquake", handleTestQuake);
  server.onNotFound(handleWebNotFound);
  server.begin();
  Serial.println("Web server started");
}

void handleWebRoot() {
  String html = R"(<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>SeisMonitor</title><style>*{margin:0;padding:0;box-sizing:border-box}body{font-family:-apple-system,sans-serif;background:#0a0a0a;color:#e8e8e8;padding:20px}h1{text-align:center;margin:20px 0;color:#88C8B0;letter-spacing:2px}.container{max-width:500px;margin:0 auto;background:#1a1a1a;padding:30px;border:1px solid#333}label{display:block;margin:15px 0 5px;font-size:12px;text-transform:uppercase;color:#888}input,select{width:100%;padding:12px;background:#0f0f0f;border:1px solid#333;color:#fff;border-radius:5px;font-size:16px}input[type=range]{padding:0;height:40px;-webkit-appearance:none}input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:20px;height:20px;background:#88C8B0;border-radius:50%;cursor:pointer}input[type=range]::-webkit-slider-runnable-track{height:4px;background:#333;border-radius:2px}o{display:block;text-align:center;color:#88C8B0;font-size:24px;font-weight:600;margin:10px 0}button{width:100%;padding:15px;background:#88C8B0;color:#000;border:none;border-radius:5px;font-size:14px;font-weight:600;margin-top:20px;cursor:pointer}button:hover{background:#9AD8C0}</style></head><body><h1>SEISMONITOR</h1><div class="container"><form action="/save" method="POST"><label>WiFi Network</label><input name="ssid" value=")";
  html += config.wifiSSID;
  html += R"(" required><label>WiFi Password</label><input type="password" name="password" value=")";
  html += config.wifiPassword;
  html += R"("><label>Region</label><select name="region"><option value="NZ")";
  if (strcmp(config.region, "NZ") == 0) html += " selected";
  html += R"(>New Zealand</option><option value="Japan")";
  if (strcmp(config.region, "Japan") == 0) html += " selected";
  html += R"(>Japan</option><option value="China")";
  if (strcmp(config.region, "China") == 0) html += " selected";
  html += R"(>China</option><option value="California")";
  if (strcmp(config.region, "California") == 0) html += " selected";
  html += R"(>California</option><option value="Global")";
  if (strcmp(config.region, "Global") == 0) html += " selected";
  html += R"(>Global</option></select><label>Alert Magnitude Threshold</label><input type="range" name="magthresh" min="2.0" max="6.0" step="0.5" value=")";
  html += String(config.magThreshold, 1);
  html += R"(" oninput="this.nextElementSibling.value=this.value"><output>)";
  html += String(config.magThreshold, 1);
  html += R"(</output><label>Font Size</label><select name="fontsize"><option value="1")";
  if (config.fontSize == 1) html += " selected";
  html += R"(>Small</option><option value="2")";
  if (config.fontSize == 2) html += " selected";
  html += R"(>Medium</option><option value="3")";
  if (config.fontSize == 3) html += " selected";
  html += R"(>Large</option></select>)";

  // Show recent quakes toggle (latest quake always remains visible)
  html += R"(<label style="margin-top:12px;display:block">Show Recent Quakes</label><input type="checkbox" name="showrecent" )";
  if (config.showRecentQuakes) html += " checked";
  html += R"(> <small style="color:#888;display:block;margin-bottom:8px">(Latest quake will still be highlighted)</small>)";

  // Show city dots toggle
  html += R"(<label style="margin-top:12px;display:block">Show City Markers</label><input type="checkbox" name="showcities" )";
  if (config.showCityDots) html += " checked";
  html += R"(>)";

  // Sound mode
  html += R"(<label>Alert Sound</label><select name="soundmode"><option value="0")";
  if (config.soundMode == 0) html += " selected";
  html += R"(>Off</option><option value="1")";
  if (config.soundMode == 1) html += " selected";
  html += R"(>Retro Tone</option><option value="2")";
  if (config.soundMode == 2) html += " selected";
  html += R"(>Earthquake Rumble</option></select>)";

  html += R"(<button type="submit">SAVE</button></form>)"
          R"(<div style="margin-top:20px;display:flex;gap:10px">)"
          R"(<a href="/testsound" style="flex:1;padding:12px;background:#333;color:#aaa;border:1px solid#555;border-radius:5px;font-size:13px;text-align:center;text-decoration:none">TEST SOUND</a>)"
          R"(<a href="/testquake" style="flex:1;padding:12px;background:#333;color:#aaa;border:1px solid#555;border-radius:5px;font-size:13px;text-align:center;text-decoration:none">TEST QUAKE</a>)"
          R"(</div></div></body></html>)";
  
  server.send(200, "text/html", html);
}

void handleWebSave() {
  if (server.hasArg("ssid")) server.arg("ssid").toCharArray(config.wifiSSID, sizeof(config.wifiSSID));
  if (server.hasArg("password")) server.arg("password").toCharArray(config.wifiPassword, sizeof(config.wifiPassword));
  if (server.hasArg("region")) server.arg("region").toCharArray(config.region, sizeof(config.region));
  if (server.hasArg("magthresh")) config.magThreshold = server.arg("magthresh").toFloat();
  if (server.hasArg("fontsize")) config.fontSize = server.arg("fontsize").toInt();
  // Checkboxes: when unchecked, browser won't send the arg, so set false by default
  if (server.hasArg("showrecent")) config.showRecentQuakes = true; else config.showRecentQuakes = false;
  if (server.hasArg("showcities")) config.showCityDots = true; else config.showCityDots = false;
  if (server.hasArg("soundmode")) config.soundMode = server.arg("soundmode").toInt();

  saveConfig();
  
  String html = R"(<!DOCTYPE html><html><head><meta http-equiv="refresh" content="3;url=/"><style>body{font-family:Arial;background:#0a0a0a;color:#88C8B0;display:flex;align-items:center;justify-content:center;height:100vh;margin:0;text-align:center}h1{letter-spacing:3px}</style></head><body><div><h1>SAVED</h1><p>Restarting...</p></div></body></html>)";
  
  server.send(200, "text/html", html);
  delay(2000);
  ESP.restart();
}

void handleTestSound() {
  server.send(200, "text/html",
    "<!DOCTYPE html><html><head><meta http-equiv='refresh' content='4;url=/'>"
    "<style>body{font-family:Arial;background:#0a0a0a;color:#88C8B0;display:flex;"
    "align-items:center;justify-content:center;height:100vh;margin:0;text-align:center}"
    "h1{letter-spacing:3px}</style></head><body><div><h1>PLAYING SOUND</h1>"
    "<p>Board will play the alert sound now...</p></div></body></html>");
  playAlertSound();
}

void handleTestQuake() {
  server.send(200, "text/html",
    "<!DOCTYPE html><html><head><meta http-equiv='refresh' content='4;url=/'>"
    "<style>body{font-family:Arial;background:#0a0a0a;color:#88C8B0;display:flex;"
    "align-items:center;justify-content:center;height:100vh;margin:0;text-align:center}"
    "h1{letter-spacing:3px}</style></head><body><div><h1>SIMULATING M6.2</h1>"
    "<p>Seismograph event triggered...</p></div></body></html>");
  // Simulate a nearby M6.2 to show full seismograph animation
  triggerSeismicEvent(-41.3f, 174.8f, 6.2f);
}

void handleWebNotFound() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}
