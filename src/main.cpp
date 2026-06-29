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
 * Orientation: Landscape 320×240 (setRotation 1, inverted)
 *
 * Version: 3.0 - Landscape layout (data left · map right · seismo bottom)
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
#define FONT_LABEL  &FreeSans9pt7b       // Headers, labels, place names
#define FONT_DATA   &FreeSansBold9pt7b   // (legacy) compact bold
#define FONT_MAG    &FreeSansBold12pt7b  // Magnitude hero — a step bigger than the place name

const int SCREEN_WIDTH  = 320;   // Landscape — full 320×240 canvas (setRotation 1)
const int SCREEN_HEIGHT = 240;

// ═══════════════════════════════════════════════════════════════════════════
// TIMING CONSTANTS
// ═══════════════════════════════════════════════════════════════════════════

const unsigned long API_POLL_INTERVAL = 90000;      // 90s — the fetch is blocking, so a longer gap means the globe stalls far less often
const unsigned long SEISMO_UPDATE_INTERVAL = 50;    // 50ms — smoother / higher-res scroll
const unsigned long DISPLAY_CYCLE_INTERVAL = 60000; // 60 seconds
const unsigned long REST_MODE_TIMEOUT = 45000;      // 45 seconds
const unsigned long ALERT_DURATION = 25000;         // 25 seconds
const unsigned long DEBOUNCE_DELAY = 300;           // 300ms
const int HTTP_TIMEOUT = 5000;                      // 5 seconds
const byte DNS_PORT = 53;

// ═══════════════════════════════════════════════════════════════════════════
// DISPLAY LAYOUT — Landscape 320×240 (June-20 handoff): three bordered panels —
// DATA (top-left) + SEISMO (bottom-left) + MAP/GLOBE (right).
// ═══════════════════════════════════════════════════════════════════════════

const int HEADER_H = 22;
const int PAD = 6;
const int CONTENT_TOP = HEADER_H + 4;            // 26
const int CONTENT_BOT = SCREEN_HEIGHT - PAD;     // 234
const int LEFT_W = 106;

// Data panel (top-left, bordered)
const int DATA_X = PAD;                          // 6
const int DATA_Y = CONTENT_TOP;                  // 26
const int DATA_WIDTH = LEFT_W;                   // 106
const int DATA_HEIGHT = 161;                     // 26..187

// Seismograph panel (bottom-left, bordered) — trace area inset from the border
const int SEISMO_PANEL_Y = 192;
const int SEISMO_PANEL_H = 42;                   // 192..234
const int SEISMO_X = PAD + 3;                    // 9
const int SEISMO_Y = SEISMO_PANEL_Y + 3;         // 195
const int SEISMO_WIDTH = LEFT_W - 6;             // 100
const int SEISMO_HEIGHT = SEISMO_PANEL_H - 6;    // 36
const int SEISMO_CENTER_Y = SEISMO_Y + (SEISMO_HEIGHT / 2);  // 213
const int SEISMO_MAX_AMPLITUDE = 50;
const int SEISMO_LINE_X = SEISMO_X;
const int SEISMO_LINE_W = SEISMO_WIDTH;

// Map / globe panel (right, bordered)
const int MAP_X = PAD + LEFT_W + 5;              // 117
const int MAP_Y = CONTENT_TOP;                   // 26
const int MAP_WIDTH = SCREEN_WIDTH - PAD - MAP_X; // 197
const int MAP_HEIGHT = CONTENT_BOT - MAP_Y;      // 208
const int MAP_CX = MAP_X + MAP_WIDTH / 2;        // 215 — rings + projection centre
const int MAP_CY = MAP_Y + MAP_HEIGHT / 2;       // 130
const int MAP_BOX_W = 150;                       // coastline projection box
const int MAP_BOX_H = 190;

// Soft-green structural panel borders (June-20 handoff)
const uint16_t PANEL_EDGE     = 0x2246;          // #234a36
const uint16_t PANEL_EDGE_DIM = 0x1183;          // #16301f
const uint16_t GEO_DIM        = 0x2227;          // faint green — barely-there OCEAN-TRENCH texture (Japan/Cascadia; dimmed: was too "stringy")
const uint16_t GEO_FAULT      = 0x2B69;          // muted green — visible-but-subtle LAND-FAULT lines (NZ/China/California)

// ═══════════════════════════════════════════════════════════════════════════
// REGIONAL BOUNDARIES
// ═══════════════════════════════════════════════════════════════════════════

struct RegionBounds {
  float latMin;
  float latMax;
  float lonMin;
  float lonMax;
};

const RegionBounds BOUNDS_NZ = {-47.3, -34.0, 166.0, 179.0};
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
const char* API_CALIFORNIA = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/1.0_day.geojson"; // M1.0+: California is often quiet at M2.5+ in 24h, so use the lower feed to stay populated
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
  uint16_t sub;        // deepest muted green — ticks, distance labels, footnotes
  uint16_t ring1;      // concentric distance rings, faint -> less faint
  uint16_t ring2;
  uint16_t ring3;
  uint16_t ring4;
};

struct Config {
  char wifiSSID[64];
  char wifiPassword[64];
  char region[16];
  float magThreshold;
  int fontSize;
  char aesthetic[16];
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
bool showingRegionPicker = false;   // gear-opened location picker



int seismoX = SEISMO_LINE_X;
int seismoLastY = SEISMO_CENTER_Y;

unsigned long lastAPICheck = 0;
unsigned long lastSeismoUpdate = 0;
unsigned long lastDisplaySwitch = 0;
unsigned long lastActivity = 0;
unsigned long lastButtonPress = 0;
unsigned long alertStartTime  = 0;
unsigned long pickerStartTime = 0;
unsigned long lastTouchTime   = 0;   // millis() of last confirmed tap
bool          prevTouching    = false;

unsigned long lastMapPing      = 0;     // epicenter "active zone" ping animation
bool          mapPingWasActive = false;

TFT_eSprite   globeSpr         = TFT_eSprite(&tft);   // off-screen buffer for the spinning globe
bool          globeSprReady    = false;
bool          globeSprTried    = false;
float         globeRot         = 0.6f;  // globe yaw (radians)
unsigned long lastGlobeFrame   = 0;
const float   GLOBE_R          = 74.0f;   // inset from the panel edges (was 92, too big)

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

// ── Soft HUD — the chosen aesthetic (desaturated phosphor green) ──
Theme createSoftHUDTheme() {
  Theme t = {};
  t.background    = 0x0041;  // #03080a near-black cool green
  t.border        = 0x08C2;  // #0e1a16 rule
  t.divider       = 0x08C2;  // #0e1a16 rule
  t.textPrimary   = 0xBF38;  // #b8e6c4 ink — place names
  t.textSecondary = 0x4C4D;  // #4a8a68 secondary — meta labels
  t.textAccent    = 0x7EB3;  // #7fd69a primary — status, header, trace
  t.seismoLine    = 0x7EB3;  // #7fd69a primary
  t.seismoGrid    = 0x08E2;  // #0e1c17 ring1
  t.mapOutline    = 0x0588;  // #00b347 BRIGHT NZ coastline
  t.mapCity       = 0x7EB3;  // #7fd69a primary
  t.mapOcean      = 0x0041;  // #03080a bg
  t.mapLand       = 0x08C2;  // #0c1813 land fill
  t.dataLatest    = 0x9F56;  // #9de8b2 latest accent
  t.dataHighest   = 0xD751;  // #d4e88a 24h-high accent
  t.sub           = 0x32C9;  // #355a48 sub
  t.ring1         = 0x0820;  // softer — concentric rings, faint -> less faint
  t.ring2         = 0x0841;
  t.ring3         = 0x10A2;
  t.ring4         = 0x18E3;
  return t;
}

Theme createElegantTheme() {
  Theme t = {};
  t.background = TFT_BLACK;
  t.border = 0x2104;
  t.divider = 0x2104;
  t.textPrimary = 0xE71C;    // Warm white
  t.textSecondary = 0x8410;  // Medium grey
  t.textAccent = 0x7DBA;     // Muted seafoam
  t.seismoLine = 0x5654;     // Soft teal/seafoam
  t.seismoGrid = 0x18C3;     // Very dark grey
  t.mapOutline = 0x6B4D;     // Light grey
  t.mapCity = 0x7DBA;         // Muted seafoam
  t.mapOcean = 0x0108;       // Very dark navy
  t.mapLand  = 0x2965;       // Dark warm grey-green
  t.dataLatest  = 0xAE9C;    // Pale aqua/seafoam
  t.dataHighest = 0xCB0B;    // Dusty coral/terracotta
  return t;
}

Theme createContrastTheme() {
  Theme t = {};
  t.background = TFT_BLACK;
  t.border = 0x2945;
  t.divider = 0x4208;
  t.textPrimary = TFT_WHITE;
  t.textSecondary = 0xBDF7;  // Light grey
  t.textAccent = 0xB7FC;     // Bright aqua
  t.seismoLine = 0x67F8;     // Bright teal
  t.seismoGrid = 0x39E7;
  t.mapOutline = 0xBDF7;
  t.mapCity = 0xB7FC;
  t.mapOcean = 0x0108;       // Very dark navy
  t.mapLand  = 0x4A49;       // Medium dark grey
  t.dataLatest  = 0xB7FC;    // Bright aqua
  t.dataHighest = 0xE34B;    // Bright coral
  return t;
}

Theme createMonoTheme() {
  Theme t = {};
  t.background = TFT_BLACK;
  t.border = 0x4208;
  t.divider = 0x4208;
  t.textPrimary = TFT_WHITE;
  t.textSecondary = 0xBDF7;
  t.textAccent = TFT_WHITE;
  t.seismoLine = TFT_WHITE;
  t.seismoGrid = 0x2945;
  t.mapOutline = 0x8410;
  t.mapCity = TFT_WHITE;
  t.mapOcean = 0x0000;       // Black ocean
  t.mapLand  = 0x2945;       // Dark grey land
  t.dataLatest  = TFT_WHITE;
  t.dataHighest = 0xBDF7;
  return t;
}

Theme loadTheme(const char* aesthetic) {
  if (strcmp(aesthetic, "elegant") == 0) return createElegantTheme();
  if (strcmp(aesthetic, "contrast") == 0) return createContrastTheme();
  if (strcmp(aesthetic, "mono") == 0) return createMonoTheme();
  return createSoftHUDTheme();   // default — the chosen aesthetic
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
  preferences.getString("aesthetic", config.aesthetic, sizeof(config.aesthetic));
  config.showRecentQuakes = preferences.getBool("show_recent", true);
  config.showCityDots = preferences.getBool("show_cities", false);
  config.soundMode = preferences.getInt("sound_mode", 0);

  preferences.end();

  if (strlen(config.region) == 0) strcpy(config.region, "NZ");
  if (strlen(config.aesthetic) == 0) strcpy(config.aesthetic, "elegant");
  
  Serial.println("Config loaded");
}

void saveConfig() {
  preferences.begin("seismonitor", false);
  
  preferences.putString("wifi_ssid", config.wifiSSID);
  preferences.putString("wifi_pass", config.wifiPassword);
  preferences.putString("region", config.region);
  preferences.putFloat("mag_thresh", config.magThreshold);
  preferences.putInt("font_size", config.fontSize);
  preferences.putString("aesthetic", config.aesthetic);
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

// Equal-aspect projection with cos(lat) longitude correction, fitted to a
// MAP_BOX_W×MAP_BOX_H box centred on (MAP_CX, MAP_CY). Mirrors NZ_PROJECT()
// in the design handoff so coastline and markers share one transform.
// China is far wider than tall, so it letterboxes in the default (tall) box —
// give it a larger box that fills more of the map panel. Both still fit the panel.
static int mapBoxW() { return (strcmp(config.region, "China") == 0) ? 194 : MAP_BOX_W; }
static int mapBoxH() { return (strcmp(config.region, "China") == 0) ? 202 : MAP_BOX_H; }

static void getMapProj(const RegionBounds& db, float& scale,
                       float& offX, float& offY, float& cosLat) {
  float latSpan = db.latMax - db.latMin;
  float lonSpan = db.lonMax - db.lonMin;
  float cLat = (db.latMin + db.latMax) * 0.5f;
  cosLat = cosf(cLat * 0.01745329f);
  float effLon = lonSpan * cosLat;
  const float m = 8.0f;                          // inner margin
  float innerW = mapBoxW() - 2 * m, innerH = mapBoxH() - 2 * m;
  scale = min(innerW / effLon, innerH / latSpan);
  offX  = m + (innerW - scale * effLon)  * 0.5f;
  offY  = m + (innerH - scale * latSpan) * 0.5f;
}

int mapLatToScreen(float lat) {
  RegionBounds db = getRegionBounds(config.region);
  float scale, offX, offY, cosLat;
  getMapProj(db, scale, offX, offY, cosLat);
  int gy = MAP_CY - mapBoxH() / 2;
  return gy + (int)(offY + (db.latMax - lat) * scale);
}

int mapLonToScreen(float lon) {
  RegionBounds db = getRegionBounds(config.region);
  float scale, offX, offY, cosLat;
  getMapProj(db, scale, offX, offY, cosLat);
  int gx = MAP_CX - mapBoxW() / 2;
  return gx + (int)(offX + (lon - db.lonMin) * scale * cosLat);
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
void animateMapPing();
void updateMapEarthquakeMarkers();
void updateDataRegion();
void drawHeader();

void drawNZMap();
void drawJapanMap();
void drawChinaMap();
void drawCaliforniaMap();
void drawGlobalMap();
void animateSeismograph();
void checkForEarthquakes();
void displayEarthquakeAlert(EarthquakeData* quake);
void drawRegionPicker();
int  regionAtPoint(int16_t sx, int16_t sy);
void selectRegion(int idx);
void handleButton();
void setupConfigPortal();
void setupWebServer();
void handleWebRoot();
void handleWebSave();
void handleWebNotFound();
void handleTestSound();
void handleTestQuake();
void playStartupRumble();
void syncTimezone();

// ═══════════════════════════════════════════════════════════════════════════
// TIMEZONE — NZ default (DST-aware) + geo-IP auto-detect of the actual offset
// ═══════════════════════════════════════════════════════════════════════════

void syncTimezone() {
  // Reliable fallback: New Zealand (NZST-12 / NZDT-13, DST handled automatically)
  setenv("TZ", "NZST-12NZDT,M9.5.0,M4.1.0/3", 1);
  tzset();
  if (WiFi.status() != WL_CONNECTED) return;

  // Auto-detect the connection's current UTC offset (incl. DST) via geo-IP.
  HTTPClient http;
  http.begin("http://ip-api.com/json/?fields=status,offset,timezone");
  http.setTimeout(4000);
  if (http.GET() == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, http.getString()) &&
        strcmp(doc["status"] | "", "success") == 0) {
      long off = doc["offset"] | (long)0;            // seconds east of UTC
      long ah = off / 3600, am = labs(off % 3600) / 60;
      char tz[24];
      if (am) snprintf(tz, sizeof(tz), "GMT%+ld:%02ld", -ah, am);   // POSIX sign inverted
      else    snprintf(tz, sizeof(tz), "GMT%+ld", -ah);
      setenv("TZ", tz, 1);
      tzset();
      Serial.printf("TZ auto: %s offset=%lds -> %s\n",
                    (const char*)(doc["timezone"] | "?"), off, tz);
    }
  }
  http.end();
}

// ═══════════════════════════════════════════════════════════════════════════
// LOADING / BOOT SCREEN — pulsing glyph, status line, scan bar (soft HUD)
// ═══════════════════════════════════════════════════════════════════════════

void drawLoadingScreen(const char* status, int frame) {
  static char lastStatus[24] = "";
  bool full = (strcmp(lastStatus, status) != 0);          // full redraw on status change
  if (full) {
    strncpy(lastStatus, status, sizeof(lastStatus) - 1);
    lastStatus[sizeof(lastStatus) - 1] = '\0';
    tft.fillScreen(currentTheme.background);
    tft.setTextColor(currentTheme.textPrimary);
    tft.drawCentreString("SEISMONITOR", 160, 116, 4);
    tft.setTextFont(1);
    tft.setTextColor(currentTheme.textSecondary);
    tft.drawCentreString("EARTHQUAKE MONITOR", 160, 146, 1);
    tft.setTextColor(currentTheme.textAccent);
    tft.drawCentreString(status, 160, 172, 1);
    tft.setTextColor(currentTheme.sub);
    tft.drawString("v4.9", 8, 228, 1);
    tft.drawString("ES3C28P", 320 - 8 - tft.textWidth("ES3C28P"), 228, 1);
  }

  // Pulsing glyph — two staggered expanding rings + bright core
  const int cx = 160, cy = 66;
  tft.fillRect(cx - 32, cy - 32, 64, 64, currentTheme.background);
  for (int k = 0; k < 2; k++) {
    int rr = 8 + ((frame * 2 + k * 11) % 22);
    tft.drawCircle(cx, cy, rr, currentTheme.ring4);
  }
  tft.drawCircle(cx, cy, 7, currentTheme.dataLatest);
  tft.fillCircle(cx, cy, 3, currentTheme.dataLatest);

  // Animated dots after the status text
  tft.setTextFont(1);
  int sw = tft.textWidth(status);
  tft.fillRect(160 + sw / 2 + 2, 170, 26, 10, currentTheme.background);
  tft.setTextColor(currentTheme.dataLatest);
  tft.setCursor(160 + sw / 2 + 4, 172);
  for (int i = 0, nd = frame % 4; i < nd; i++) tft.print(".");

  // Indeterminate scan bar
  const int bx = 160 - 75, bw = 150;
  tft.fillRect(bx, 192, bw, 2, currentTheme.divider);
  const int sweep = 48;
  int sx = bx + ((frame * 9) % (bw + sweep)) - sweep;
  int x0 = max(sx, bx), x1 = min(sx + sweep, bx + bw);
  if (x1 > x0) tft.fillRect(x0, 192, x1 - x0, 2, currentTheme.dataLatest);
}

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

