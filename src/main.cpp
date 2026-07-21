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
 * Orientation: Landscape 320×240 (setRotation 3 — 180°-flipped for the enclosure, inverted)
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
// Custom intermediate sizes (generated: Arial rasterised into the GFX font format) so place names
// step down GENTLY instead of falling off a cliff from 9pt straight to the tiny built-in font.
#include "SeisSans10.h"   // cap 10 (vs 9pt's 12)
#include "SeisSans8.h"    // cap ~8
#include "SeisMag.h"      // Arial Bold ~58px — the alert-screen magnitude hero (only '.' 0-9 'M')
#include "SeisPlace22.h"  // Arial Bold 22px — the alert-screen place name
#include "SeisCoast.h"    // Natural Earth 1:110m coastlines for the globe (see tools/gencoast.py)
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

const int SCREEN_WIDTH  = 320;   // Landscape — full 320×240 canvas (setRotation 3, 180°-flipped)
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
const int HTTP_TIMEOUT = 15000;                     // 15s — the California M1.0+ feed is ~112KB; small feeds still return fast (this is only a ceiling)
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
// M1.0+ feed: California is quiet at M2.5+ (the shared 2.5_day feed had 0 CA events), so use the
// lower-threshold feed. The FDSN /fdsnws/ server-side query 404s (USGS/CDN-side, confirmed on the
// device), so we can't server-filter. This feed is ~112KB, which needs the longer HTTP_TIMEOUT.
const char* API_CALIFORNIA = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/1.0_day.geojson";
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

// ── Dual-core fetch handoff: the background task (core 0) drops the raw feed text here and raises
//    g_payloadReady; the UI loop (core 1) parses/reacts and clears it. So the blocking network fetch
//    never freezes the seismograph / clock / touch. ──
volatile bool g_payloadReady = false;   // task -> loop: a fresh payload is waiting
volatile bool g_fetchNow     = false;   // loop -> task: fetch immediately (region change / manual refresh)
String        g_payload;                // raw feed text (written by task, read by loop when ready)
char          g_payloadRegion[16] = ""; // which region g_payload was fetched for

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
  int year, month, day, hour, minute, second;
  if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) >= 6) {
    // GeoNet timestamps are UTC ("...Z"). mktime() would interpret these fields as LOCAL time and
    // apply the NZ offset, shifting the result 12h (the cause of every NZ quake reading ~12h too
    // old). Convert UTC fields -> epoch directly, timezone-independent (days-from-civil, Hinnant).
    int y = year - (month <= 2);
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = era * 146097L + (long)doe - 719468L;             // days since 1970-01-01 (UTC)
    return (unsigned long)(days * 86400L + hour * 3600L + minute * 60L + second);
  }
  return 0;
}

// ── Felt-severity: magnitude AND depth ──────────────────────────────────────────────────────────
// Magnitude is the ENERGY released at the source; what actually reaches the surface — the shaking a
// person feels, which is what an "activity detected" alert is really reporting — attenuates with the
// distance the waves travel up, i.e. with focal depth. A shallow M4 rattles windows; a deep M6 three
// hundred km down is often not felt at all. So severity must fold in depth, not magnitude alone.
//
// This is NOT a published ground-motion equation (those are regional and full of scatter) — it's an
// honest monotonic heuristic that captures the real direction of the physics: felt intensity rises
// with magnitude and falls with the LOG of depth (each ~10× of depth costs ~one step). Neutral at
// 10 km (typical shallow-crustal depth), so a 10 km quake scores its own magnitude; shallower bumps
// up, deeper pulls down. Returns an "effective magnitude" on the same familiar M-scale.
float feltSeverity(float magnitude, float depthKm) {
  float d = depthKm < 1.0f ? 1.0f : depthKm;          // floor: <1 km ≈ at the surface, and avoids log(0)
  float eff = magnitude - 0.7f * (log10f(d) - 1.0f);  // 10 km is neutral; 100 km ≈ −0.7; 1 km ≈ +0.7
  return eff;
}

// Severity colour ramp, keyed on the felt (depth-adjusted) magnitude. Climbs in BOTH luminance and
// saturation — deliberately, because on a black additive display brightness reads as urgency, so the
// worst event must be the brightest/hottest pixel. (The old ramp ran the other way: its M7 sienna was
// the DIMMEST colour on the screen, so the biggest quake looked the calmest.)
uint16_t severityColor(float magnitude, float depthKm) {
  float eff = feltSeverity(magnitude, depthKm);
  if (eff >= 7.0f) return 0xFAE9;   // #ff5d4d hot red     — brightest & most saturated
  if (eff >= 6.0f) return 0xFCA7;   // #ff9538 orange
  if (eff >= 5.0f) return 0xFE68;   // #ffcf47 amber
  if (eff >= 4.0f) return 0xCF0B;   // #cfe25f chartreuse
  return currentTheme.textAccent;   // #7fd69a calm phosphor green (routine — ~90% of alerts)
}