  delay(100);
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n══════════════════════════════");
  Serial.println("   SEISMONITOR V2.0");
  Serial.println("══════════════════════════════\n");

  loadConfig();
  currentTheme = createSoftHUDTheme();  // Soft HUD — the chosen aesthetic

  tft.init();
  tft.setRotation(1);      // Landscape — 320×240 (native 240×320 panel rotated)
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
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.wifiSSID, config.wifiPassword);

  int frame = 0;
  while (WiFi.status() != WL_CONNECTED && frame < 200) {   // animated, ~20s
    drawLoadingScreen("CONNECTING TO WIFI", frame);
    delay(100);
    frame++;
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

  // Sync time via NTP so earthquake timestamps can be interpreted
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  int f2 = 0;
  while (time(nullptr) < 1600000000 && f2 < 60) {          // animated, ~6s
    drawLoadingScreen("FETCHING SEISMIC DATA", f2);
    delay(100);
    f2++;
  }

  syncTimezone();   // NZ default + geo-IP auto-detect of the local offset
  setupWebServer();

  drawLoadingScreen("FETCHING SEISMIC DATA", f2);   // hold the loader through the first fetch
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
  
  handleButton();

  // Read the clock AFTER handleButton: it may have just opened the picker (or shown an
  // alert) and set pickerStartTime/alertStartTime to a millis() LATER than a value
  // captured before the call. "now - start" would then underflow (unsigned) to ~4e9 and
  // trip the 30s auto-close on the SAME iteration the picker opens — the entire "flash".
  unsigned long now = millis();

  if (showingAlert) {
    if (now - alertStartTime > ALERT_DURATION) {
      showingAlert = false;
      drawUI();
      lastActivity = now;
    }
    return;
  }

  if (showingRegionPicker) {
    if (now - pickerStartTime > 30000UL) {   // auto-close after 30s of no choice
      showingRegionPicker = false;
      drawUI();
      lastActivity = now;
    }
    return;
  }

  if (now - lastSeismoUpdate > SEISMO_UPDATE_INTERVAL) {
    animateSeismograph();
    lastSeismoUpdate = now;
  }

  // Live epicenter pulse — radiating rings on the LATEST quake so the screen feels alive
  // and you're likely to catch it. Lively for a fresh quake (every ~90s in the first hour),
  // calmer after (every ~3 min). First burst ~8s after data.
  {
    static unsigned long nextPulse = 0, pulseStart = 0;
    static bool pulseActive = false;
    bool canPulse = latestQuake.isValid && strcmp(config.region, "Global") != 0;   // globe self-animates
    if (canPulse) {
      if (nextPulse == 0) nextPulse = now + 8000UL;
      if (!pulseActive && now >= nextPulse) {
        pulseActive = true; pulseStart = now;
        time_t te = time(nullptr);
        unsigned long age = (te > 1600000000 && latestQuake.timestamp > 0 && (unsigned long)te > latestQuake.timestamp)
                            ? ((unsigned long)te - latestQuake.timestamp) : 999999UL;
        nextPulse = now + (age < 3600UL ? 90000UL : 180000UL);   // 90s in the first hour, else 3 min
      }
    }
    if (pulseActive) {
      if (now - pulseStart < 3500UL) {                           // ~3.5s burst — long enough to notice
        if (now - lastMapPing > 180) { animateMapPing(); lastMapPing = now; }
      } else {
        pulseActive = false;
        updateMapEarthquakeMarkers();   // restore the clean marker after the burst
      }
    }
  }

  // Spin the globe (Global region) — off-screen render + push, flicker-free
  if (globeSprReady && strcmp(config.region, "Global") == 0 && now - lastGlobeFrame > 90) {
    globeRot += 0.012f;   // smaller step + shorter interval = smoother at the same spin speed
    if (globeRot > 6.2831853f) globeRot -= 6.2831853f;
    drawGlobalMap();
    lastGlobeFrame = now;
  }

  if (now - lastAPICheck > API_POLL_INTERVAL) {
    unsigned long lT = latestQuake.timestamp, hT = highestRegionalQuake.timestamp;
    float         lM = latestQuake.magnitude, hM = highestRegionalQuake.magnitude;
    checkForEarthquakes();
    // Repaint BOTH the data block and the map when a poll changes the latest/highest
    // quake — otherwise the map (repainted by the pulse) and the cells drift out of sync.
    if (latestQuake.timestamp != lT || latestQuake.magnitude != lM ||
        highestRegionalQuake.timestamp != hT || highestRegionalQuake.magnitude != hM) {
      updateDataRegion();
      updateMapEarthquakeMarkers();
    }
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

// Settings gear centre (top-left corner) — also the touch hit target.
const int GEAR_CX = 308;   // settings cog — header right corner (also touch target)
const int GEAR_CY = 10;

// Gear/cog icon centred at (cx,cy): body disc with 8 protruding teeth + hub
// hole. The teeth sticking out read clearly as a settings cog (~20px).
void drawGearIcon(int cx, int cy, uint16_t color) {
  for (int a = 0; a < 360; a += 45) {            // 8 teeth poking out past the body
    float r = a * 0.01745329f;
    tft.fillCircle(cx + (int)(cosf(r) * 8), cy + (int)(sinf(r) * 8), 2, color);
  }
  tft.fillCircle(cx, cy, 6, color);              // body
  tft.fillCircle(cx, cy, 2, currentTheme.background);   // hub hole
}

// Native-script region glyphs: 中国 (China) and 日本 (Japan), rasterised from a real font
// (Microsoft YaHei Bold) as 16x16 anti-aliased coverage maps — proper character shapes with
// smooth edges. GeoNet/USGS send romanised names; the header overlays the native script.
static const uint8_t GLYPH_ZHONG[256] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x1A,0x0A,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x99,0xFF,0x57,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x99,0xFF,0x57,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x6E,0x99,0x99,0x99,0x99,0xD6,0xFF,0xBD,0x99,0x99,0x99,0x99,0x4C,0x00,0x00,0x00,0xB5,0xFF,0xE2,0xD6,0xD6,0xF0,0xFF,0xEB,0xD6,0xD6,0xEB,0xFF,0x80,0x00,0x00,0x00,0xB5,0xFF,0x36,0x00,0x00,0x99,0xFF,0x57,0x00,0x00,0x80,0xFF,0x80,0x00,0x00,0x00,0xB5,0xFF,0x36,0x00,0x00,0x99,0xFF,0x57,0x00,0x00,0x80,0xFF,0x80,0x00,0x00,0x00,0xB5,0xFF,0x4A,0x1A,0x1A,0xA3,0xFF,0x69,0x1A,0x1A,0x8C,0xFF,0x80,0x00,0x00,0x00,0xB5,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x80,0x00,0x00,0x00,0xB5,0xFF,0x7D,0x57,0x57,0xBD,0xFF,0x97,0x57,0x57,0xAB,0xFF,0x80,0x00,0x00,0x00,0x3F,0x57,0x14,0x00,0x00,0x99,0xFF,0x57,0x00,0x00,0x2C,0x57,0x2C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x99,0xFF,0x57,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x99,0xFF,0x57,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x99,0xFF,0x57,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x35,0x57,0x1E,0x00,0x00,0x00,0x00,0x00};
static const uint8_t GLYPH_GUO[256] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x82,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x5C,0x00,0x00,0x00,0xD6,0xFF,0xDC,0xD6,0xD6,0xD6,0xD6,0xD6,0xD6,0xD6,0xEB,0xFF,0x99,0x00,0x00,0x00,0xD6,0xFF,0x23,0x1A,0x1A,0x1A,0x1A,0x1A,0x1A,0x1A,0x5A,0xFF,0x99,0x00,0x00,0x00,0xD6,0xFF,0x70,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x70,0xFF,0x99,0x00,0x00,0x00,0xD6,0xFF,0x38,0x57,0x57,0xD1,0xFF,0x69,0x57,0x57,0x60,0xFF,0x99,0x00,0x00,0x00,0xD6,0xFF,0x29,0x99,0x99,0xE2,0xFF,0xA3,0x99,0x82,0x57,0xFF,0x99,0x00,0x00,0x00,0xD6,0xFF,0x30,0xD6,0xD6,0xF6,0xFF,0xDC,0xF6,0xB5,0x57,0xFF,0x99,0x00,0x00,0x00,0xD6,0xFF,0x1A,0x00,0x00,0xB5,0xFF,0x4E,0xFF,0x67,0x57,0xFF,0x99,0x00,0x00,0x00,0xD6,0xFF,0x26,0x1A,0x1A,0xBD,0xFF,0x3A,0xDE,0xB6,0x5D,0xFF,0x99,0x00,0x00,0x00,0xD6,0xFF,0x99,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x8C,0xFF,0x99,0x00,0x00,0x00,0xD6,0xFF,0x45,0x57,0x57,0x57,0x57,0x57,0x57,0x57,0x6A,0xFF,0x99,0x00,0x00,0x00,0xD6,0xFF,0xA3,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0xBD,0xFF,0x99,0x00,0x00,0x00,0xD6,0xFF,0xDC,0xD6,0xD6,0xD6,0xD6,0xD6,0xD6,0xD6,0xEB,0xFF,0x99,0x00,0x00,0x00,0x4A,0x57,0x0A,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1E,0x57,0x35};
static const uint8_t GLYPH_RI[256] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x99,0x82,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xDC,0xD6,0xD6,0xD6,0xD6,0xD6,0xE2,0xFF,0xD6,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0x1A,0x00,0x00,0x00,0x00,0x00,0x36,0xFF,0xD6,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0x1A,0x00,0x00,0x00,0x00,0x00,0x36,0xFF,0xD6,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0x1A,0x00,0x00,0x00,0x00,0x00,0x36,0xFF,0xD6,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xA3,0x99,0x99,0x99,0x99,0x99,0xAF,0xFF,0xD6,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xDC,0xD6,0xD6,0xD6,0xD6,0xD6,0xE2,0xFF,0xD6,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0x1A,0x00,0x00,0x00,0x00,0x00,0x36,0xFF,0xD6,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0x1A,0x00,0x00,0x00,0x00,0x00,0x36,0xFF,0xD6,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0x1A,0x00,0x00,0x00,0x00,0x00,0x36,0xFF,0xD6,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0x30,0x1A,0x1A,0x1A,0x1A,0x1A,0x4A,0xFF,0xD6,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xD6,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0x69,0x57,0x57,0x57,0x57,0x57,0x7D,0xFF,0xD6,0x00,0x00,0x00,0x00,0x00,0x57,0x57,0x0A,0x00,0x00,0x00,0x00,0x00,0x14,0x57,0x4A,0x00};
static const uint8_t GLYPH_HON[256] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x1A,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x99,0xFF,0x36,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x16,0x1A,0x1A,0x1A,0x1A,0xA3,0xFF,0x4A,0x1A,0x1A,0x1A,0x1A,0x10,0x00,0x00,0x00,0xD6,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x99,0x00,0x00,0x00,0x4A,0x57,0x57,0x57,0x88,0xFF,0xFF,0xFF,0x60,0x57,0x57,0x57,0x35,0x00,0x00,0x00,0x00,0x00,0x00,0x0A,0xD6,0xFF,0xFF,0xFF,0x8B,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x9A,0xFF,0xD3,0xFF,0xA9,0xFF,0x56,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x85,0xFF,0x8B,0x99,0xFF,0x38,0xCC,0xF0,0x37,0x00,0x00,0x00,0x00,0x00,0x03,0x85,0xFF,0xCF,0x06,0x99,0xFF,0x36,0x1B,0xE6,0xF0,0x3D,0x00,0x00,0x00,0x10,0xB6,0xFF,0xF0,0x30,0x1A,0xA3,0xFF,0x4A,0x1A,0x50,0xF0,0xFF,0x6E,0x00,0x00,0x14,0xD3,0xD3,0x7A,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x37,0xF0,0x9F,0x00,0x00,0x00,0x14,0x0D,0x1E,0x57,0x57,0xBD,0xFF,0x7D,0x57,0x57,0x00,0x1E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x99,0xFF,0x36,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x99,0xFF,0x36,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x35,0x57,0x14,0x00,0x00,0x00,0x00,0x00};

// Blend a 16x16 grayscale glyph (fg over bg by per-pixel coverage) for smooth AA edges.
static void drawGlyphGray(int gx, int gy, const uint8_t* gly, uint16_t fg, uint16_t bg) {
  int fr = (fg >> 11) & 0x1F, fc = (fg >> 5) & 0x3F, fb = fg & 0x1F;
  int br = (bg >> 11) & 0x1F, bc = (bg >> 5) & 0x3F, bb = bg & 0x1F;
  for (int py = 0; py < 16; py++) for (int px = 0; px < 16; px++) {
    int a = gly[py * 16 + px];
    if (a < 12) continue;
    int r = br + ((fr - br) * a) / 255, c = bc + ((fc - bc) * a) / 255, b = bb + ((fb - bb) * a) / 255;
    tft.drawPixel(gx + px, gy + py, (uint16_t)((r << 11) | (c << 5) | b));
  }
}

// Header — ◉ SEIS · REGION (left), HH:MM · WIFI + cog (right). Soft-HUD, mono.
void drawHeader() {
  tft.fillRect(0, 0, SCREEN_WIDTH, HEADER_H - 1, currentTheme.background);
  tft.drawFastHLine(0, HEADER_H - 1, SCREEN_WIDTH, PANEL_EDGE);
  tft.setTextFont(1);

  // ── Left: ◉ SEIS · <REGION> (primary) ──
  uint16_t acc = currentTheme.textAccent;
  tft.drawCircle(7, 10, 3, acc);
  tft.fillCircle(7, 10, 1, acc);
  tft.setTextColor(acc);
  tft.setCursor(14, 7);
  tft.print("SEISMONITOR ");
  int dx = tft.getCursorX();
  tft.fillCircle(dx + 1, 11, 1, acc);                 // · separator
  int rx = dx + 4;
  if (strcmp(config.region, "NZ") == 0) {
    tft.setCursor(rx, 7); tft.print("AOTEAROA NZ");
  } else if (strcmp(config.region, "China") == 0) {            // 中国
    drawGlyphGray(rx, 3, GLYPH_ZHONG, acc, currentTheme.background);
    drawGlyphGray(rx + 18, 3, GLYPH_GUO, acc, currentTheme.background);
    tft.setCursor(rx + 38, 7); tft.print("CHINA");
  } else if (strcmp(config.region, "Japan") == 0) {            // 日本
    drawGlyphGray(rx, 3, GLYPH_RI, acc, currentTheme.background);
    drawGlyphGray(rx + 18, 3, GLYPH_HON, acc, currentTheme.background);
    tft.setCursor(rx + 38, 7); tft.print("JAPAN");
  } else {
    tft.setCursor(rx, 7); tft.print(config.region);            // California / Global
  }

  // ── Right: HH:MM · WIFI + cog (secondary) ──
  uint16_t sec = currentTheme.textSecondary;
  drawGearIcon(GEAR_CX, GEAR_CY, sec);

  char clk[8];
  time_t tnow = time(nullptr);
  if (tnow > 1600000000) {
    struct tm lt; localtime_r(&tnow, &lt);
    snprintf(clk, sizeof(clk), "%02d:%02d", lt.tm_hour, lt.tm_min);
  } else {
    strcpy(clk, "--:--");
  }
  uint16_t wifiCol = (WiFi.status() == WL_CONNECTED) ? sec : currentTheme.sub;
  int xc = GEAR_CX - 8 - 6;                            // just left of the cog
  int wW = tft.textWidth("WIFI");
  tft.setTextColor(wifiCol);
  tft.setCursor(xc - wW, 7);
  tft.print("WIFI");
  int dotx = xc - wW - 5;
  tft.fillCircle(dotx, 11, 1, sec);                   // · separator
  int cW = tft.textWidth(clk);
  tft.setTextColor(sec);
  tft.setCursor(dotx - 4 - cW, 7);
  tft.print(clk);
}

void drawUI() {
  tft.fillScreen(currentTheme.background);
  drawHeader();
  drawDataPanel();   // each panel draws its own bordered frame
  drawMap();
  drawSeismograph();
}