// Back-compat: magnitude-only callers assume a neutral (10 km) depth.
uint16_t getMagnitudeColor(float magnitude) { return severityColor(magnitude, 10.0f); }

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
void processQuakes(String& payload, bool usingNZ);
void requestFetch();
void fetchTask(void* param);
void displayEarthquakeAlert(EarthquakeData* quake);
void animateAlertRings();
void previewAlertCycle();
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
    tft.drawString("v7.8-type", 8, 228, 1);
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
  tft.setRotation(3);      // Landscape — 320×240, rotated 180° (device mounted upside-down in the enclosure)
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

  drawLoadingScreen("FETCHING SEISMIC DATA", f2);   // hold the loader through the first (synchronous) fetch
  checkForEarthquakes();
  drawUI();
  playStartupRumble();

  lastActivity = millis();
  isRestMode = true;

  // Hand ongoing fetches to a background task on core 0 so the blocking network call can never freeze
  // the UI loop (core 1). It self-polls every API_POLL_INTERVAL and reacts to requestFetch().
  xTaskCreatePinnedToCore(fetchTask, "fetch", 12288, NULL, 1, NULL, 0);

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
    } else {
      static unsigned long lastRing = 0;                 // keep the activity rings radiating
      if (now - lastRing > 55) { animateAlertRings(); lastRing = now; }
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

  // Keep the header clock live: it otherwise only refreshes on a full UI redraw, so the HH:MM
  // sat frozen at whatever it last showed. Redraw the header strip when the minute changes
  // (cheap — header-only fillRect, doesn't touch the map/seismo).
  {
    static int lastClockMin = -1;
    time_t tnow = time(nullptr);
    if (tnow > 1600000000) {
      struct tm lt; localtime_r(&tnow, &lt);
      if (lt.tm_min != lastClockMin) { lastClockMin = lt.tm_min; drawHeader(); }
    }
  }

  if (now - lastSeismoUpdate > SEISMO_UPDATE_INTERVAL) {
    animateSeismograph();
    lastSeismoUpdate = now;
  }

  // Live epicenter pulse — radiating rings on the LATEST quake, but ONLY while it's fresh: every
  // 10 minutes during its first hour, then nothing (no animation for older quakes). A newly-arrived
  // quake pulses ~8s after it appears.
  {
    static unsigned long nextPulse = 0, pulseStart = 0, pulsedTs = 0;
    static bool pulseActive = false;
    time_t te = time(nullptr);
    unsigned long age = (te > 1600000000 && latestQuake.timestamp > 0 && (unsigned long)te > latestQuake.timestamp)
                        ? ((unsigned long)te - latestQuake.timestamp) : 999999UL;
    if (latestQuake.isValid && latestQuake.timestamp != pulsedTs) {   // a new quake just arrived
      pulsedTs = latestQuake.timestamp;
      nextPulse = now + 8000UL;                                       // first burst shortly after it appears
    }
    bool canPulse = latestQuake.isValid && strcmp(config.region, "Global") != 0 && age < 3600UL;  // first hour only
    if (canPulse && !pulseActive && now >= nextPulse) {
      pulseActive = true; pulseStart = now;
      nextPulse = now + 600000UL;                                     // then every 10 minutes
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

  // Apply a payload the background task fetched (parse + react happen HERE, on the UI core). The task
  // owns the 90s polling; the loop just consumes results, so the network never blocks the animation.
  if (g_payloadReady) {
    unsigned long lT = latestQuake.timestamp, hT = highestRegionalQuake.timestamp;
    float         lM = latestQuake.magnitude, hM = highestRegionalQuake.magnitude;
    if (strcmp(g_payloadRegion, config.region) == 0)      // ignore a payload for a region we've since left
      processQuakes(g_payload, isUsingNZAPI(config.region));
    g_payload = String();                                 // free the buffer
    g_payloadReady = false;                               // release the task to fetch again
    Serial.printf("[data] region=%s valid=%d mag=%.1f latest_ts=%lu age=%lds loc=[%s]\n",
                  config.region, (int)latestQuake.isValid, latestQuake.magnitude, latestQuake.timestamp,
                  (long)time(nullptr) - (long)latestQuake.timestamp, latestQuake.location);
    // NOT while an alert is up: processQuakes() may have just raised one (same iteration — the
    // showingAlert check at the top of loop() already ran), and these two repaint the DATA + MAP
    // panels straight over it, leaving the alert showing only in the gaps with its rings still
    // radiating across the main screen. The alert's own expiry calls drawUI(), which redraws
    // everything with this fresh data anyway.
    if (!showingAlert &&
        (latestQuake.timestamp != lT || latestQuake.magnitude != lM ||
         highestRegionalQuake.timestamp != hT || highestRegionalQuake.magnitude != hM)) {
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

// Gear/cog icon centred at (cx,cy): body disc with 8 RECTANGULAR radial teeth + hub
// hole. Rectangular teeth (not round bumps) read as a cog, not a flower (~20px).
void drawGearIcon(int cx, int cy, uint16_t color) {
  const float ri = 5.0f, ro = 9.0f, hw = 1.6f;  // tooth inner/outer radius + half-width
  for (int a = 0; a < 360; a += 45) {            // 8 rectangular teeth as radial spokes
    float r = a * 0.01745329f;
    float cr = cosf(r), sr = sinf(r);            // radial unit vector
    float px = -sr, py = cr;                     // perpendicular unit vector (tooth width axis)
    int ilx = cx + (int)(cr * ri + px * hw), ily = cy + (int)(sr * ri + py * hw);
    int irx = cx + (int)(cr * ri - px * hw), iry = cy + (int)(sr * ri - py * hw);
    int olx = cx + (int)(cr * ro + px * hw), oly = cy + (int)(sr * ro + py * hw);
    int orx = cx + (int)(cr * ro - px * hw), ory = cy + (int)(sr * ro - py * hw);
    tft.fillTriangle(ilx, ily, irx, iry, olx, oly, color);   // tooth = two triangles (a quad)
    tft.fillTriangle(irx, iry, olx, oly, orx, ory, color);
  }
  tft.fillCircle(cx, cy, 6, color);              // body disc (covers the teeth roots)
  tft.fillCircle(cx, cy, 2, currentTheme.background);   // hub hole — the give-away it's a cog
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
  } else if (strcmp(config.region, "California") == 0) {
    tft.setCursor(rx, 7); tft.print("CALIFORNIA, USA");        // no second script — state + country reads nicely
  } else {
    tft.setCursor(rx, 7); tft.print(config.region);            // Global
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
  // USGS uses compass codes ("12km SSW of ..."). Collapse the wordy 3-letter intercardinals to the
  // nearest diagonal (SSW->SW). Lone cardinals are ABBREVIATED (north->N) for room + consistency —
  // GeoNet spells them out, USGS already sends "N". Order: 3-letter first, then the words.
  s.replace(" NNE of ", " NE of "); s.replace(" ENE of ", " NE of ");
  s.replace(" ESE of ", " SE of "); s.replace(" SSE of ", " SE of ");
  s.replace(" SSW of ", " SW of "); s.replace(" WSW of ", " SW of ");
  s.replace(" WNW of ", " NW of "); s.replace(" NNW of ", " NW of ");
  s.replace(" north of ", " N of "); s.replace(" south of ", " S of ");   // GeoNet words -> letters
  s.replace(" east of ",  " E of "); s.replace(" west of ",  " W of ");
  // Global view: the country matters more than the distance from a small town, and
  // long "City, Country" names truncate — so drop the leading "<dist>km <dir> of ".
  if (strcmp(config.region, "Global") == 0) {
    int ofIdx = s.indexOf(" of ");
    if (ofIdx >= 0 && ofIdx < 15) s = s.substring(ofIdx + 4);   // keep "Place, Country"
  } else if (strcmp(config.region, "NZ") == 0) {
    // GeoNet's "Within N km of <place>" (very-close quakes) is verbose and overflows the data cell;
    // the map marker already shows the exact spot, so collapse it to just "<place>".
    if (s.startsWith("Within ")) { int o = s.indexOf(" of "); if (o >= 0) s = s.substring(o + 4); }
    restoreMacrons(s);                                          // GeoNet drops macrons — restore them
  } else {
    int ci = s.lastIndexOf(',');                               // Japan/California/China: the trailing
    if (ci > 0) s = s.substring(0, ci);                        // ", Region" is redundant + truncates — drop it
  }
  strncpy(out, s.c_str(), maxOut - 1); out[maxOut - 1] = '\0';
}

// Wrap demacron'd ASCII `a` at the CURRENT font into <= maxLines; returns line
// count and sets `overflow` true if text remained past the cap.
static bool connectorAfter(const char* a, int sp, int len);   // forward decl (defined below)
static int wrapMeasure(const char* a, int len, int maxW, int maxLines, bool& overflow) {
  int pos = 0, lines = 0; char ln[80];
  while (pos < len && lines < maxLines) {
    int rem = min(len - pos, 79); strncpy(ln, a + pos, rem); ln[rem] = '\0';
    int end;
    if (tft.textWidth(ln) <= maxW) end = pos + rem;
    else {
      end = pos; int ls = -1;
      for (int i = pos; i < len && i - pos < 79; i++) {
        if (a[i] == ' ' && !connectorAfter(a, i, len)) ls = i;
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

// Left side bearing: the blank columns the GFX format bakes into a glyph BEFORE its ink. It differs
// per character, so setting the cursor to x does NOT put every line's ink at x — it indents some and
// not others. Worst offender is '1': digits are tabular (every digit advances 10px in FreeSans9pt) so
// the narrow 1 is centred inside that slot and carries a 3px bearing, against 1px for '3' and 0 for
// 'W'. A line reading "10km E of ..." therefore looks like it starts with a space, which is exactly
// what it looks like. Subtracting the bearing lands every line's ink flush on the margin.
//
// This replaced a hand-guessed nudge table (T/V/W/Y -> 2px, A/J -> 1px) that knew nothing about
// digits and, stacked on top of the real bearings, spread line starts across x-2 .. x+3. Read the
// metric out of the font instead of guessing at it.
static int glyphLeftBearing(const GFXfont* f, char c) {
  if (!f || c == '\0') return 0;
  uint8_t u = (uint8_t)c;
  uint8_t first = pgm_read_byte(&f->first), last = pgm_read_byte(&f->last);
  if (u < first || u > last) return 0;
  GFXglyph* g = &(((GFXglyph*)pgm_read_dword(&f->glyph))[u - first]);
  if (pgm_read_byte(&g->width) == 0) return 0;          // blank glyph (space) — no ink to align
  return (int8_t)pgm_read_byte(&g->xOffset);
}

// Width of the widest single space-delimited word, measured in the CURRENT font. A word wider than the
// box can only be rendered by chopping it mid-letter, so the size ladder uses this to step down instead.
static int widestWordWidth(const char* a, int len) {
  int widest = 0, start = 0;
  for (int i = 0; i <= len; i++) {
    if (i < len && a[i] != ' ') continue;
    if (i > start) {
      char w[80]; int n = i - start; if (n > 79) n = 79;
      strncpy(w, a + start, n); w[n] = '\0';
      int ww = tft.textWidth(w);
      if (ww > widest) widest = ww;
    }
    start = i + 1;
  }
  return widest;
}

// Place name fitted into a box (x,y; width maxW, height maxH). Picks the largest font whose FULL wrap
// fits — FreeSans9pt (lineH 14), else the compact built-in Font 1 (lineH 10) — so long "City, Country"
// names fit instead of truncating. Centres the wrapped block vertically (a 2-line name sits mid-cell,
// not jammed to the top). Māori macron bars + optical left-alignment applied.
void drawPlaceNameFit(const char* text, int x, int y, int maxW, int maxH) {
  char a[80]; bool mc[80];
  int len = demacron(text, a, mc, sizeof(a));
  tft.setTextColor(currentTheme.textPrimary);

  // FONT LADDER — step down gently, never off a cliff. Walk from the biggest size to the smallest and
  // take the FIRST one whose whole name fits the box; only a monster name reaches the last rung. The
  // custom sizes are proportional + smooth (same rasterised style as 9pt), not the blocky built-in.
  struct FontStep { const GFXfont* f; int lineH; int cursOff; int macUp; int macLo; };
  static const FontStep LADDER[] = {
    { FONT_LABEL,   14, 11, 15, 12 },   // FreeSans9pt — cap 12 (preferred)
    { &SeisSans10,  12, 10, 13, 10 },   // custom      — cap 10
    { &SeisSans8,   10,  8, 10,  8 },   // custom      — cap ~8 (last resort)
  };
  const int LADDER_N = 3;
  int step = LADDER_N - 1;                                     // default: smallest
  for (int i = 0; i < LADDER_N; i++) {
    tft.setFreeFont(LADDER[i].f);
    bool ovi; wrapMeasure(a, len, maxW, maxH / LADDER[i].lineH, ovi);
    // A size only "fits" if the wrap succeeds AND every individual WORD fits on a line. Without the
    // second test the wrapper reports success while chopping a word mid-letter — "Martinborough" is
    // 116px against a 97px cell at 9pt, so it rendered as "Martinborou" / "gh". It's 93px at
    // SeisSans10, so requiring the widest word to fit steps down to a size that keeps words whole.
    if (!ovi && widestWordWidth(a, len) <= maxW) { step = i; break; }
  }
  tft.setFreeFont(LADDER[step].f);
  int lineH = LADDER[step].lineH, cursOff = LADDER[step].cursOff;
  bool small = (step > 0);
  int maxLines = maxH / lineH; if (maxLines < 1) maxLines = 1;

  // Count lines, then centre the block AND (for short names) spread the lines to use more of the
  // cell — a 2-line name breathes instead of sitting as a tight little block jammed together.
  bool ov2; int nLines = wrapMeasure(a, len, maxW, maxLines, ov2);
  Serial.printf("[place] [%s] maxW=%d maxH=%d step=%d lineH=%d nLines=%d\n",
                a, maxW, maxH, step, lineH, nLines);
  int stepH = lineH, fill = maxH / nLines;
  if (fill > lineH + 3) stepH = min(lineH + 4, fill); // spread only short names (real spare height); a full
                                                      // 3-line name stays tight so it fits with a gap, not filling
  int y0 = y + (maxH - nLines * stepH) / 2; if (y0 < y) y0 = y;

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
    int lineY = y0 + lines * stepH + cursOff;

    if (lines == maxLines - 1 && end < len) {         // last line + more text → ellipsis
      while (pl > 0) {
        char t2[92]; snprintf(t2, sizeof(t2), "%.*s...", pl, a + pos);
        if (tft.textWidth(t2) <= maxW) break;
        pl--;
      }
      char outl[92]; snprintf(outl, sizeof(outl), "%.*s...", pl, a + pos);
      tft.setCursor(x - glyphLeftBearing(LADDER[step].f, outl[0]), lineY); tft.print(outl);
      break;
    }

    strncpy(line, a + pos, pl); line[pl] = '\0';
    while (pl > 0 && line[pl - 1] == ' ') line[--pl] = '\0';
    int lx = x - glyphLeftBearing(LADDER[step].f, line[0]);   // land the INK on the margin, not the cursor
    tft.setCursor(lx, lineY);
    tft.print(line);

    for (int k = 0; k < pl; k++) {                    // Māori macron bars
      if (!mc[pos + k]) continue;
      char pre[80]; strncpy(pre, a + pos, k); pre[k] = '\0';
      int vx = lx + tft.textWidth(pre);
      char ch[2] = { a[pos + k], '\0' };
      int vw = tft.textWidth(ch);
      bool upper = (a[pos + k] >= 'A' && a[pos + k] <= 'Z');
      int barY = lineY - (upper ? LADDER[step].macUp : LADDER[step].macLo);   // macron bar scales with the size
      tft.drawFastHLine(vx + 1, barY, (vw > 3) ? vw - 2 : vw, currentTheme.textPrimary);
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
  tft.setTextFont(1);
  tft.setTextColor(accent);
  tft.setCursor(xL, topY + 7);                        // baseline-aligned with the magnitude below (clean row)
  tft.print(label);

  if (!q.isValid) {
    tft.setTextColor(currentTheme.textSecondary);          // a calm "nothing happening", not an error
    tft.setCursor(xL, topY + 20); tft.print("NO RECENT");
    tft.setCursor(xL, topY + 32); tft.print("QUAKES");
    return;
  }

  char m[8]; snprintf(m, sizeof(m), "M%.1f", q.magnitude);
  tft.setFreeFont(FONT_LABEL);                       // regular weight — the accent colour carries it, so it reads lighter
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

  // ── Place name fills the gap between row 1 and the meta (auto-fit + centred + spread) ──
  int placeTop = topY + 18;                          // clear gap below the LATEST/M row so the place never rams it
  // Give the place name the FULL cell width (wider than the mag's column) so borderline names like
  // "10km east of Waipukurau" wrap to a tidy 2 lines at normal size instead of spilling to 3 + shrinking.
  drawPlaceNameFit(q.location, xL, placeTop, DATA_WIDTH - 9, metaY - 3 - placeTop);
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
// The M-tag always sits to the SIDE of the ring (beside the dot). side: 0 = auto (right, flip left
// near the edge); -1 = force LEFT; +1 = force RIGHT. When two markers are close the caller puts each
// on its OUTER side so the tags separate horizontally instead of colliding.
void drawMarker(float lat, float lon, uint16_t col, float mag, int side = 0) {
  int x = mapLonToScreen(lon), y = mapLatToScreen(lat);
  if (x < MAP_X || x > MAP_X + MAP_WIDTH || y < MAP_Y || y > MAP_Y + MAP_HEIGHT) return;
  int r = constrain(4 + (int)mag, 4, 12);    // ring radius reflects magnitude
  tft.drawCircle(x, y, r, col);
  tft.drawCircle(x, y, r - 1, col);          // 2px ring
  tft.fillCircle(x, y, 2, col);              // core dot on the location

  char m[8]; snprintf(m, sizeof(m), "M%.1f", mag);
  tft.setTextFont(1);
  tft.setTextColor(col);
  int lw = tft.textWidth(m);
  int lx;
  if (side < 0)      lx = x - r - 3 - lw;                        // to the LEFT of the ring
  else if (side > 0) lx = x + r + 3;                             // to the RIGHT of the ring
  else {                                                         // auto: right, flip left near the edge
    lx = x + r + 3;
    if (lx + lw > MAP_X + MAP_WIDTH - 2) lx = x - r - 3 - lw;
  }
  if (lx < MAP_X + 2) lx = MAP_X + 2;                            // keep the tag on the panel
  if (lx + lw > MAP_X + MAP_WIDTH - 2) lx = MAP_X + MAP_WIDTH - 2 - lw;
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
    // Latest + 24h-high markers. If they're the SAME quake, draw just one. If they're DIFFERENT
    // but land close together ON THE MAP, put each M-tag on its OUTER side (left marker's tag to the
    // left, right marker's to the right) so the numbers separate instead of smearing — a degree-based
    // test misses this (nearby places can be >0.3° apart yet only a few pixels apart on a small map).
    bool haveL = latestQuake.isValid, haveH = highestRegionalQuake.isValid;
    bool sameQuake = haveL && haveH &&
                     (latestQuake.timestamp == highestRegionalQuake.timestamp ||
                      (fabsf(latestQuake.latitude  - highestRegionalQuake.latitude)  < 0.15f &&
                       fabsf(latestQuake.longitude - highestRegionalQuake.longitude) < 0.15f));
    int latSide = 0, hiSide = 0;
    if (haveL && haveH && !sameQuake) {
      int lsx = mapLonToScreen(latestQuake.longitude),  lsy = mapLatToScreen(latestQuake.latitude);
      int hsx = mapLonToScreen(highestRegionalQuake.longitude), hsy = mapLatToScreen(highestRegionalQuake.latitude);
      if (abs(lsx - hsx) < 52 && abs(lsy - hsy) < 26) {         // close on-screen -> outer sides
        bool latestLeft = (lsx <= hsx);
        latSide = latestLeft ? -1 : 1;
        hiSide  = latestLeft ?  1 : -1;
      }
    }
    if (haveL)
      drawMarker(latestQuake.latitude, latestQuake.longitude, currentTheme.dataLatest, latestQuake.magnitude, latSide);
    if (haveH && !sameQuake)
      drawMarker(highestRegionalQuake.latitude, highestRegionalQuake.longitude, currentTheme.dataHighest, highestRegionalQuake.magnitude, hiSide);

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
  tft.fillCircle(mapLonToScreen(-120.10), mapLatToScreen(33.97), 2, currentTheme.mapOutline); // Santa Rosa
  tft.fillCircle(mapLonToScreen(-118.42), mapLatToScreen(33.39), 2, currentTheme.mapOutline); // Santa Catalina
  tft.fillCircle(mapLonToScreen(-118.55), mapLatToScreen(32.90), 2, currentTheme.mapOutline); // San Clemente

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

  // ── Offshore features — fill the empty Pacific side of the map ──
  // San Gregorio–Hosgri fault system: runs offshore, just west of the central coast
  const float sanGregorio[][2] = {
    {37.50, -123.00}, {37.10, -122.75}, {36.60, -122.35},
    {36.10, -121.95}, {35.60, -121.55}, {35.10, -121.30}, {34.60, -121.10},
  };
  drawGeoDotted(sanGregorio, sizeof(sanGregorio) / sizeof(sanGregorio[0]));

  // San Clemente fault zone: offshore Southern California, west of the Channel Islands
  const float sanClemente[][2] = {
    {33.80, -119.30}, {33.40, -118.95}, {33.00, -118.60}, {32.55, -118.20},
  };
  drawGeoDotted(sanClemente, sizeof(sanClemente) / sizeof(sanClemente[0]));

  // Mendocino Fracture Zone: E-W ridge running out to sea off Cape Mendocino (far NW)
  const float mendocino[][2] = {
    {40.35, -124.55}, {40.30, -125.00}, {40.25, -125.35},
  };
  drawGeoDotted(mendocino, sizeof(mendocino) / sizeof(mendocino[0]));

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

  // Coastlines — Natural Earth 1:110m (SeisCoast.h, from tools/gencoast.py). One flat {lat,lon} array;
  // a {999,999} sentinel lifts the pen between features so separate landmasses don't get joined. Front
  // arcs bright, back (far-side) arcs dim — same depth cue the graticule uses. Replaced the old ~225
  // hand-plotted points (blocky continents, no Antarctica) with ~1225 real ones.
  {
    int px = 0, py = 0; bool pf = false, have = false;
    for (int i = 0; i < GLOBE_COAST_N; i++) {
      if (GLOBE_COAST[i][0] > 900.0f) { have = false; continue; }   // sentinel — pen up
      int sx, sy; bool front;
      globeProject(GLOBE_COAST[i][0], GLOBE_COAST[i][1], cx, cy, sx, sy, front);
      if (have) { if (pf && front)        g->drawLine(px, py, sx, sy, eqF);
                  else if (!pf && !front) g->drawLine(px, py, sx, sy, 0x0140); }
      px = sx; py = sy; pf = front; have = true;
    }
  }

  g->drawCircle(cx, cy, (int)GLOBE_R, limb);
  g->drawCircle(cx, cy, (int)GLOBE_R - 1, limb);

  if (latestQuake.isValid)
    globeMarkerT(g, latestQuake.latitude, latestQuake.longitude, cx, cy, currentTheme.dataLatest, latestQuake.magnitude);
  if (highestRegionalQuake.isValid)
    globeMarkerT(g, highestRegionalQuake.latitude, highestRegionalQuake.longitude, cx, cy, currentTheme.dataHighest, highestRegionalQuake.magnitude);
}

// "POWERED BY USGS" credit drawn bottom-right into the given target (globe sprite or the panel), so
// Global carries the same data-source credit as the flat regions. Baked into the sprite each frame so
// the spin never wipes it. (rightX/bottomY are the target's right/bottom edge in its own coords.)
template <typename T> void drawGlobeCredit(T* g, int rightX, int bottomY) {
  g->setTextFont(1);
  g->setTextColor(currentTheme.textSecondary);
  const char* src = "POWERED BY USGS";
  int sw = g->textWidth(src);
  g->setCursor(rightX - sw - 4, bottomY - 10);
  g->print(src);
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
    drawGlobeCredit(&globeSpr, globeSpr.width(), globeSpr.height());      // baked in — static over the spin
    globeSpr.pushSprite(MAP_X + 1, MAP_Y + 1);                            // inside the border
  } else {
    renderGlobe(&tft, MAP_CX, MAP_CY);           // fallback: straight to the panel (static)
    drawGlobeCredit(&tft, MAP_X + MAP_WIDTH, MAP_Y + MAP_HEIGHT);
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// EARTHQUAKE DATA FETCHING
// ═══════════════════════════════════════════════════════════════════════════

// ── Network-only: fetch the raw feed payload. This is the ONLY blocking part, so it runs on the
//    background core (fetchTask) — a slow/stalled fetch can never freeze the UI loop. ──
String fetchPayload(const char* apiURL) {
  if (WiFi.status() != WL_CONNECTED) return String();
  HTTPClient http;
  http.begin(apiURL);
  http.setTimeout(HTTP_TIMEOUT);
  int httpCode = http.GET();
  Serial.printf("[fetch] HTTP %d  heap=%u  %s\n", httpCode, ESP.getFreeHeap(), apiURL);
  if (httpCode != 200) { http.end(); return String(); }
  String payload = http.getString();
  http.end();
  Serial.printf("[fetch] payload=%u bytes  heap=%u\n", (unsigned)payload.length(), ESP.getFreeHeap());
  return payload;
}

// ── Parse the payload, update the quake data, and react (seismo trigger / alert / redraw). Runs on
//    the UI core, so all the TFT/seismo touches here are safe. ──
void processQuakes(String& payload, bool usingNZ) {
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
  
  // Tolerate IncompleteInput: the device's HTTPS client caps reads at ~64KB, and California's
  // M1.0+ feed is ~112KB. The feed is newest-first, so the 64KB prefix still holds plenty of
  // recent in-region quakes — ArduinoJson keeps every COMPLETE feature it parsed before the cut.
  // Other regions' feeds are <64KB so they parse whole (no IncompleteInput).
  if (error && error != DeserializationError::IncompleteInput) {
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

void checkForEarthquakes() {            // synchronous fetch + process — used once at boot
  String p = fetchPayload(getAPIEndpoint(config.region));
  if (p.length()) processQuakes(p, isUsingNZAPI(config.region));
}

void requestFetch() { g_fetchNow = true; }   // ask the background fetch task to run now

// Background fetch task (pinned to core 0). Only touches the network + g_payload* — never the TFT/
// seismo — so a slow or stalled fetch can't freeze the UI loop on core 1.
void fetchTask(void*) {
  for (;;) {
    unsigned long waitStart = millis();
    while (!g_fetchNow && (millis() - waitStart < API_POLL_INTERVAL)) vTaskDelay(pdMS_TO_TICKS(50));
    g_fetchNow = false;
    while (g_payloadReady) vTaskDelay(pdMS_TO_TICKS(20));      // wait until the loop consumes the last one
    char region[16];
    strncpy(region, config.region, sizeof(region) - 1); region[sizeof(region) - 1] = '\0';
    String p = fetchPayload(getAPIEndpoint(region));
    if (p.length()) {
      g_payload = p;
      strncpy(g_payloadRegion, region, sizeof(g_payloadRegion) - 1);
      g_payloadRegion[sizeof(g_payloadRegion) - 1] = '\0';
      g_payloadReady = true;
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// EARTHQUAKE ALERT
// ═══════════════════════════════════════════════════════════════════════════

// ── ACTIVITY DETECTED screen ────────────────────────────────────────────────
// Deliberately NOT a quieter copy of the main screen: full-bleed, magnitude-coloured, no panels —
// just the one quake that's happening RIGHT NOW, with activity rings radiating out of its magnitude.
// ═══════════════════════════════════════════════════════════════════════════
// ALERT — "SEISMIC ACTIVITY DETECTED"  (Claude Design "Alert04a": diagonal corner burst)
// A full-bleed single-event takeover, deliberately unlike the calm 3-panel monitor: activity rings
// erupt from the BOTTOM-LEFT corner and sweep the whole screen while the content sits anchored right,
// clear of the blast. Severity (header/magnitude/rings/meta colour) is depth-aware — see severityColor().
// ═══════════════════════════════════════════════════════════════════════════
// Framed in a soft green border so it reads as part of the monitor, not a different product: everything
// is the theme green EXCEPT the magnitude number, which alone takes the depth-aware severity colour.
const int ALERT_OX = 20, ALERT_OY = 220;     // ring origin — bottom-left, inside the border
const int ALERT_RIGHT = 300;                 // right anchor, pulled in so content clears the frame
const int ALERT_PLACE_MAXW = 280;            // a place line runs from the right anchor to ~x20 before wrapping
const int ALERT_MAXR = 350;                  // reaches the far corner; rings are clipped inside the frame
const int ALERT_RING_N = 4, ALERT_RING_STEP = 4;   // slower now (was 8) — a calm pulse, not a fast burst
const int ALERT_VP_X = 8, ALERT_VP_Y = 8, ALERT_VP_W = 304, ALERT_VP_H = 224;   // ring clip = inside the frame
static int      alertRingPhase = 0;
static uint16_t alertColor     = 0;          // severity colour — used ONLY for the magnitude number
static EarthquakeData alertQuake;            // the one quake on screen — re-drawn on top of the rings each frame

// Draw the frame that ties the alert to the monitor's bordered panels — SAME green (PANEL_EDGE, the dim
// soft green), SAME thin 1px line, SAME radius 3 as drawDataPanel/drawMap/drawSeismograph. (Was a bright
// 2px double frame that read as a different product.) Drawn once; rings are clipped inside it.
static void drawAlertFrame() {
  tft.drawRoundRect(4, 4, 312, 232, 3, PANEL_EDGE);
}

// Scale an RGB565 colour's brightness by num/den (fades the rings as they travel out).
static uint16_t dimColor(uint16_t c, int num, int den) {
  if (num < 0) num = 0;
  if (num > den) num = den;
  int r = ((c >> 11) & 0x1F) * num / den;
  int g = ((c >> 5)  & 0x3F) * num / den;
  int b = ( c        & 0x1F) * num / den;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// Right-anchored text, redrawn on top of the rings each frame. The glyphs are opaque and land in the
// SAME place every frame, so redrawing them over themselves is pixel-identical → no flicker (unlike a
// fillRect-then-redraw, which would blank the hero magnitude 18×/second). Rings pass behind: where a
// ring crossed a glyph, this repaints it; ring specks left in the glyphs' negative space are cleared
// by the erase step on the next frame. Text sits on the flat black fill from displayEarthquakeAlert.
static void alertText(const char* s, int rightX, int baseY, const GFXfont* f, uint16_t col) {
  tft.setFreeFont(f);
  tft.setTextColor(col);
  tft.setCursor(rightX - tft.textWidth(s), baseY);
  tft.print(s);
}

// Right-anchored text in the BUILT-IN pixel font — the same font the main screen's header and meta use,
// so the alert reads as the same instrument rather than a different product. Note built-in fonts position
// from the cursor's TOP-left (GFX fonts use the baseline), so topY is the top of the glyph cell.
// (This also replaced a hand-rolled letter-spacing loop that measured each glyph with textWidth(): for a
// single space TFT_eSPI returns 0, not the advance, so every space collapsed — "27MAGO-17KMDEEP".)
static void alertTextPixel(const char* s, int rightX, int topY, uint8_t size, uint16_t col) {
  tft.setTextFont(1);
  tft.setTextSize(size);
  tft.setTextColor(col);
  tft.setCursor(rightX - tft.textWidth(s), topY);
  tft.print(s);
  tft.setTextSize(1);                                // restore the global size for everything else
}

// One place line: right-anchored SeisPlace22, with Māori macron bars restored above the flagged vowels
// (mac[] is aligned to s). Colour is `ink` — the place name is the calm readable element.
static void alertPlaceLine(const char* s, const bool* mac, int rightX, int baseY) {
  tft.setFreeFont(&SeisPlace22);
  int w = tft.textWidth(s);
  int x = rightX - w;
  tft.setTextColor(currentTheme.textPrimary);
  tft.setCursor(x, baseY);
  tft.print(s);
  for (int k = 0; s[k]; k++) {
    if (!mac[k]) continue;
    char pre[96]; strncpy(pre, s, k); pre[k] = '\0';
    int vx = x + tft.textWidth(pre);
    char ch[2] = { s[k], '\0' };
    int vw = tft.textWidth(ch);
    bool upper = (s[k] >= 'A' && s[k] <= 'Z');
    tft.drawFastHLine(vx + 1, baseY - (upper ? 18 : 13), (vw > 3) ? vw - 2 : vw, currentTheme.textPrimary);
  }
}

// All the text of the alert, redrawn on top of the rings every frame. Computes the place wrap first
// (1 line, or 2 when a long USGS name won't fit) and lifts the magnitude up when it's two lines.
static void drawAlertContent() {
  EarthquakeData* q = &alertQuake;
  uint16_t green = currentTheme.textAccent;    // everything is the theme green...
  uint16_t sev   = alertColor;                 // ...except the magnitude number (severity colour)

  // Place name → demacron → wrap to at most two right-anchored lines (never shrink; wrap instead).
  char buf[96]; bool mac[96];
  int len = demacron(q->location, buf, mac, sizeof(buf));
  tft.setFreeFont(&SeisPlace22);
  bool twoLine = tft.textWidth(buf) > ALERT_PLACE_MAXW;
  int split = -1;
  if (twoLine) {                       // pick the space split that best balances the two lines
    int bestScore = 1 << 30;
    for (int i = 1; i < len; i++) {
      if (buf[i] != ' ') continue;
      char a[96], b[96];
      strncpy(a, buf, i); a[i] = '\0';
      strcpy(b, buf + i + 1);
      int w1 = tft.textWidth(a), w2 = tft.textWidth(b);
      int over = (w1 > ALERT_PLACE_MAXW ? w1 - ALERT_PLACE_MAXW : 0) +
                 (w2 > ALERT_PLACE_MAXW ? w2 - ALERT_PLACE_MAXW : 0);
      int score = over * 1000 + (w1 > w2 ? w1 - w2 : w2 - w1);   // fit first, then balance
      if (score < bestScore) { bestScore = score; split = i; }
    }
    if (split < 0) twoLine = false;    // single unbreakable token — let it run full-width on one line
  }

  // Header — the main screen's pixel font, scaled 2x, so both screens share a type family.
  alertTextPixel("SEISMIC ACTIVITY", ALERT_RIGHT, 16, 2, green);
  alertTextPixel("DETECTED",         ALERT_RIGHT, 34, 2, green);

  // Magnitude hero — the ONE splash of severity colour. Lifted up when the place wraps to two lines.
  char magStr[10]; snprintf(magStr, sizeof(magStr), "M%.1f", q->magnitude);
  int magBaseY = twoLine ? 154 : 176;
  alertText(magStr, ALERT_RIGHT, magBaseY, &SeisMag, sev);

  // Place — one line, or two balanced lines (drawn in `ink`, the calm readable green).
  if (!twoLine) {
    alertPlaceLine(buf, mac, ALERT_RIGHT, 204);
  } else {
    char a[96]; bool ma[96];
    strncpy(a, buf, split); a[split] = '\0';
    for (int i = 0; i < split; i++) ma[i] = mac[i];
    const char* b = buf + split + 1;
    const bool* mb = mac + split + 1;
    alertPlaceLine(a, ma, ALERT_RIGHT, 184);
    alertPlaceLine(b, mb, ALERT_RIGHT, 206);
  }

  // How long ago — honest, not a blanket "NOW". USGS often publishes a quake many minutes (sometimes
  // hours) after it happens, so "NOW" was a lie for most alerts; only say NOW when it genuinely is recent.
  // (Hyphen, not the design's middot: these ASCII GFX fonts have no 0xB7 glyph.)
  char meta[48];
  time_t nowt = time(nullptr);
  unsigned long ts = q->timestamp;
  if (ts == 0 || nowt < 1600000000 || ((unsigned long)nowt - ts) < 120) {
    snprintf(meta, sizeof(meta), "NOW - %dKM DEEP", (int)q->depth);
  } else {
    String ago = getTimeAgo(ts); ago.toUpperCase();
    snprintf(meta, sizeof(meta), "%s AGO - %dKM DEEP", ago.c_str(), (int)q->depth);
  }
  // Same pixel font as the main screen's meta, but 2x — the age has twice been misread as "just now",
  // and this is a takeover screen with room to spare, so it should be legible at a glance.
  alertTextPixel(meta, ALERT_RIGHT, 216, 2, currentTheme.textSecondary);
}

// One animation frame: erase the old ring arcs, step the phase out, redraw them fading as they go,
// then repaint the content on top (rings pass behind the text). Rings are GREEN now and clipped to
// inside the frame (setViewport, absolute coords) so they never paint over the border. Driven from
// loop() every ~55ms.
void animateAlertRings() {
  uint16_t ringCol = currentTheme.textSecondary;     // dim green — echoes the map's faint distance rings
  const int gap = ALERT_MAXR / ALERT_RING_N;
  tft.setViewport(ALERT_VP_X, ALERT_VP_Y, ALERT_VP_W, ALERT_VP_H, false);   // false = keep absolute coords
  for (int k = 0; k < ALERT_RING_N; k++) {           // erase where each ring was (both px of the 2px stroke)
    int r = (alertRingPhase + k * gap) % ALERT_MAXR;
    if (r > 1) { tft.drawCircle(ALERT_OX, ALERT_OY, r, currentTheme.background);
                 tft.drawCircle(ALERT_OX, ALERT_OY, r + 1, currentTheme.background); }
  }
  alertRingPhase = (alertRingPhase + ALERT_RING_STEP) % ALERT_MAXR;
  for (int k = 0; k < ALERT_RING_N; k++) {           // redraw, brighter near the origin, fading outward
    int r = (alertRingPhase + k * gap) % ALERT_MAXR;
    if (r <= 1) continue;
    uint16_t c = dimColor(ringCol, ALERT_MAXR - r, ALERT_MAXR);
    tft.drawCircle(ALERT_OX, ALERT_OY, r, c);
    tft.drawCircle(ALERT_OX, ALERT_OY, r + 1, c);    // 2px stroke
  }
  tft.fillCircle(ALERT_OX, ALERT_OY, 2, currentTheme.textAccent);   // small brighter origin core (focal point)
  tft.resetViewport();
  drawAlertContent();                                // text always on top, crisp, over the full screen
}

void displayEarthquakeAlert(EarthquakeData* quake) {
  showingAlert   = true;
  alertStartTime = millis();
  alertQuake     = *quake;
  alertColor     = severityColor(quake->magnitude, quake->depth);   // depth-aware; drives the magnitude only
  alertRingPhase = 0;

  tft.fillScreen(currentTheme.background);
  drawAlertFrame();                                  // green frame (drawn once; rings are clipped inside it)
  drawAlertContent();                                // paint content immediately; loop() adds the rings
}

// Preview the alert on demand (seismograph tap) — cycles one demo quake per severity tier plus a
// two-line wrap case, so every state is reviewable without waiting for a real quake to land.
struct AlertDemo { float mag; float depth; const char* place; int agoSec; };
static const AlertDemo ALERT_DEMOS[] = {
  { 2.4f, 12.0f, "Porirua, New Zealand",              45 },   // green      — routine, recent -> "NOW"
  { 5.5f, 20.0f, "35km E of Cheviot",                480 },   // amber      — notable, "8M AGO"
  { 7.1f,  8.0f, "Off E. Honshu, Japan",              60 },   // red        — shallow big one, "NOW"
  { 4.8f, 33.0f, "10km SW of Fukushima-shi, Japan", 2040 },   // chartreuse — two-line wrap, "34M AGO"
};
static int alertDemoIdx = 0;

void previewAlertCycle() {
  const AlertDemo& d = ALERT_DEMOS[alertDemoIdx];
  alertDemoIdx = (alertDemoIdx + 1) % (int)(sizeof(ALERT_DEMOS) / sizeof(ALERT_DEMOS[0]));
  EarthquakeData q; q.clear();
  q.magnitude = d.mag; q.depth = d.depth;
  strncpy(q.location, d.place, sizeof(q.location) - 1);
  time_t nowt = time(nullptr);                         // stamp a plausible age so the meta line varies
  q.timestamp = (nowt > 1600000000) ? (unsigned long)nowt - d.agoSec : 0;
  q.isValid = true;
  displayEarthquakeAlert(&q);
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

// ── Alert sensitivity control (bottom-right of the settings screen) ──
// config.magThreshold is the minimum magnitude that fires the "EQ DETECTED" alert. Lower = more
// sensitive = more alerts (catch smaller / more-frequent quakes live); higher = only the big ones.
const float SENS_MIN = 0.5f, SENS_MAX = 5.0f;
const int SENS_X = 174, SENS_Y = 166, SENS_W = 132, SENS_H = 13;

void drawSensitivityBar() {
  tft.fillRect(SENS_X - 2, 140, SCREEN_WIDTH - (SENS_X - 2), 62, currentTheme.background);   // clear area
  tft.setTextFont(1);
  tft.setTextColor(currentTheme.textSecondary);
  tft.setCursor(SENS_X, 148);
  tft.print("ALERT SENSITIVITY");
  char v[8]; snprintf(v, sizeof(v), "M%.1f", config.magThreshold);
  tft.setTextColor(currentTheme.dataLatest);
  int vw = tft.textWidth(v);
  tft.setCursor(SCREEN_WIDTH - 8 - vw, 148);
  tft.print(v);
  float frac = (config.magThreshold - SENS_MIN) / (SENS_MAX - SENS_MIN);
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
  tft.drawRoundRect(SENS_X, SENS_Y, SENS_W, SENS_H, 3, currentTheme.border);
  int fillW = (int)(frac * (SENS_W - 2) + 0.5f);
  if (fillW > 0) tft.fillRoundRect(SENS_X + 1, SENS_Y + 1, fillW, SENS_H - 2, 2, currentTheme.dataLatest);
  tft.setTextColor(currentTheme.sub);
  tft.setCursor(SENS_X, SENS_Y + SENS_H + 4);
  tft.print("M0.5");
  const char* rt = "M5.0";
  tft.setCursor(SENS_X + SENS_W - tft.textWidth(rt), SENS_Y + SENS_H + 4);
  tft.print(rt);
  tft.setCursor(SENS_X, SENS_Y + SENS_H + 15);
  tft.print("LOWER = MORE ALERTS");
}

// True if a settings-screen tap fell on the sensitivity bar (forgiving margins).
bool sensBarHit(int sx, int sy) {
  return sx >= SENS_X - 8 && sx <= SENS_X + SENS_W + 8 &&
         sy >= SENS_Y - 20 && sy <= SENS_Y + SENS_H + 16;
}

// Set config.magThreshold from a tap x on the bar (snapped to 0.5), and persist it.
bool setSensitivityFromTap(int sx) {                  // returns true only if the value actually changed
  float frac = (float)(sx - SENS_X) / (float)SENS_W;
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
  float v = SENS_MIN + frac * (SENS_MAX - SENS_MIN);
  v = roundf(v * 10.0f) / 10.0f;                     // snap to 0.1 — fine control via the live drag
  if (v < SENS_MIN) v = SENS_MIN;
  if (v > SENS_MAX) v = SENS_MAX;
  if (v == config.magThreshold) return false;
  config.magThreshold = v;                            // NOTE: persisted on drag-release (avoids per-frame flash writes)
  return true;
}

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

  drawSensitivityBar();

  // ── Footer hint ──
  tft.drawFastHLine(0, SCREEN_HEIGHT - 16, SCREEN_WIDTH, currentTheme.border);
  tft.setTextFont(1);
  tft.setTextColor(currentTheme.sub);
  tft.drawCentreString("TAP: REGION  ·  SENSITIVITY BAR  ·  < CLOSE", 160, SCREEN_HEIGHT - 11, 1);
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

  drawUI();                         // immediate redraw (new map + cleared cells) confirms the choice
  requestFetch();                   // pull the new region's quakes in the background; the loop applies them
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

// Map raw FT6336G portrait coords to landscape screen coords (setRotation 3, 180°-flipped).
// If the gear hit-zone lands in the wrong corner on hardware, flip these 2 lines.
void mapTouch(int16_t tx, int16_t ty, int16_t &sx, int16_t &sy) {
  sx = (int16_t)(SCREEN_WIDTH - 1 - ty);        // native Y (0-319) -> screen X (flipped for the 180° rotation)
  sy = tx;                                       // native X (0-239) -> screen Y (flipped for the 180° rotation)
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
  static bool pressActed = false, releasedSinceOpen = true, sensDirty = false;

  if (touching) {
    if (pressStart == 0) pressStart = now;
    lastSeen = now;

    // Sensitivity bar = a live drag: update continuously (not once-per-press) so it feels smooth.
    if (showingRegionPicker && releasedSinceOpen && (now - pickerStartTime > 400)) {
      int16_t dsx, dsy; mapTouch(tx, ty, dsx, dsy);
      if (sensBarHit(dsx, dsy)) {
        if (setSensitivityFromTap(dsx)) { drawSensitivityBar(); sensDirty = true; }  // redraw only on change (no flicker)
        pressActed = true;                     // consume the press so region/close logic skips it
        lastActivity = now;
      }
    }

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
          // (the sensitivity bar is handled by the live-drag block above)
          // Gear corner (top-right) deliberately does NOT close.
        }
      } else if (sx > 264 && sy < 36) {                             // top-right corner — bigger, more forgiving cog zone
        Serial.println("  -> OPEN picker");
        drawRegionPicker();                                           // gear (top-right) → settings
        releasedSinceOpen = false;                                    // require a lift before any action
      } else if (sx < 115 && sy > 190) {
        previewAlertCycle();                                          // tap the seismograph → preview the alert (cycles states)
      } else {
        requestFetch();                                              // elsewhere → refresh data (background)
      }
    }
  } else if (now - lastSeen > 300) {                                  // finger confirmed UP for 300ms
    if (sensDirty) { saveConfig(); sensDirty = false; }               // persist the sensitivity once the drag ends
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
      requestFetch();                                            // background refresh
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