void drawSeismograph() {
  // Faint grid dots backdrop
  for (int y = SEISMO_Y; y <= SEISMO_Y + SEISMO_HEIGHT; y += 9)
    for (int x = SEISMO_LINE_X; x < SEISMO_LINE_X + SEISMO_LINE_W; x += 8)
      tft.drawPixel(x, y, currentTheme.seismoGrid);

  // Centre baseline
  tft.drawFastHLine(SEISMO_LINE_X, SEISMO_CENTER_Y, SEISMO_LINE_W, currentTheme.sub);

  // Z / 60s corner labels
  tft.setTextFont(1);
  tft.setTextColor(currentTheme.sub);
  tft.setCursor(SEISMO_X + 2, SEISMO_Y + 2);                tft.print("Z");
  tft.setCursor(SEISMO_X + SEISMO_WIDTH - 18, SEISMO_Y + 2); tft.print("60s");

  tft.drawRoundRect(PAD, SEISMO_PANEL_Y, LEFT_W, SEISMO_PANEL_H, 3, PANEL_EDGE);   // panel frame
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

// Convert a UTF-8 place name to renderable ASCII. Māori macron vowels
// (ā ē ī ō ū + uppercase) collapse to their base letter and macronAt[] is set
// true at that slot, so the caller can stroke a macron bar above it. Other
// multi-byte UTF-8 runs are dropped cleanly. Returns the ASCII length.
int demacron(const char* in, char* out, bool* macronAt, int maxOut) {
  int o = 0;
  for (int i = 0; in[i] && o < maxOut - 1; ) {
    uint8_t c = (uint8_t)in[i];
    if (c < 0x80) {                                   // plain ASCII
      out[o] = in[i]; macronAt[o] = false; o++; i++;
    } else if ((c == 0xC4 || c == 0xC5) && in[i + 1]) {
      uint8_t d = (uint8_t)in[i + 1];
      char base = 0;                                  // odd code point = lowercase
      if (c == 0xC4) {
        if      (d == 0x80 || d == 0x81) base = (d & 1) ? 'a' : 'A';   // Ā ā
        else if (d == 0x92 || d == 0x93) base = (d & 1) ? 'e' : 'E';   // Ē ē
        else if (d == 0xAA || d == 0xAB) base = (d & 1) ? 'i' : 'I';   // Ī ī
      } else {                                        // 0xC5
        if      (d == 0x8C || d == 0x8D) base = (d & 1) ? 'o' : 'O';   // Ō ō
        else if (d == 0xAA || d == 0xAB) base = (d & 1) ? 'u' : 'U';   // Ū ū
      }
      if (base) { out[o] = base; macronAt[o] = true; o++; }
      i += 2;                                         // consumed the 2-byte char
    } else if (c >= 0xC0) {                           // other lead byte — skip its run
      i++; while (((uint8_t)in[i] & 0xC0) == 0x80) i++;
    } else { i++; }                                   // stray continuation byte
  }
  out[o] = '\0';
  return o;
}

// NZ place names GeoNet sends WITHOUT macrons → their correct macronised form.
// GeoNet's API drops the macrons; the device puts them back. Extend freely — and
// worth having a GeoNet/Te Reo contact verify the spellings. (Only confident entries
// included; an unsure name should be omitted rather than wrongly macronised.)
struct MacronName { const char* ascii; const char* macron; };
static const MacronName NZ_MACRONS[] = {
  {"Taupo","Taupō"},{"Turangi","Tūrangi"},{"Whakatane","Whakatāne"},{"Opotiki","Ōpōtiki"},
  {"Otaki","Ōtaki"},{"Whangarei","Whangārei"},{"Kaitaia","Kaitāia"},{"Otorohanga","Ōtorohanga"},
  {"Kuiti","Kūiti"},{"Pokeno","Pōkeno"},{"Paekakariki","Paekākāriki"},{"Kawhia","Kāwhia"},
  {"Piopio","Pīopio"},{"Mokau","Mōkau"},{"Ohura","Ōhura"},{"Maketu","Maketū"},
  {"Matata","Matatā"},{"Taneatua","Tāneatua"},{"Ngongotaha","Ngongotahā"},{"Putaruru","Pūtāruru"},
  {"Tirau","Tīrau"},{"Waiouru","Waiōuru"},{"Wairakei","Wairākei"},{"Okato","Ōkato"},
  {"Papamoa","Pāpāmoa"},{"Omokoroa","Ōmokoroa"},{"Waihi","Waihī"},{"Whangamata","Whangamatā"},
  {"Ohau","Ōhau"},{"Tutira","Tūtira"},{"Ohope","Ōhope"},{"Patea","Pātea"},
  {"Porangahau","Pōrangahau"},{"Eketahuna","Eketāhuna"},{"Ruatoria","Ruatōria"},{"Torere","Tōrere"},
  {"Ngaruawahia","Ngāruawāhia"},{"Maungaturoto","Maungatūroto"},{"Hawera","Hāwera"},{"Owhango","Ōwhango"},
  {"Pauatahanui","Pāuatahanui"},{"Ohaupo","Ōhaupō"},{"Titahi","Tītahi"},{"Kaikoura","Kaikōura"},
  {"Wanaka","Wānaka"},{"Takaka","Tākaka"},
};
static bool isLetterC(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

// Restore macrons on whole-word matches (so "Otaki" can't match inside "Otakiri", etc.).
static void restoreMacrons(String& s) {
  for (unsigned i = 0; i < sizeof(NZ_MACRONS) / sizeof(NZ_MACRONS[0]); i++) {
    const char* f = NZ_MACRONS[i].ascii; int fl = strlen(f), idx = 0;
    while ((idx = s.indexOf(f, idx)) >= 0) {
      char b = (idx == 0) ? ' ' : s.charAt(idx - 1);
      int ai = idx + fl; char a = (ai >= (int)s.length()) ? ' ' : s.charAt(ai);
      if (!isLetterC(b) && !isLetterC(a)) {
        s = s.substring(0, idx) + NZ_MACRONS[i].macron + s.substring(ai);
        idx += strlen(NZ_MACRONS[i].macron);
      } else idx += fl;
    }
  }
}

// Abbreviate GeoNet/USGS boilerplate so long names fit: "<n> km <dir> of" -> "<n>km <DIR> of".
void abbreviatePlace(const char* in, char* out, int maxOut) {
  if (!in) { out[0] = '\0'; return; }
  String s = in;
  s.replace(" km ", "km ");
  // Compound directions abbreviate (spelled out they're too long); single
  // directions stay spelled out (north/south/east/west) per preference.
  s.replace(" north-west of ", " NW of "); s.replace(" north-east of ", " NE of ");
  s.replace(" south-west of ", " SW of "); s.replace(" south-east of ", " SE of ");
  // Global view: the country matters more than the distance from a small town, and
  // long "City, Country" names truncate — so drop the leading "<dist>km <dir> of ".
  if (strcmp(config.region, "Global") == 0) {
    int ofIdx = s.indexOf(" of ");
    if (ofIdx >= 0 && ofIdx < 15) s = s.substring(ofIdx + 4);   // keep "Place, Country"
  } else if (strcmp(config.region, "NZ") == 0) {
    restoreMacrons(s);                                          // GeoNet drops macrons — restore them
  } else {
    int ci = s.lastIndexOf(',');                               // Japan/California/China: the trailing
    if (ci > 0) s = s.substring(0, ci);                        // ", Region" is redundant + truncates — drop it
  }
  strncpy(out, s.c_str(), maxOut - 1); out[maxOut - 1] = '\0';
}

// Wrap demacron'd ASCII `a` at the CURRENT font into <= maxLines; returns line
// count and sets `overflow` true if text remained past the cap.
static int wrapMeasure(const char* a, int len, int maxW, int maxLines, bool& overflow) {
  int pos = 0, lines = 0; char ln[80];
  while (pos < len && lines < maxLines) {
    int rem = min(len - pos, 79); strncpy(ln, a + pos, rem); ln[rem] = '\0';
    int end;
    if (tft.textWidth(ln) <= maxW) end = pos + rem;
    else {
      end = pos; int ls = -1;
      for (int i = pos; i < len && i - pos < 79; i++) {
        if (a[i] == ' ') ls = i;
        int s = i + 1 - pos; strncpy(ln, a + pos, s); ln[s] = '\0';
        if (tft.textWidth(ln) > maxW) { end = (ls > pos) ? ls : i; break; }
        end = i + 1;
      }
    }
    if (end <= pos) end = pos + 1;
    lines++; pos = end; while (pos < len && a[pos] == ' ') pos++;
  }
  overflow = (pos < len);
  return lines < 1 ? 1 : lines;
}

// Choose font for a place name: FreeSans9pt if it fits in 3 lines, else the
// smaller built-in Font 1 (up to 5 lines) so long names still fit. Sets small + lineH.
static int placeLayout(const char* a, int len, int maxW, bool& small, int& lineH) {
  bool ov;
  tft.setFreeFont(FONT_LABEL);
  int n = wrapMeasure(a, len, maxW, 3, ov);
  if (!ov) { small = false; lineH = 14; return n; }
  tft.setTextFont(1);
  n = wrapMeasure(a, len, maxW, 5, ov);
  small = true; lineH = 10; return n;
}

// Pixel height the place name will occupy (matches drawPlaceName's font choice).
int placeNameHeight(const char* text, int maxW) {
  char a[80]; bool m[80];
  int len = demacron(text, a, m, sizeof(a));
  bool small; int lineH;
  return placeLayout(a, len, maxW, small, lineH) * lineH;
}

// True if the word right after space `sp` is a 2-letter connector ("of"/"de") — keep
// it glued to the preceding word so it never strands on its own line.
static bool connectorAfter(const char* a, int sp, int len) {
  int j = sp + 1;
  if (j + 1 >= len) return false;
  bool boundary = (j + 2 >= len) || (a[j + 2] == ' ');
  if (!boundary) return false;
  return (a[j] == 'o' && a[j + 1] == 'f') || (a[j] == 'd' && a[j + 1] == 'e');
}

// Place name fitted into a box (x,y; width maxW, height maxH). ALWAYS one font
// (FreeSans9pt, with Māori macron bars) — no pixel-font switching, so it reads the
// same on every region. Wraps to as many 14px lines as fit maxH; a genuinely huge
// name (rare, after abbreviation) trims the last line with "...".
void drawPlaceNameFit(const char* text, int x, int y, int maxW, int maxH) {
  char a[80]; bool mc[80];
  int len = demacron(text, a, mc, sizeof(a));
  tft.setFreeFont(FONT_LABEL);
  tft.setTextColor(currentTheme.textPrimary);
  const int lineH = 14, base = 11;
  int maxLines = maxH / lineH; if (maxLines < 1) maxLines = 1;
  int pos = 0, lines = 0; char line[88];
  while (pos < len && lines < maxLines) {
    int rem = min(len - pos, 79); strncpy(line, a + pos, rem); line[rem] = '\0';
    int end;
    if (tft.textWidth(line) <= maxW) end = pos + rem;
    else {
      end = pos; int lastSpace = -1;
      for (int i = pos; i < len && i - pos < 79; i++) {
        if (a[i] == ' ' && !connectorAfter(a, i, len)) lastSpace = i;   // don't break before "of"/"de"
        int seg = i + 1 - pos; strncpy(line, a + pos, seg); line[seg] = '\0';
        if (tft.textWidth(line) > maxW) { end = (lastSpace > pos) ? lastSpace : i; break; }
        end = i + 1;
      }
    }
    int pl = end - pos; if (pl <= 0) pl = 1;
    int lineY = y + lines * lineH + base;

    if (lines == maxLines - 1 && end < len) {         // last line + more text → ellipsis
      while (pl > 0) {
        char t2[92]; snprintf(t2, sizeof(t2), "%.*s...", pl, a + pos);
        if (tft.textWidth(t2) <= maxW) break;
        pl--;
      }
      char outl[92]; snprintf(outl, sizeof(outl), "%.*s...", pl, a + pos);
      tft.setCursor(x, lineY); tft.print(outl);
      break;
    }

    strncpy(line, a + pos, pl); line[pl] = '\0';
    while (pl > 0 && line[pl - 1] == ' ') line[--pl] = '\0';
    tft.setCursor(x, lineY);
    tft.print(line);

    for (int k = 0; k < pl; k++) {                    // Māori macron bars
      if (!mc[pos + k]) continue;
      char pre[80]; strncpy(pre, a + pos, k); pre[k] = '\0';
      int vx = x + tft.textWidth(pre);
      char ch[2] = { a[pos + k], '\0' };
      int vw = tft.textWidth(ch);
      bool upper = (a[pos + k] >= 'A' && a[pos + k] <= 'Z');
      tft.drawFastHLine(vx + 1, lineY - (upper ? 15 : 12), (vw > 3) ? vw - 2 : vw, currentTheme.textPrimary);
    }
    lines++;
    pos = end; while (pos < len && a[pos] == ' ') pos++;
    if (pos >= len) break;
  }
}

// One data cell: ◆ LABEL + magnitude on row 1; place name fills the middle (auto-fit);
// meta (time · depth) pinned to the bottom so a long name can never bury it.
void drawDataCell(int cellY, int cellH, EarthquakeData &q, const char* label, uint16_t accent) {
  int xL = DATA_X + 7;
  int maxW = DATA_WIDTH - 12;
  int topY = cellY + 4;

  // ── Row 1: ◆ LABEL (left, centred on the magnitude) + M#.# hero (right) ──
  int rowMid = topY + 8;
  tft.setTextFont(1);
  tft.setTextColor(accent);
  tft.setCursor(xL, rowMid - 4);                      // diamond removed — frees room so "24H HIGH" + M#.# never collide
  tft.print(label);

  if (!q.isValid) {
    tft.setTextColor(currentTheme.textSecondary);
    tft.setCursor(xL, topY + 24); tft.print("NO DATA");
    return;
  }

  char m[8]; snprintf(m, sizeof(m), "M%.1f", q.magnitude);
  tft.setFreeFont(FONT_DATA);                        // compact bold — stands out by weight, not size
  tft.setTextColor(accent);
  int mw = tft.textWidth(m);
  tft.setCursor(xL + maxW - mw, topY + 14);          // right-aligned, centred on the label row
  tft.print(m);

  // ── Meta pinned to the bottom of the cell (always visible) ──
  int metaY = cellY + cellH - 9;
  String ago = getTimeAgo(q.timestamp); ago.toUpperCase();
  tft.setTextFont(1);
  tft.setTextColor(currentTheme.textSecondary);
  tft.setCursor(xL, metaY);
  tft.print(ago); tft.print(" AGO");
  int mx = tft.getCursorX();
  tft.fillCircle(mx + 3, metaY + 3, 1, currentTheme.textSecondary);
  char dep[10]; snprintf(dep, sizeof(dep), " %dKM", (int)q.depth);
  tft.setCursor(mx + 5, metaY);
  tft.print(dep);

  // ── Place name fills the gap between row 1 and the meta (one font, never overlaps) ──
  int placeTop = topY + 18;
  drawPlaceNameFit(q.location, xL, placeTop, maxW, metaY - 3 - placeTop);
}

void drawDataPanel() {
  tft.fillRect(DATA_X, DATA_Y, DATA_WIDTH, DATA_HEIGHT, currentTheme.background);
  int cellH = (DATA_HEIGHT - 1) / 2;
  drawDataCell(DATA_Y, cellH, latestQuake, "LATEST", currentTheme.dataLatest);
  int divY = DATA_Y + cellH;
  tft.drawFastHLine(DATA_X + 8, divY, DATA_WIDTH - 16, PANEL_EDGE_DIM);
  drawDataCell(divY + 1, DATA_HEIGHT - cellH - 1, highestRegionalQuake, "24H HIGH", currentTheme.dataHighest);
  tft.drawRoundRect(DATA_X, DATA_Y, DATA_WIDTH, DATA_HEIGHT, 3, PANEL_EDGE);
}

// Sparse dashed circle — softer than a solid ring (radar look).
void drawDashedCircle(int cx, int cy, int r, uint16_t col) {
  int n = (int)(6.2832f * r / 2.2f);
  if (n < 16) n = 16;
  for (int i = 0; i < n; i++) {
    if ((i & 3) == 0) {                    // 1 dot on, 3 off
      float a = (i * 6.2831853f) / n;
      tft.drawPixel(cx + (int)(r * cosf(a)), cy + (int)(r * sinf(a)), col);
    }
  }
}

// Concentric distance rings + crosshair + KM labels, centred on (MAP_CX,MAP_CY).
void drawRings() {
  const int rad[4] = {30, 54, 78, 98};
  const uint16_t rc[4] = {currentTheme.ring1, currentTheme.ring2, currentTheme.ring3, currentTheme.ring4};
  for (int i = 0; i < 4; i++) drawDashedCircle(MAP_CX, MAP_CY, rad[i], rc[i]);
  tft.drawFastHLine(MAP_CX - 6, MAP_CY, 13, currentTheme.textSecondary);
  tft.drawFastVLine(MAP_CX, MAP_CY - 6, 13, currentTheme.textSecondary);
}

// Epicentre marker — a target ring sized by magnitude with the core dot on the
// (projected) location, plus an M-value tag that ties it to the data block.
void drawMarker(float lat, float lon, uint16_t col, float mag) {
  int x = mapLonToScreen(lon), y = mapLatToScreen(lat);
  if (x < MAP_X || x > MAP_X + MAP_WIDTH || y < MAP_Y || y > MAP_Y + MAP_HEIGHT) return;
  int r = constrain(4 + (int)mag, 4, 12);    // ring radius reflects magnitude
  tft.drawCircle(x, y, r, col);
  tft.drawCircle(x, y, r - 1, col);          // 2px ring
  tft.fillCircle(x, y, 2, col);              // core dot on the location

  // Magnitude tag (same colour as the data block) — right of the dot, or left near the edge
  char m[8]; snprintf(m, sizeof(m), "M%.1f", mag);
  tft.setTextFont(1);
  tft.setTextColor(col);
  int lw = tft.textWidth(m);
  int lx = x + r + 3;
  if (lx + lw > MAP_X + MAP_WIDTH - 2) lx = x - r - 3 - lw;   // flip to stay on the panel
  tft.setCursor(lx, y - 3);
  tft.print(m);
}

void drawMap() {
  bool isGlobal = (strcmp(config.region, "Global") == 0);
  tft.fillRect(MAP_X + 1, MAP_Y + 1, MAP_WIDTH - 2, MAP_HEIGHT - 2, currentTheme.mapOcean);  // inside the border
  if (!isGlobal) drawRings();                           // radar rings (flat regions only)

  if      (strcmp(config.region, "NZ") == 0)         drawNZMap();
  else if (strcmp(config.region, "Japan") == 0)      drawJapanMap();
  else if (strcmp(config.region, "China") == 0)      drawChinaMap();
  else if (strcmp(config.region, "California") == 0) drawCaliforniaMap();
  else                                               drawGlobalMap();   // self-contained globe

  if (!isGlobal) {
    // Recent quakes (optional, faint) + the flat target markers
    if (config.showRecentQuakes) {
      for (int i = 0; i < recentQuakeCount; i++) {
        if (!recentQuakes[i].valid) continue;
        int x = mapLonToScreen(recentQuakes[i].lon);
        int y = mapLatToScreen(recentQuakes[i].lat);
        if (x < MAP_X || x > MAP_X + MAP_WIDTH || y < MAP_Y || y > MAP_Y + MAP_HEIGHT) continue;
        tft.fillCircle(x, y, (recentQuakes[i].mag >= 6.0) ? 2 : 1, currentTheme.sub);
      }
    }
    if (latestQuake.isValid)
      drawMarker(latestQuake.latitude, latestQuake.longitude, currentTheme.dataLatest, latestQuake.magnitude);
    // Skip the 24h-high marker when it's the same quake/spot as the latest — otherwise the
    // two rings + magnitude tags overlap and the number is unreadable.
    bool sameSpot = latestQuake.isValid && highestRegionalQuake.isValid &&
                    (latestQuake.timestamp == highestRegionalQuake.timestamp ||
                     (fabsf(latestQuake.latitude  - highestRegionalQuake.latitude)  < 0.3f &&
                      fabsf(latestQuake.longitude - highestRegionalQuake.longitude) < 0.3f));
    if (highestRegionalQuake.isValid && !sameSpot)
      drawMarker(highestRegionalQuake.latitude, highestRegionalQuake.longitude, currentTheme.dataHighest, highestRegionalQuake.magnitude);

    // Data-source credit — bottom-right of the map (GeoNet for NZ, USGS otherwise)
    const char* src = (strcmp(config.region, "NZ") == 0) ? "POWERED BY GEONET" : "POWERED BY USGS";
    tft.setTextFont(1);
    tft.setTextColor(currentTheme.textSecondary);
    int sw = tft.textWidth(src);
    tft.setCursor(MAP_X + MAP_WIDTH - sw - 6, MAP_Y + MAP_HEIGHT - 11);
    tft.print(src);
  }

  tft.drawRoundRect(MAP_X, MAP_Y, MAP_WIDTH, MAP_HEIGHT, 3, PANEL_EDGE);   // panel frame on top
}

// Live "active zone" ping — for 10 min after a quake, expanding rings radiate
// from its epicentre. A clipped viewport re-stamps the map under the rings each
// frame so the coastline/markers stay intact (no full-screen flicker).
void animateMapPing() {
  if (!latestQuake.isValid) return;
  int ex = mapLonToScreen(latestQuake.longitude);
  int ey = mapLatToScreen(latestQuake.latitude);
  if (ex < MAP_X || ex > MAP_X + MAP_WIDTH || ey < MAP_Y || ey > MAP_Y + MAP_HEIGHT) return;

  const int R = 24;
  int bx = max(ex - R, MAP_X), by = max(ey - R, MAP_Y);
  int bw = min(ex + R, MAP_X + MAP_WIDTH)  - bx;
  int bh = min(ey + R, MAP_Y + MAP_HEIGHT) - by;
  if (bw <= 0 || bh <= 0) return;

  tft.setViewport(bx, by, bw, bh, false);   // clip to box, keep absolute coords
  drawMap();                                 // re-stamp static map (clears old rings)

  unsigned long ph = millis() % 1600UL;
  for (int k = 0; k < 2; k++) {              // two staggered expanding rings
    float p = (float)((ph + k * 800UL) % 1600UL) / 1600.0f;
    int r = (int)(p * R);
    if (r >= 2)
      tft.drawCircle(ex, ey, r, (p < 0.6f) ? currentTheme.dataLatest : currentTheme.textSecondary);
  }
  tft.fillCircle(ex, ey, 2, currentTheme.dataLatest);   // bright core

  tft.resetViewport();
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
  float halfH   = (SEISMO_HEIGHT / 2) + 6;  // allows wave to clip edges on big quakes
  float rawAmp  = pow(10.0f, 0.8f * mag)        / distKm;
  float refAmp  = pow(10.0f, 0.8f * 5.0f)       / 200.0f;
  float scaled  = constrain((rawAmp / refAmp) * halfH * 2.5f, 8.0f, (float)halfH);  // exaggerated

  seismoPAmp    = scaled * 0.45f;   // P-waves are smallest
  seismoSAmp    = scaled * 0.90f;   // S-waves intermediate
  seismoSurfAmp = scaled * 1.30f;   // Surface waves largest

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

    case SEISMO_QUIET: {
      // Continuous microseismic tremor — always alive (never flat), lightly exaggerated
      float amb = SEISMO_HEIGHT * 0.20f;
      displacement = (int)(amb * (0.45f * sin(2.0f * PI * 0.7f * t) +
                                  0.30f * sin(2.0f * PI * 1.9f * t) +
                                  0.25f * sin(2.0f * PI * 3.7f * t + 1.3f)));
      displacement += random(-3, 4);
      if (random(100) < 6) displacement += random(-6, 7);   // occasional flutter
      break;
    }

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

  // Restore backdrop (grid dots + centre baseline) in the erased strip
  for (int x = eraseX; x < eraseX + 2 && x < SEISMO_LINE_X + SEISMO_LINE_W; x++) {
    if (x < SEISMO_LINE_X) continue;
    for (int y = SEISMO_Y; y <= SEISMO_Y + SEISMO_HEIGHT; y += 9)
      if ((x - SEISMO_LINE_X) % 8 == 0) tft.drawPixel(x, y, currentTheme.seismoGrid);
    tft.drawPixel(x, SEISMO_CENTER_Y, currentTheme.sub);   // keep baseline alive
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

// Subtle geological feature (land fault) as a dotted polyline (GEO_FAULT — visible-but-subtle).
void drawGeoDotted(const float pts[][2], int n) {
  for (int i = 0; i < n - 1; i++) {
    int x0 = mapLonToScreen(pts[i][1]),   y0 = mapLatToScreen(pts[i][0]);
    int x1 = mapLonToScreen(pts[i + 1][1]), y1 = mapLatToScreen(pts[i + 1][0]);
    int steps = max(abs(x1 - x0), abs(y1 - y0));
    for (int s = 0; s <= steps; s += 2) {
      int px = x0 + s * (x1 - x0) / max(steps, 1);
      int py = y0 + s * (y1 - y0) / max(steps, 1);
      if (px > MAP_X && px < MAP_X + MAP_WIDTH && py > MAP_Y && py < MAP_Y + MAP_HEIGHT)
        tft.drawPixel(px, py, GEO_FAULT);
    }
  }
}

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

  // Geological features (subtle) — Alpine Fault + Hikurangi subduction margin
  static const float alpineFault[][2] = {
    {-44.10,168.55},{-43.55,169.45},{-43.05,170.25},{-42.55,171.10},{-42.05,171.95},{-41.70,172.75}
  };
  static const float hikurangi[][2] = {
    {-41.60,176.30},{-40.70,177.40},{-39.50,178.20},{-38.20,178.90},{-37.20,179.30}
  };
  drawGeoDotted(alpineFault, 6);
  drawGeoDotted(hikurangi, 5);

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
  // (Ryukyu chain is drawn dim + clipped below with the trenches — it was a bright unclipped
  //  line stretching off toward Taiwan, which looked like a tail. Now it's subtle texture.)

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
  // More ocean trenches for subtle texture (all clipped to the panel by drawDotted)
  const float kuril[][2]    = {{45.20,146.00},{43.50,145.00},{42.00,144.00},{40.50,143.50}};   // Kuril Trench (NE)
  const float izuBonin[][2] = {{33.00,141.20},{31.50,141.80},{30.20,142.30}};                  // Izu-Ogasawara (S)
  drawDotted(japanTrench,  6, GEO_DIM);
  drawDotted(nankaiTrough, 6, GEO_DIM);
  drawDotted(kuril,        4, GEO_DIM);
  drawDotted(izuBonin,     3, GEO_DIM);
  drawDotted(ryukyu,      11, GEO_DIM);   // island chain — subtle dotted, clipped (no bright tail)

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
    // NE (Heilongjiang) — Amur / Ussuri "head"
    {53.30,123.30}, {52.30,124.40}, {50.00,127.30}, {48.90,128.70}, {48.50,135.00},
    {47.30,134.10}, {45.50,133.00}, {44.30,131.50}, {43.00,131.20},
    // Korean border (Tumen -> Yalu)
    {42.40,130.50}, {42.00,129.50}, {41.40,128.10}, {41.00,126.50}, {40.40,125.10}, {39.85,124.35},
    // Liaodong Peninsula + Bohai gulf
    {40.00,122.10}, {39.00,121.65}, {38.72,121.18}, {39.50,121.55}, {40.10,121.85},
    {40.92,121.65}, {40.20,120.00}, {39.10,118.90}, {38.60,117.90}, {38.15,118.30}, {37.55,118.95},
    // Shandong Peninsula (the eastward fist)
    {37.20,119.30}, {37.85,120.75}, {37.50,122.15}, {37.00,122.60}, {36.60,122.45},
    {36.65,121.30}, {36.05,120.30}, {35.55,119.60}, {34.95,119.20},
    // Jiangsu -> Yangtze mouth -> Hangzhou Bay
    {34.30,120.20}, {33.40,120.50}, {32.60,120.95}, {32.00,121.75}, {31.45,121.90},
    {30.85,121.85}, {30.40,121.00}, {30.15,120.55}, {30.20,121.70}, {29.50,121.85}, {28.80,121.60}, {28.30,121.20},
    // SE coast (Zhejiang -> Fujian -> Guangdong, Pearl estuary)
    {27.40,120.65}, {26.60,120.10}, {26.00,119.55}, {25.30,119.35}, {24.80,118.80},
    {24.45,118.10}, {23.70,117.25}, {23.35,116.80}, {22.95,116.50}, {22.75,115.50},
    {22.60,114.85}, {22.50,114.15}, {22.20,113.55}, {21.95,113.25}, {21.80,112.30}, {21.50,111.00},
    // Leizhou Peninsula (faces Hainan)
    {21.15,110.55}, {20.50,110.40}, {20.25,110.10}, {20.45,109.80}, {21.05,109.60}, {21.50,109.05},
    // South coast + Vietnam border
    {21.60,108.20}, {22.50,106.60}, {22.90,106.70}, {23.15,105.30}, {22.55,103.95}, {22.40,102.10},
    // SW borders (Yunnan / Laos / Myanmar)
    {21.80,101.70}, {21.15,101.15}, {21.15,100.10}, {24.00,98.50}, {25.60,98.05}, {27.50,98.50}, {28.20,97.45},
    // India / Nepal / Tibet border
    {28.50,96.40}, {27.80,91.70}, {28.10,87.00}, {30.00,81.30}, {32.00,79.00}, {34.50,78.20},
    // Xinjiang far west
    {35.60,76.80}, {37.00,75.10}, {39.50,73.60}, {40.30,74.90},
    // NW borders (Kazakhstan / Mongolia)
    {41.05,79.90}, {44.00,80.30}, {45.50,82.50}, {47.00,85.50}, {47.90,85.70}, {49.10,87.90},
    {48.90,89.60}, {49.50,94.00}, {50.30,97.30}, {49.20,100.50}, {47.90,102.10}, {46.50,104.00},
    {45.00,107.50}, {42.50,109.00}, {42.60,112.40}, {43.70,116.60}, {45.00,119.50}, {47.00,120.00},
    {50.00,119.80}, {52.60,120.10}, {53.30,123.30},
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

  // Geological features (subtle) — major active fault systems
  static const float longmenshan[][2] = {{30.6,103.0},{31.5,103.8},{32.4,104.7},{33.0,105.4}};
  static const float kunlun[][2]      = {{35.5,90.0},{35.8,95.0},{35.9,99.0},{36.0,103.0}};
  static const float altynTagh[][2]   = {{36.5,82.0},{37.5,87.0},{38.5,91.0},{39.5,94.5}};
  static const float xianshuihe[][2]  = {{32.0,100.0},{30.8,101.4},{29.5,102.3},{28.0,103.0}};
  static const float tianshan[][2]    = {{42.0,76.0},{42.4,82.0},{42.8,87.0},{43.2,92.0}};
  drawGeoDotted(longmenshan, 4);
  drawGeoDotted(kunlun, 4);
  drawGeoDotted(altynTagh, 4);
  drawGeoDotted(xianshuihe, 4);
  drawGeoDotted(tianshan, 4);

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
    // Pacific coast — Oregon border south to Mexico (densified)
    {42.00, -124.21}, {41.75, -124.18}, {41.45, -124.07}, {41.10, -124.15},
    {40.80, -124.18}, {40.44, -124.40}, {40.26, -124.36}, {40.02, -124.08},
    {39.70, -123.82}, {39.35, -123.80}, {39.10, -123.72}, {38.95, -123.65},
    {38.74, -123.52}, {38.50, -123.22}, {38.35, -123.05}, {38.11, -122.95},
    {38.00, -122.98}, {37.92, -122.70}, {37.82, -122.52}, {37.66, -122.51},
    {37.55, -122.50}, {37.34, -122.41}, {37.18, -122.38}, {36.97, -122.03},
    {36.83, -121.81}, {36.62, -121.92}, {36.55, -121.95}, {36.42, -121.91},
    {36.30, -121.85}, {36.10, -121.62}, {35.89, -121.48}, {35.66, -121.28},
    {35.45, -121.00}, {35.30, -120.85}, {35.17, -120.72}, {34.92, -120.65},
    {34.70, -120.62}, {34.52, -120.52}, {34.45, -120.47}, {34.42, -120.12},
    {34.40, -119.85}, {34.27, -119.52}, {34.08, -119.25}, {34.05, -119.00},
    {33.97, -118.82}, {33.86, -118.56}, {33.72, -118.40}, {33.72, -118.28},
    {33.60, -117.93}, {33.46, -117.72}, {33.38, -117.60}, {33.18, -117.42},
    {33.00, -117.28}, {32.85, -117.26}, {32.72, -117.17}, {32.53, -117.12},
    // Mexico + Arizona/Nevada/Oregon borders
    {32.54, -116.10}, {32.72, -114.72}, {33.00, -114.63}, {34.00, -114.43},
    {35.00, -114.63}, {36.00, -114.75}, {36.20, -115.90}, {37.00, -117.00},
    {38.00, -118.00}, {39.00, -120.00}, {40.00, -120.00}, {41.00, -120.00},
    {42.00, -120.00}, {42.00, -122.00}, {42.00, -124.21},
  };

  int pts = sizeof(california) / sizeof(california[0]);

  // Land fill
  fillMapPolygon(california, pts, currentTheme.mapLand);

  // Outline
  for (int i = 0; i < pts - 1; i++)
    tft.drawLine(mapLonToScreen(california[i][1]),   mapLatToScreen(california[i][0]),
                 mapLonToScreen(california[i+1][1]), mapLatToScreen(california[i+1][0]), currentTheme.mapOutline);

  // Salton Sea — dark inland lake (Imperial Valley); a recognisable, seismically busy feature
  {
    int ssx = mapLonToScreen(-115.85), ssy = mapLatToScreen(33.30);
    tft.fillEllipse(ssx, ssy, 3, 4, currentTheme.background);
    tft.drawEllipse(ssx, ssy, 3, 4, GEO_DIM);
  }

  // Channel Islands — tiny land specks off the Southern California coast
  tft.fillCircle(mapLonToScreen(-119.75), mapLatToScreen(34.02), 2, currentTheme.mapOutline); // Santa Cruz
  tft.fillCircle(mapLonToScreen(-120.10), mapLatToScreen(33.97), 1, currentTheme.mapOutline); // Santa Rosa
  tft.fillCircle(mapLonToScreen(-118.42), mapLatToScreen(33.39), 1, currentTheme.mapOutline); // Santa Catalina
  tft.fillCircle(mapLonToScreen(-118.55), mapLatToScreen(32.90), 1, currentTheme.mapOutline); // San Clemente

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
        tft.drawPixel(px, py, GEO_DIM);   // Muted teal
    }
  }

  // ── California's fault network — dotted, matching the other regions' geo texture ──
  // San Andreas (the big one) — Mendocino coast all the way south to the Salton Sea
  const float sanAndreas[][2] = {
    {40.30, -124.30}, {39.80, -123.70}, {39.20, -123.30},
    {38.50, -122.95}, {38.00, -122.55}, {37.70, -122.25},
    {37.40, -122.10}, {37.00, -121.85},
    {36.50, -121.20}, {36.00, -120.60}, {35.50, -120.10}, {35.00, -119.60},
    {34.80, -119.20}, {34.60, -118.85}, {34.40, -118.55},
    {34.20, -118.35}, {34.00, -118.20}, {33.80, -118.00},
    {33.60, -117.70}, {33.40, -117.35}, {33.20, -116.95},
    {33.00, -116.50}, {32.80, -116.10}, {32.60, -115.70},
  };
  drawGeoDotted(sanAndreas, sizeof(sanAndreas) / sizeof(sanAndreas[0]));

  // Hayward Fault — East Bay (San Pablo Bay down through Oakland to Fremont)
  const float hayward[][2] = {
    {38.05, -122.30}, {37.90, -122.20}, {37.75, -122.10},
    {37.60, -121.99}, {37.45, -121.88}, {37.30, -121.78},
  };
  drawGeoDotted(hayward, sizeof(hayward) / sizeof(hayward[0]));

  // San Jacinto Fault — San Bernardino SE toward the Imperial Valley (very active)
  const float sanJacinto[][2] = {
    {34.10, -117.46}, {33.90, -117.18}, {33.65, -116.85},
    {33.40, -116.50}, {33.10, -116.05}, {32.80, -115.60},
  };
  drawGeoDotted(sanJacinto, sizeof(sanJacinto) / sizeof(sanJacinto[0]));

  // Garlock Fault — the E-W scarp along the north edge of the Mojave
  const float garlock[][2] = {
    {34.81, -118.90}, {35.00, -118.30}, {35.20, -117.75},
    {35.38, -117.25}, {35.48, -116.70},
  };
  drawGeoDotted(garlock, sizeof(garlock) / sizeof(garlock[0]));

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

// ── Wireframe globe (Global region) — orthographic tilted sphere, lat/lon mesh.
//    Rendered into an off-screen sprite (globeSpr) so it can spin flicker-free;
//    falls back to drawing straight to the panel if the sprite won't allocate.
//    Templated on the draw target T so ONE renderer targets sprite OR screen. ──

void globeProject(float lat, float lon, int cx, int cy, int& sx, int& sy, bool& front) {
  const float cosT = 0.9131f, sinT = 0.4078f;   // tilt 0.42 rad
  float phi = lat * 0.01745329f, lam = lon * 0.01745329f + globeRot;
  float x = cosf(phi) * sinf(lam);
  float y = sinf(phi);
  float z = cosf(phi) * cosf(lam);
  float y2 = y * cosT - z * sinT;
  float z2 = y * sinT + z * cosT;
  sx = cx + (int)(GLOBE_R * x);
  sy = cy - (int)(GLOBE_R * y2);
  front = (z2 > 0.0f);
}

template<class T>
void globePolyT(T* g, const float pts[][2], int n, int cx, int cy, uint16_t bright, uint16_t dim) {
  int px = 0, py = 0; bool pf = false, have = false;
  for (int i = 0; i < n; i++) {
    int sx, sy; bool front; globeProject(pts[i][0], pts[i][1], cx, cy, sx, sy, front);
    if (have) { if (pf && front) g->drawLine(px, py, sx, sy, bright);
                else if (!pf && !front) g->drawLine(px, py, sx, sy, dim); }
    px = sx; py = sy; pf = front; have = true;
  }
}

template<class T>
void globeMarkerT(T* g, float lat, float lon, int cx, int cy, uint16_t col, float mag) {
  int sx, sy; bool front; globeProject(lat, lon, cx, cy, sx, sy, front);
  if (!front) return;                              // back face — hidden (the spin reveals it)
  float dx = sx - cx, dy = sy - cy;
  float len = sqrtf(dx * dx + dy * dy); if (len < 1) len = 1;
  float ux = dx / len, uy = dy / len;
  int tx = sx + (int)(ux * 12), ty = sy + (int)(uy * 12);   // radial spike outward
  g->drawLine(sx, sy, tx, ty, col);
  g->drawCircle(sx, sy, 4, col);
  g->fillCircle(sx, sy, 2, col);
  g->fillCircle(tx, ty, 1, col);
  // M-value tag just past the spike tip, in the data-block colour
  char m[8]; snprintf(m, sizeof(m), "M%.1f", mag);
  g->setTextFont(1);
  g->setTextColor(col);
  int lw = g->textWidth(m);
  int lx = (ux >= 0) ? tx + 2 : tx - 2 - lw;
  g->setCursor(lx, ty - 3);
  g->print(m);
}

// Render the whole globe into target g (sprite or screen), centred at (cx,cy).
template<class T>
void renderGlobe(T* g, int cx, int cy) {
  const uint16_t globeFill = 0x0081, limb = 0x07F1, meshF = 0x05EC, meshB = 0x0120, eqF = 0x07EC;
  g->fillCircle(cx, cy, (int)GLOBE_R, globeFill);
  g->setTextWrap(false);   // a magnitude tag near the edge must clip, NOT wrap to the far side

  for (int lat = -60; lat <= 60; lat += 30) {                 // parallels (equator brighter)
    uint16_t b = (lat == 0) ? eqF : meshF;
    int px = 0, py = 0; bool pf = false, have = false;
    for (int lon = -180; lon <= 180; lon += 8) {
      int sx, sy; bool front; globeProject(lat, lon, cx, cy, sx, sy, front);
      if (have) { if (pf && front) g->drawLine(px, py, sx, sy, b);
                  else if (!pf && !front) g->drawLine(px, py, sx, sy, meshB); }
      px = sx; py = sy; pf = front; have = true;
    }
  }
  for (int lon = -180; lon < 180; lon += 24) {                 // meridians
    int px = 0, py = 0; bool pf = false, have = false;
    for (int lat = -90; lat <= 90; lat += 8) {
      int sx, sy; bool front; globeProject(lat, lon, cx, cy, sx, sy, front);
      if (have) { if (pf && front) g->drawLine(px, py, sx, sy, meshF);
                  else if (!pf && !front) g->drawLine(px, py, sx, sy, meshB); }
      px = sx; py = sy; pf = front; have = true;
    }
  }

  static const float na[][2] = {{71,-156},{68,-166},{60,-164},{56,-156},{59,-139},{54,-130},{48,-124},{42,-124},{34,-120},{32,-117},{27,-114},{23,-110},{21,-105},{18,-103},{16,-95},{19,-91},{18,-88},{14,-83},{9,-80},{14,-84},{20,-90},{25,-82},{27,-80},{30,-82},{35,-76},{40,-73},{44,-67},{47,-60},{52,-56},{55,-60},{60,-64},{63,-77},{67,-63},{70,-85},{73,-100},{72,-125},{71,-156}};
  static const float sa[][2] = {{12,-72},{8,-77},{2,-80},{-4,-81},{-10,-78},{-16,-74},{-23,-70},{-31,-71},{-38,-73},{-44,-75},{-50,-74},{-54,-69},{-51,-68},{-46,-66},{-40,-62},{-37,-57},{-32,-52},{-25,-48},{-23,-43},{-15,-39},{-8,-35},{-3,-39},{0,-50},{5,-52},{8,-60},{11,-65},{12,-72}};
  static const float af[][2] = {{37,10},{33,-6},{27,-13},{21,-17},{14,-17},{8,-13},{4,-7},{5,0},{6,4},{3,9},{-3,11},{-10,14},{-17,12},{-25,16},{-33,18},{-34,24},{-29,31},{-22,35},{-15,40},{-8,40},{-1,42},{6,49},{12,51},{12,44},{16,40},{22,38},{28,34},{31,31},{32,24},{33,18},{35,11},{37,10}};
  static const float eu[][2] = {{43,-9},{37,-9},{36,-2},{40,3},{43,7},{44,12},{40,18},{38,16},{37,23},{41,29},{45,37},{41,41},{37,36},{31,32},{29,35},{25,37},{21,39},{17,42},{15,44},{20,57},{25,57},{23,68},{20,73},{14,80},{8,77},{10,80},{16,82},{21,87},{22,92},{16,95},{9,99},{14,109},{20,107},{22,114},{30,122},{37,122},{40,125},{43,131},{47,138},{52,141},{59,143},{60,154},{62,163},{66,170},{70,160},{72,140},{75,110},{78,95},{80,68},{75,55},{70,50},{66,42},{62,30},{66,22},{70,30},{68,38},{63,30},{59,23},{61,16},{57,8},{55,11},{52,3},{48,-4},{43,-9}};
  static const float au[][2] = {{-12,131},{-11,137},{-14,142},{-19,147},{-25,153},{-32,153},{-38,147},{-38,141},{-35,138},{-32,134},{-34,123},{-32,116},{-26,114},{-20,117},{-15,124},{-12,131}};
  static const float gr[][2] = {{83,-32},{80,-18},{73,-22},{67,-32},{60,-43},{64,-50},{72,-55},{78,-50},{82,-40},{83,-32}};
  static const float mg[][2] = {{-12,49},{-16,50},{-22,48},{-25,45},{-22,43},{-16,44},{-12,49}};
  static const float jp[][2] = {{45,142},{40,140},{35,139},{34,135},{36,137},{38,140},{41,141},{45,142}};
  static const float br[][2] = {{58,-5},{54,-3},{51,1},{50,-5},{54,-6},{57,-7},{58,-5}};
  static const float nz[][2] = {{-34,173},{-39,177},{-41,175},{-46,168},{-43,170},{-38,174},{-34,173}};
  static const float id[][2] = {{5,95},{0,100},{-6,105},{-8,114},{-8,120},{-4,119},{1,110},{4,99},{5,95}};
  #define NPTS(a) (int)(sizeof(a) / sizeof(a[0]))
  globePolyT(g, na, NPTS(na), cx, cy, eqF,   0x0140);
  globePolyT(g, sa, NPTS(sa), cx, cy, eqF,   0x0140);
  globePolyT(g, af, NPTS(af), cx, cy, eqF,   0x0140);
  globePolyT(g, eu, NPTS(eu), cx, cy, eqF,   0x0140);
  globePolyT(g, au, NPTS(au), cx, cy, eqF,   0x0140);
  globePolyT(g, gr, NPTS(gr), cx, cy, meshF, 0x0120);
  globePolyT(g, mg, NPTS(mg), cx, cy, meshF, 0x0120);
  globePolyT(g, jp, NPTS(jp), cx, cy, meshF, 0x0120);
  globePolyT(g, br, NPTS(br), cx, cy, meshF, 0x0120);
  globePolyT(g, nz, NPTS(nz), cx, cy, meshF, 0x0120);
  globePolyT(g, id, NPTS(id), cx, cy, meshF, 0x0120);
  #undef NPTS

  g->drawCircle(cx, cy, (int)GLOBE_R, limb);
  g->drawCircle(cx, cy, (int)GLOBE_R - 1, limb);

  if (latestQuake.isValid)
    globeMarkerT(g, latestQuake.latitude, latestQuake.longitude, cx, cy, currentTheme.dataLatest, latestQuake.magnitude);
  if (highestRegionalQuake.isValid)
    globeMarkerT(g, highestRegionalQuake.latitude, highestRegionalQuake.longitude, cx, cy, currentTheme.dataHighest, highestRegionalQuake.magnitude);
}

void drawGlobalMap() {
  if (!globeSprTried) {                          // lazy one-time sprite allocation (panel interior)
    globeSprTried = true;
    globeSpr.setColorDepth(16);
    globeSprReady = (globeSpr.createSprite(MAP_WIDTH - 2, MAP_HEIGHT - 2) != nullptr);
  }
  if (globeSprReady) {
    globeSpr.fillSprite(currentTheme.mapOcean);
    renderGlobe(&globeSpr, MAP_CX - (MAP_X + 1), MAP_CY - (MAP_Y + 1));   // sprite-local centre
    globeSpr.pushSprite(MAP_X + 1, MAP_Y + 1);                            // inside the border
  } else {
    renderGlobe(&tft, MAP_CX, MAP_CY);           // fallback: straight to the panel (static)
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
  
  float highestMag = 0;    int highestIdx = -1;     // strongest in the whole feed (fallback)
  float highestMag24 = 0;  int highestIdx24 = -1;   // strongest within the last 24 h (preferred)
  int processedCount = 0;
  time_t nowEpoch = time(nullptr);

  recentQuakeCount = 0;

  for (int i = 0; i < totalFeatures && processedCount < 20; i++) {
    JsonObject quake = features[i];

    float mag, lat, lon, depth;
    unsigned long qts;

    if (usingNZ) {
      lat = quake["geometry"]["coordinates"][1];
      lon = quake["geometry"]["coordinates"][0];
      mag = quake["properties"]["magnitude"];
      depth = quake["properties"]["depth"];
      qts = parseISOToEpoch(quake["properties"]["time"].as<const char*>());
    } else {
      lon = quake["geometry"]["coordinates"][0];
      lat = quake["geometry"]["coordinates"][1];
      mag = quake["properties"]["mag"];
      depth = quake["geometry"]["coordinates"][2];
      qts = (unsigned long)(quake["properties"]["time"].as<double>() / 1000.0);
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

    if (mag > highestMag) { highestMag = mag; highestIdx = i; }
    bool within24 = (nowEpoch > 1600000000) && (qts > 0) &&
                    ((unsigned long)nowEpoch - qts < 86400UL);
    if (within24 && mag > highestMag24) { highestMag24 = mag; highestIdx24 = i; }
  }
  // Prefer the TRUE 24-hour max so the "24H" label is honest; fall back to the
  // feed max only when nothing landed in the last 24 h.
  if (highestIdx24 >= 0) { highestIdx = highestIdx24; highestMag = highestMag24; }
  
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
      abbreviatePlace(h["properties"]["locality"] | "", highestRegionalQuake.location, sizeof(highestRegionalQuake.location));
      highestRegionalQuake.timestamp = parseISOToEpoch(h["properties"]["time"].as<const char*>());
    } else {
      highestRegionalQuake.latitude = h["geometry"]["coordinates"][1];
      highestRegionalQuake.longitude = h["geometry"]["coordinates"][0];
      highestRegionalQuake.depth = h["geometry"]["coordinates"][2];
      abbreviatePlace(h["properties"]["place"] | "", highestRegionalQuake.location, sizeof(highestRegionalQuake.location));
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
      abbreviatePlace(loc, latestQuake.location, sizeof(latestQuake.location));
      
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
  int htx = 160 - htw / 2;
  tft.setCursor(htx, 17);
  tft.print("EQ DETECTED");
  tft.fillTriangle(htx - 16, 20, htx - 10, 6, htx - 4, 20, magColor);
  tft.fillTriangle(160 + htw / 2 + 4, 20, 160 + htw / 2 + 10, 6, 160 + htw / 2 + 16, 20, magColor);

  // Thin divider
  tft.drawFastHLine(5, 28, 310, currentTheme.divider);

  // ── Radar / Target Reticle ──
  int cx = 160;
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

  // Location (demacron, truncate if too wide, centred)
  tft.setFreeFont(FONT_LABEL);
  tft.setTextColor(currentTheme.textPrimary);
  char locBuf[80]; bool locMac[80];
  int locLen = demacron(quake->location, locBuf, locMac, sizeof(locBuf));
  while (locLen > 3 && tft.textWidth(locBuf) > 300) locBuf[--locLen] = '\0';
  int locX = 160 - tft.textWidth(locBuf) / 2, locBaseY = dataY + 11;
  tft.setCursor(locX, locBaseY);
  tft.print(locBuf);
  for (int k = 0; k < locLen; k++) {
    if (!locMac[k]) continue;
    char pre[80]; strncpy(pre, locBuf, k); pre[k] = '\0';
    int vx = locX + tft.textWidth(pre);
    char ch[2] = { locBuf[k], '\0' };
    int vw = tft.textWidth(ch);
    bool upper = (locBuf[k] >= 'A' && locBuf[k] <= 'Z');
    tft.drawFastHLine(vx + 1, locBaseY - (upper ? 15 : 12), (vw > 3) ? vw - 2 : vw, currentTheme.textPrimary);
  }

  // Depth
  char depthBuf[32];
  snprintf(depthBuf, sizeof(depthBuf), "Depth: %.0fkm", quake->depth);
  tft.setTextColor(currentTheme.textSecondary);
  tft.setCursor(160 - tft.textWidth(depthBuf) / 2, dataY + 26);
  tft.print(depthBuf);
}

// ═══════════════════════════════════════════════════════════════════════════
// LOCATION PICKER  (gear → choose region)
// ═══════════════════════════════════════════════════════════════════════════

const char* REGION_VALUES[] = {"NZ", "Japan", "California", "China", "Global"};
const char* REGION_LABELS[] = {"New Zealand", "Japan", "California", "China", "Global"};
const int   REGION_COUNT    = 5;

// Region row geometry — left column of the settings screen
const int PICK_X     = 10;
const int PICK_W     = 140;
const int PICK_Y0    = 48;
const int PICK_H     = 24;
const int PICK_PITCH = 28;

// Settings screen — REGION rows (left) + WIFI connection details (right).
// Tap a region to switch; tap < (top-left) / BOOT / 30s to close. The top-right gear
// does NOT close (that corner is where the recurring phantom second-tap landed).
void drawRegionPicker() {
  showingRegionPicker = true;
  pickerStartTime = millis();
  tft.fillScreen(currentTheme.background);

  // ── Header: ‹ SETTINGS + active cog ──
  tft.drawFastHLine(0, HEADER_H - 1, SCREEN_WIDTH, currentTheme.border);
  tft.setTextFont(1);
  tft.setTextColor(currentTheme.textAccent);
  tft.drawRoundRect(3, 2, 72, 17, 2, currentTheme.textAccent);   // tappable BACK / close affordance
  tft.setCursor(9, 7);
  tft.print("< SETTINGS");
  drawGearIcon(GEAR_CX, GEAR_CY, currentTheme.dataLatest);   // accent = active (does not close)

  tft.drawFastVLine(160, HEADER_H, SCREEN_HEIGHT - HEADER_H, currentTheme.divider);

  // ── LEFT: REGION ──
  tft.setTextFont(1);
  tft.setTextColor(currentTheme.textSecondary);
  tft.setCursor(12, 32);
  tft.print("REGION");
  for (int i = 0; i < REGION_COUNT; i++) {
    int y = PICK_Y0 + i * PICK_PITCH, cyc = y + PICK_H / 2;
    bool on = (strcmp(config.region, REGION_VALUES[i]) == 0);
    if (on) {
      tft.fillRoundRect(PICK_X, y, PICK_W, PICK_H, 4, 0x0903);          // selected fill #0f2118
      tft.fillRect(PICK_X, y, 2, PICK_H, currentTheme.dataLatest);      // inset accent bar
      tft.fillCircle(PICK_X + 12, cyc, 3, currentTheme.dataLatest);     // ◉
      tft.drawCircle(PICK_X + 12, cyc, 4, currentTheme.dataLatest);
    } else {
      tft.drawCircle(PICK_X + 12, cyc, 3, currentTheme.sub);            // ○
    }
    tft.setFreeFont(on ? FONT_DATA : FONT_LABEL);
    tft.setTextColor(on ? currentTheme.textPrimary : currentTheme.textSecondary);
    tft.setCursor(PICK_X + 22, cyc + 5);
    tft.print(REGION_LABELS[i]);
  }

  // ── RIGHT: WIFI CONNECTION ──
  const int rx = 174;
  bool conn = (WiFi.status() == WL_CONNECTED);
  int rssi = conn ? WiFi.RSSI() : -100;
  tft.setTextFont(1);
  tft.setTextColor(currentTheme.textSecondary);
  tft.setCursor(rx, 32);
  tft.print("WIFI CONNECTION");

  tft.setTextColor(currentTheme.sub);
  tft.setCursor(rx, 50);
  tft.print("NETWORK");
  tft.setFreeFont(FONT_DATA);
  tft.setTextColor(currentTheme.textPrimary);
  tft.setCursor(rx, 74);
  char ssid[18]; strncpy(ssid, config.wifiSSID, sizeof(ssid) - 1); ssid[sizeof(ssid) - 1] = '\0';
  tft.print(strlen(ssid) ? ssid : "--");

  int bars = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : rssi > -85 ? 1 : 0;
  for (int i = 0; i < 4; i++) {                                         // 4 signal bars (right)
    int bh = 3 + i * 3;
    tft.fillRect(298 + i * 5, 62 - bh, 3, bh, (i < bars) ? currentTheme.dataLatest : currentTheme.border);
  }

  tft.drawFastHLine(rx, 82, SCREEN_WIDTH - rx - 8, currentTheme.divider);

  tft.setTextFont(1);
  int ry = 92;
  const char* keys[3] = {"SIGNAL", "IP ADDR", "STATUS"};
  for (int i = 0; i < 3; i++, ry += 16) {
    tft.setTextColor(currentTheme.sub);
    tft.setCursor(rx, ry);
    tft.print(keys[i]);
    String val; uint16_t vc = currentTheme.textAccent;
    if (i == 0)      val = String(rssi) + " dBm";
    else if (i == 1) val = conn ? WiFi.localIP().toString() : String("--");
    else { val = conn ? "CONNECTED" : "OFFLINE"; vc = conn ? currentTheme.dataLatest : currentTheme.dataHighest; }
    int vw = tft.textWidth(val.c_str());
    if (i == 2) tft.fillCircle(SCREEN_WIDTH - 8 - vw - 6, ry + 3, 2, vc);   // status dot
    tft.setTextColor(vc);
    tft.setCursor(SCREEN_WIDTH - 8 - vw, ry);
    tft.print(val);
  }

  // ── Footer hint ──
  tft.drawFastHLine(0, SCREEN_HEIGHT - 16, SCREEN_WIDTH, currentTheme.border);
  tft.setTextFont(1);
  tft.setTextColor(currentTheme.sub);
  tft.drawCentreString("TAP REGION TO SWITCH    TAP < TO CLOSE", 160, SCREEN_HEIGHT - 11, 1);
}

// Returns picker row 0..4 under a screen point, or -1 if none.
int regionAtPoint(int16_t sx, int16_t sy) {
  // Forgiving hit zones: the whole left column (up to the divider), and each row claims
  // its full pitch so taps in the gaps still land on the nearest region.
  if (sx < 2 || sx > 158) return -1;
  if (sy < PICK_Y0 - 2) return -1;
  int i = (sy - (PICK_Y0 - 2)) / PICK_PITCH;
  return (i >= 0 && i < REGION_COUNT) ? i : -1;
}

// Apply a chosen region: persist it, clear stale data, reload, return to main UI.
void selectRegion(int idx) {
  if (idx < 0 || idx >= REGION_COUNT) return;
  strncpy(config.region, REGION_VALUES[idx], sizeof(config.region) - 1);
  config.region[sizeof(config.region) - 1] = '\0';
  saveConfig();

  showingRegionPicker = false;
  lastQuakeID = "";                 // force a fresh "latest" detection for the new region
  lastTriggeredQuakeID = "";
  latestQuake.clear();
  highestRegionalQuake.clear();
  recentQuakeCount = 0;

  drawUI();                         // immediate redraw (new map) confirms the choice
  checkForEarthquakes();            // then pull the new region's quakes
  updateDataRegion();
  updateMapEarthquakeMarkers();
  lastAPICheck = millis();
  lastActivity = millis();
}

// ═══════════════════════════════════════════════════════════════════════════
// CAPACITIVE TOUCH  (FT6336G, I2C 0x38) + BOOT BUTTON FALLBACK
// ═══════════════════════════════════════════════════════════════════════════

// Returns true if at least one finger is currently down.
// x/y are raw FT6336G panel coordinates (native portrait frame, X:0-239 Y:0-319).
// mapTouch() converts these to landscape screen coords for hit-testing the gear.
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

// Map raw FT6336G portrait coords to landscape screen coords (setRotation 1).
// If the gear hit-zone lands in the wrong corner on hardware, flip these 2 lines.
void mapTouch(int16_t tx, int16_t ty, int16_t &sx, int16_t &sy) {
  sx = ty;                                      // native Y (0-319) -> screen X
  sy = (int16_t)(SCREEN_HEIGHT - 1 - tx);       // native X (0-239) -> screen Y (flipped)
  sx = (int16_t)constrain((int)sx, 0, SCREEN_WIDTH - 1);
  sy = (int16_t)constrain((int)sy, 0, SCREEN_HEIGHT - 1);
}

void handleButton() {
  if (showingAlert) return;

  unsigned long now = millis();

  // ── Capacitive touch tap ─────────────────────────────────────────────────
  // Fire ONE action per press, ~45ms after first contact so the FT6336G's
  // (often noisy) first coordinate has settled — then don't re-arm until the
  // finger has been confirmed up for 80ms. This kills both the phantom
  // double-tap (flash-open-then-close) and the missed/misplaced first read.
  int16_t tx, ty;
  bool touching = readTouch(tx, ty);

  static unsigned long pressStart = 0, lastSeen = 0;
  static bool pressActed = false, releasedSinceOpen = true;

  if (touching) {
    if (pressStart == 0) pressStart = now;
    lastSeen = now;
    if (!pressActed && (now - pressStart >= 45)) {
      pressActed   = true;
      lastActivity = now;
      isRestMode   = false;

      int16_t sx, sy;
      mapTouch(tx, ty, sx, sy);
      Serial.printf("TOUCH (%d,%d) picker=%d released=%d dt=%ld\n",
                    sx, sy, (int)showingRegionPicker, (int)releasedSinceOpen,
                    (long)(now - pickerStartTime));

      if (showingRegionPicker) {
        // Act ONLY after a real finger-lift since opening (+ a short lock). The
        // opening press — even with a sensor drop-out under 300ms — keeps
        // releasedSinceOpen false, so it can never select a region or close.
        if (releasedSinceOpen && (now - pickerStartTime > 400)) {
          int picked = regionAtPoint(sx, sy);
          if (picked >= 0)             selectRegion(picked);                       // chose a region
          else if (sx < 90 && sy < 30) { showingRegionPicker = false; drawUI(); }  // < BACK (top-left) closes
          // Gear corner (top-right) deliberately does NOT close.
        }
      } else if (sx > 280 && sy < 30) {
        Serial.println("  -> OPEN picker");
        drawRegionPicker();                                           // gear (top-right) → settings
        releasedSinceOpen = false;                                    // require a lift before any action
      } else {
        checkForEarthquakes();                                        // elsewhere → refresh data
        updateDataRegion();
        updateMapEarthquakeMarkers();
        lastAPICheck = now;
      }
    }
  } else if (now - lastSeen > 300) {                                  // finger confirmed UP for 300ms
    pressStart = 0; pressActed = false; releasedSinceOpen = true;     // re-arm + allow settings taps
  }

  // ── Physical BOOT button (GPIO0) — debounced so electrical noise can't fire it,
  //    and it NEVER closes the picker (the suspected by-itself flash-close). It only
  //    refreshes the main screen, and only after a sustained (>80ms) press. ───────
  static unsigned long bootLowSince = 0;
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (bootLowSince == 0) bootLowSince = now;
    if (now - bootLowSince > 80 && now - lastButtonPress > DEBOUNCE_DELAY && !showingRegionPicker) {
      lastButtonPress = now; lastActivity = now; isRestMode = false;
      checkForEarthquakes(); updateDataRegion(); updateMapEarthquakeMarkers(); lastAPICheck = now;
    }
  } else {
    bootLowSince = 0;
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
  tft.drawCentreString("SETUP MODE", 160, 75, 4);

  tft.setTextColor(currentTheme.textSecondary);
  tft.drawCentreString("WiFi: SeisMonitor-Setup", 160, 115, 2);
  tft.drawCentreString("URL: 192.168.4.1", 160, 138, 2);
  
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
  html += R"(>Large</option></select><label>Visual Style</label><select name="aesthetic"><option value="elegant")";
  if (strcmp(config.aesthetic, "elegant") == 0) html += " selected";
  html += R"(>Elegant</option><option value="contrast")";
  if (strcmp(config.aesthetic, "contrast") == 0) html += " selected";
  html += R"(>High Contrast</option><option value="mono")";
  if (strcmp(config.aesthetic, "mono") == 0) html += " selected";
  html += R"(>Monochrome</option></select>)";

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
  if (server.hasArg("aesthetic")) server.arg("aesthetic").toCharArray(config.aesthetic, sizeof(config.aesthetic));
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
