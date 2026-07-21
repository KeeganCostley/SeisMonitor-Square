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
 * Hardware: ESP32 (Cheap Yellow Display)
 * Display: 320x240 ILI9341 TFT
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
#include <FS.h>
#include <time.h>
using namespace fs;
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <DNSServer.h>

// ═══════════════════════════════════════════════════════════════════════════
// HARDWARE CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

const int BUTTON_PIN = 0;
const int SCREEN_WIDTH = 320;
const int SCREEN_HEIGHT = 240;

// ═══════════════════════════════════════════════════════════════════════════
// TIMING CONSTANTS
// ═══════════════════════════════════════════════════════════════════════════

const unsigned long API_POLL_INTERVAL = 30000;      // 30 seconds
const unsigned long SEISMO_UPDATE_INTERVAL = 100;   // 100ms (was 50ms - slowed by 50%)
const unsigned long DISPLAY_CYCLE_INTERVAL = 60000; // 60 seconds
const unsigned long REST_MODE_TIMEOUT = 45000;      // 45 seconds
const unsigned long ALERT_DURATION = 30000;         // 30 seconds
const unsigned long DEBOUNCE_DELAY = 300;           // 300ms
const int HTTP_TIMEOUT = 5000;                      // 5 seconds
const byte DNS_PORT = 53;

// ═══════════════════════════════════════════════════════════════════════════
// DISPLAY LAYOUT
// ═══════════════════════════════════════════════════════════════════════════

const int PANEL_DIVIDER_X = 160;

// Left panel - Seismograph
const int SEISMO_X = 5;
const int SEISMO_Y = 30;
const int SEISMO_WIDTH = 150;
const int SEISMO_HEIGHT = 70;
const int SEISMO_CENTER_Y = SEISMO_Y + (SEISMO_HEIGHT / 2);
const int SEISMO_MAX_AMPLITUDE = 80;  // Maximum vertical movement for seismograph

// Left panel - Activity display
const int ACTIVITY_X = 5;
const int ACTIVITY_Y = 105;
const int ACTIVITY_WIDTH = 150;
const int ACTIVITY_HEIGHT = 130;

// Right panel - Map
const int MAP_X = 170;
const int MAP_Y = 30;
const int MAP_WIDTH = 145;
const int MAP_HEIGHT = 205;

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
const int MAX_RECENT_QUAKES = 20;
QuakeHistory recentQuakes[MAX_RECENT_QUAKES];
int recentQuakeCount = 0;

int displayMode = 0;
bool isRestMode = false;
bool isConfigMode = false;
bool showingAlert = false;



int seismoX = SEISMO_X;
int seismoLastY = SEISMO_CENTER_Y;

unsigned long lastAPICheck = 0;
unsigned long lastSeismoUpdate = 0;
unsigned long lastDisplaySwitch = 0;
unsigned long lastActivity = 0;
unsigned long lastButtonPress = 0;
unsigned long alertStartTime = 0;

String lastQuakeID = "";

// ═══════════════════════════════════════════════════════════════════════════
// THEME DEFINITIONS
// ═══════════════════════════════════════════════════════════════════════════

Theme createElegantTheme() {
  Theme t;
  t.background = TFT_BLACK;
  t.border = 0x2104;
  t.divider = 0x2104;
  t.textPrimary = 0xE71C;    // Warm white
  t.textSecondary = 0x8410;  // Medium grey
  t.textAccent = 0xC5A3;     // Subtle gold
  t.seismoLine = 0x9CF3;     // Cool grey
  t.seismoGrid = 0x18C3;     // Very dark grey
  t.mapOutline = 0x6B4D;     // Light grey
  t.mapCity = 0xC5A3;        // Subtle gold
  return t;
}

Theme createContrastTheme() {
  Theme t;
  t.background = TFT_BLACK;
  t.border = 0x2945;
  t.divider = 0x4208;
  t.textPrimary = TFT_WHITE;
  t.textSecondary = 0xBDF7;  // Light grey
  t.textAccent = 0xFB20;     // Safety orange
  t.seismoLine = TFT_WHITE;
  t.seismoGrid = 0x39E7;
  t.mapOutline = 0xBDF7;
  t.mapCity = 0xFB20;
  return t;
}

Theme createMonoTheme() {
  Theme t;
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
  return t;
}

Theme loadTheme(const char* aesthetic) {
  if (strcmp(aesthetic, "contrast") == 0) return createContrastTheme();
  if (strcmp(aesthetic, "mono") == 0) return createMonoTheme();
  return createElegantTheme();
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

int mapLatToScreen(float lat) {
  RegionBounds bounds = getRegionBounds(config.region);
  // Allow a region-specific display offset/zoom to better center some maps
  RegionBounds db = bounds;
  if (strcmp(config.region, "NZ") == 0) {
    // Slightly shift NZ longitude to the east for better centering
    db.lonMin += 1.5;
    db.lonMax += 1.5;
  }
  return MAP_Y + (int)((db.latMax - lat) / (db.latMax - db.latMin) * MAP_HEIGHT);
}

int mapLonToScreen(float lon) {
  RegionBounds bounds = getRegionBounds(config.region);
  RegionBounds db = bounds;
  if (strcmp(config.region, "NZ") == 0) {
    // Apply the same eastward shift to longitude mapping
    db.lonMin += 1.5;
    db.lonMax += 1.5;
  }
  return MAP_X + (int)((lon - db.lonMin) / (db.lonMax - db.lonMin) * MAP_WIDTH);
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
  if (magnitude >= 7.0) return 0x7800;
  if (magnitude >= 6.0) return TFT_RED;
  if (magnitude >= 5.0) return TFT_ORANGE;
  if (magnitude >= 4.0) return TFT_YELLOW;
  return currentTheme.textAccent;
}

// ═══════════════════════════════════════════════════════════════════════════
// FORWARD DECLARATIONS
// ═══════════════════════════════════════════════════════════════════════════

void drawUI();
void drawSeismograph();
void drawActivityPanel();
void drawMap();

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

// ═══════════════════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
  delay(100);
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n══════════════════════════════");
  Serial.println("   SEISMONITOR V2.0");
  Serial.println("══════════════════════════════\n");
  
  loadConfig();
  currentTheme = loadTheme(config.aesthetic);
  
  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(true);
  tft.fillScreen(currentTheme.background);
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  if (strlen(config.wifiSSID) == 0) {
    Serial.println("No WiFi - setup mode");
    isConfigMode = true;
    setupConfigPortal();
    return;
  }
  
  tft.setTextColor(currentTheme.textPrimary);
  tft.drawCentreString("SEISMONITOR", 160, 100, 4);
  tft.drawCentreString("Connecting...", 160, 130, 2);
  
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
  
  tft.drawCentreString("Connected", 160, 155, 2);
  delay(2000);
  
  checkForEarthquakes();
  drawUI();
  
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
  
  if (now - lastDisplaySwitch > DISPLAY_CYCLE_INTERVAL) {
    displayMode = (displayMode + 1) % 2;
    updateActivityRegion();
    lastDisplaySwitch = now;
  }
  
  if (!isRestMode && (now - lastActivity > REST_MODE_TIMEOUT)) {
    isRestMode = true;
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// PARTIAL SCREEN UPDATE HELPERS
// ═══════════════════════════════════════════════════════════════════════════

void updateSeismographRegion() {
  // Clear only the seismograph area
  tft.fillRect(SEISMO_X, SEISMO_Y, SEISMO_WIDTH, SEISMO_HEIGHT, currentTheme.background);
  drawSeismograph();
}

void updateActivityRegion() {
  // Clear only the activity panel area
  tft.fillRect(ACTIVITY_X, ACTIVITY_Y, ACTIVITY_WIDTH, ACTIVITY_HEIGHT, currentTheme.background);
  drawActivityPanel();
}

void updateMapEarthquakeMarkers() {
  // Redraw only the earthquake markers on the map (latest + recent quakes)
  // This is called after new earthquake data arrives
  // Optimization: redraw entire map instead of trying to selectively update markers
  // as it's complex to erase old markers precisely
  int x = MAP_X;
  int y = MAP_Y;
  int w = MAP_WIDTH;
  int h = MAP_HEIGHT;
  
  tft.fillRect(x, y, w, h, currentTheme.background);
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
  
  // Clean minimal borders
  tft.drawFastHLine(0, 0, SCREEN_WIDTH, currentTheme.border);
  tft.drawFastHLine(0, SCREEN_HEIGHT-1, SCREEN_WIDTH, currentTheme.border);
  tft.drawFastVLine(PANEL_DIVIDER_X, 0, SCREEN_HEIGHT, currentTheme.divider);
  
  // Headers
  tft.setTextColor(currentTheme.textSecondary);
  tft.setTextFont(2);
  tft.setCursor(5, 5);
  tft.print("SEISMOGRAPH");
  tft.setCursor(MAP_X, 5);
  tft.print(config.region);
  
  drawSeismograph();
  drawActivityPanel();
  drawMap();
}

void drawSeismograph() {
  // Vintage panel border (double-line)
  tft.drawRect(SEISMO_X - 3, SEISMO_Y - 3, SEISMO_WIDTH + 6, SEISMO_HEIGHT + 6, currentTheme.border);
  tft.drawRect(SEISMO_X - 2, SEISMO_Y - 2, SEISMO_WIDTH + 4, SEISMO_HEIGHT + 4, currentTheme.divider);

  // Slight textured background: faint horizontal rules and dots
  for (int y = SEISMO_Y; y <= SEISMO_Y + SEISMO_HEIGHT; y += 10) {
    for (int x = SEISMO_X; x < SEISMO_X + SEISMO_WIDTH; x += 6) {
      tft.drawPixel(x, y, currentTheme.seismoGrid);
    }
  }

  // Center horizontal line (thicker)
  for (int off = -1; off <= 1; off++) {
    tft.drawFastHLine(SEISMO_X, SEISMO_CENTER_Y + off, SEISMO_WIDTH, currentTheme.seismoGrid);
  }

  // Left-side scale ticks (vintage meter style)
  for (int t = 0; t <= SEISMO_HEIGHT; t += 15) {
    int yy = SEISMO_Y + t;
    tft.drawFastVLine(SEISMO_X - 8, yy, 2, currentTheme.textSecondary);
  }
}

void drawActivityPanel() {
  tft.fillRect(ACTIVITY_X, ACTIVITY_Y, ACTIVITY_WIDTH, ACTIVITY_HEIGHT, currentTheme.background);
  tft.drawRect(ACTIVITY_X, ACTIVITY_Y, ACTIVITY_WIDTH, ACTIVITY_HEIGHT, currentTheme.border);
  
  EarthquakeData* quake;
  const char* label;
  
  if (displayMode == 0) {
    quake = &latestQuake;
    label = "LATEST";
  } else {
    quake = &highestRegionalQuake;
    label = "HIGHEST 24H";
  }
  
  tft.setTextFont(2);
  tft.setTextColor(currentTheme.textSecondary);
  tft.setCursor(ACTIVITY_X + 5, ACTIVITY_Y + 3);
  tft.print(label);

  if (!quake->isValid) {
    tft.setCursor(ACTIVITY_X + 5, ACTIVITY_Y + 42);
    tft.print("NO DATA");
    return;
  }

  // Magnitude
  uint16_t dataColor = (displayMode == 0) ? TFT_ORANGE : TFT_RED;
  tft.setTextColor(dataColor);
  tft.setTextFont(4);
  tft.setCursor(ACTIVITY_X + 5, ACTIVITY_Y + 21);
  tft.print("M");
  tft.print(quake->magnitude, 1);

  // Location
  uint16_t locColor = (displayMode == 0) ? TFT_ORANGE : TFT_RED;
  drawWrappedText(quake->location, ACTIVITY_X + 5, ACTIVITY_Y + 50, ACTIVITY_WIDTH - 10, ACTIVITY_Y + ACTIVITY_HEIGHT - 20, 18, locColor);

  // Time
  tft.setTextFont(2);
  tft.setTextColor(currentTheme.textSecondary);
  tft.setCursor(ACTIVITY_X + 5, ACTIVITY_Y + ACTIVITY_HEIGHT - 18);
  tft.print(getTimeAgo(quake->timestamp));
  tft.print(" ago");
}

void drawMap() {
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
  
  // Plot recent quakes
  // Plot recent quakes (can be disabled via web settings)
  if (config.showRecentQuakes) {
    for (int i = 0; i < recentQuakeCount; i++) {
      if (!recentQuakes[i].valid) continue;
      
      int x = mapLonToScreen(recentQuakes[i].lon);
      int y = mapLatToScreen(recentQuakes[i].lat);
      
      if (x < MAP_X || x > MAP_X + MAP_WIDTH || y < MAP_Y || y > MAP_Y + MAP_HEIGHT) continue;
      
      uint16_t color = getMagnitudeColor(recentQuakes[i].mag);
      int radius = (recentQuakes[i].mag >= 6.0) ? 3 : ((recentQuakes[i].mag >= 5.0) ? 2 : 1);
      
      tft.fillCircle(x, y, radius, color);
    }
  }
  
  // Highlight LATEST quake location (orange dot) - most recent event
  if (latestQuake.isValid) {
    int x = mapLonToScreen(latestQuake.longitude);
    int y = mapLatToScreen(latestQuake.latitude);
    
    if (x >= MAP_X && x <= MAP_X + MAP_WIDTH && y >= MAP_Y && y <= MAP_Y + MAP_HEIGHT) {
      // Orange pulsing circle for latest quake
      tft.fillCircle(x, y, 3, TFT_ORANGE);
      tft.drawCircle(x, y, 4, TFT_ORANGE);
      tft.drawCircle(x, y, 5, TFT_ORANGE);
    }
  }
  
  // Highlight HIGHEST 24H quake location (red dot) - strongest event in 24h
  if (highestRegionalQuake.isValid) {
    int x = mapLonToScreen(highestRegionalQuake.longitude);
    int y = mapLatToScreen(highestRegionalQuake.latitude);
    
    if (x >= MAP_X && x <= MAP_X + MAP_WIDTH && y >= MAP_Y && y <= MAP_Y + MAP_HEIGHT) {
      // Red pulsing circle for highest regional quake
      tft.fillCircle(x, y, 3, TFT_RED);
      tft.drawCircle(x, y, 4, TFT_RED);
      tft.drawCircle(x, y, 5, TFT_RED);
    }
  }
}

void animateSeismograph() {
  // Produce a smoother, slightly vintage-looking waveform
  int movement = (random(100) < 8) ? random(-SEISMO_MAX_AMPLITUDE, SEISMO_MAX_AMPLITUDE) : random(-25, 25);
  int targetY = SEISMO_CENTER_Y + movement;
  int newY = (seismoLastY * 4 + targetY) / 5;

  if (newY < SEISMO_Y) newY = SEISMO_Y;
  if (newY > SEISMO_Y + SEISMO_HEIGHT) newY = SEISMO_Y + SEISMO_HEIGHT;

  // Draw a slightly thicker vintage line (3-pixel stroke)
  for (int dx = 0; dx < 2; dx++) {
    tft.drawLine(seismoX + dx, seismoLastY, seismoX + 1 + dx, newY, currentTheme.seismoLine);
  }

  // Erase a thin vertical column ahead to create scrolling effect
  int eraseX = SEISMO_X + ((seismoX - SEISMO_X + 3) % SEISMO_WIDTH);
  tft.fillRect(eraseX, SEISMO_Y, 2, SEISMO_HEIGHT, currentTheme.background);

  // Redraw faint texture where erased to preserve background texture
  for (int y = SEISMO_Y; y <= SEISMO_Y + SEISMO_HEIGHT; y += 10) {
    for (int x = eraseX; x < eraseX + 2; x += 6) {
      if (x >= SEISMO_X && x < SEISMO_X + SEISMO_WIDTH) tft.drawPixel(x, y, currentTheme.seismoGrid);
    }
  }

  seismoLastY = newY;
  seismoX++;
  if (seismoX >= SEISMO_X + SEISMO_WIDTH) seismoX = SEISMO_X;
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

  int northPts = sizeof(northIsland) / sizeof(northIsland[0]);
  int southPts = sizeof(southIsland) / sizeof(southIsland[0]);
  int stewartPts = sizeof(stewartIsland) / sizeof(stewartIsland[0]);

  for (int i = 0; i < northPts - 1; i++) {
    tft.drawLine(mapLonToScreen(northIsland[i][1]), mapLatToScreen(northIsland[i][0]),
                 mapLonToScreen(northIsland[i+1][1]), mapLatToScreen(northIsland[i+1][0]), currentTheme.mapOutline);
  }
  for (int i = 0; i < southPts - 1; i++) {
    tft.drawLine(mapLonToScreen(southIsland[i][1]), mapLatToScreen(southIsland[i][0]),
                 mapLonToScreen(southIsland[i+1][1]), mapLatToScreen(southIsland[i+1][0]), currentTheme.mapOutline);
  }
  for (int i = 0; i < stewartPts - 1; i++) {
    tft.drawLine(mapLonToScreen(stewartIsland[i][1]), mapLatToScreen(stewartIsland[i][0]),
                 mapLonToScreen(stewartIsland[i+1][1]), mapLatToScreen(stewartIsland[i+1][0]), currentTheme.mapOutline);
  }

  if (config.showCityDots) {
    tft.fillCircle(mapLonToScreen(174.78), mapLatToScreen(-41.28), 2, currentTheme.mapCity); // Wellington
    tft.fillCircle(mapLonToScreen(174.76), mapLatToScreen(-36.85), 2, currentTheme.mapCity); // Auckland
    tft.fillCircle(mapLonToScreen(172.64), mapLatToScreen(-43.53), 2, currentTheme.mapCity); // Christchurch
    tft.fillCircle(mapLonToScreen(170.50), mapLatToScreen(-45.87), 2, currentTheme.mapCity); // Dunedin
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
  int honshuPts = sizeof(honshu) / sizeof(honshu[0]);
  int shikokuPts = sizeof(shikoku) / sizeof(shikoku[0]);
  int kyushuPts = sizeof(kyushu) / sizeof(kyushu[0]);

  // Draw all islands using theme color
  for (int i = 0; i < hokkaidoPts - 1; i++) {
    tft.drawLine(mapLonToScreen(hokkaido[i][1]), mapLatToScreen(hokkaido[i][0]),
                 mapLonToScreen(hokkaido[i+1][1]), mapLatToScreen(hokkaido[i+1][0]), currentTheme.mapOutline);
  }
  for (int i = 0; i < honshuPts - 1; i++) {
    tft.drawLine(mapLonToScreen(honshu[i][1]), mapLatToScreen(honshu[i][0]),
                 mapLonToScreen(honshu[i+1][1]), mapLatToScreen(honshu[i+1][0]), currentTheme.mapOutline);
  }
  for (int i = 0; i < shikokuPts - 1; i++) {
    tft.drawLine(mapLonToScreen(shikoku[i][1]), mapLatToScreen(shikoku[i][0]),
                 mapLonToScreen(shikoku[i+1][1]), mapLatToScreen(shikoku[i+1][0]), currentTheme.mapOutline);
  }
  for (int i = 0; i < kyushuPts - 1; i++) {
    tft.drawLine(mapLonToScreen(kyushu[i][1]), mapLatToScreen(kyushu[i][0]),
                 mapLonToScreen(kyushu[i+1][1]), mapLatToScreen(kyushu[i+1][0]), currentTheme.mapOutline);
  }

  if (config.showCityDots) {
    tft.fillCircle(mapLonToScreen(139.69), mapLatToScreen(35.68), 2, currentTheme.mapCity); // Tokyo
    tft.fillCircle(mapLonToScreen(135.50), mapLatToScreen(34.69), 2, currentTheme.mapCity); // Osaka
    tft.fillCircle(mapLonToScreen(130.42), mapLatToScreen(33.59), 2, currentTheme.mapCity); // Fukuoka
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// REGIONAL MAPS - Taiwan
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
  for (int i = 0; i < pts - 1; i++) {
    tft.drawLine(mapLonToScreen(china[i][1]), mapLatToScreen(china[i][0]),
                 mapLonToScreen(china[i+1][1]), mapLatToScreen(china[i+1][0]), currentTheme.mapOutline);
  }

  if (config.showCityDots) {
    tft.fillCircle(mapLonToScreen(116.40), mapLatToScreen(39.90), 2, currentTheme.mapCity); // Beijing
    tft.fillCircle(mapLonToScreen(121.47), mapLatToScreen(31.23), 2, currentTheme.mapCity); // Shanghai
    tft.fillCircle(mapLonToScreen(113.26), mapLatToScreen(23.13), 2, currentTheme.mapCity); // Guangzhou
    tft.fillCircle(mapLonToScreen(104.07), mapLatToScreen(30.67), 2, currentTheme.mapCity); // Chengdu
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
  for (int i = 0; i < pts - 1; i++) {
    tft.drawLine(mapLonToScreen(california[i][1]), mapLatToScreen(california[i][0]),
                 mapLonToScreen(california[i+1][1]), mapLatToScreen(california[i+1][0]), currentTheme.mapOutline);
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
                 mapLonToScreen(fault[i+1][1]), mapLatToScreen(fault[i+1][0]), TFT_RED);
  }

  if (config.showCityDots) {
    tft.fillCircle(mapLonToScreen(-122.42), mapLatToScreen(37.77), 2, currentTheme.mapCity); // SF
    tft.fillCircle(mapLonToScreen(-118.24), mapLatToScreen(34.05), 2, currentTheme.mapCity); // LA
    tft.fillCircle(mapLonToScreen(-117.16), mapLatToScreen(32.72), 2, currentTheme.mapCity); // SD
    tft.fillCircle(mapLonToScreen(-121.49), mapLatToScreen(38.58), 2, currentTheme.mapCity); // Sacramento
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// REGIONAL MAPS - GLOBAL
// ═══════════════════════════════════════════════════════════════════════════

void drawGlobalMap() {
  int cx = MAP_X + MAP_WIDTH / 2;
  int cy = MAP_Y + MAP_HEIGHT / 2;
  int r = 65;

  // Globe outline
  tft.drawCircle(cx, cy, r, currentTheme.mapOutline);
  tft.drawCircle(cx, cy, r - 1, currentTheme.mapOutline);

  // Equator
  tft.drawFastHLine(cx - r, cy, r * 2, currentTheme.seismoGrid);

  // Tropics (dotted)
  int tropOff = r * 0.4;
  for (int x = -r + 10; x < r - 10; x += 2) {
    tft.drawPixel(cx + x, cy - tropOff, currentTheme.seismoGrid);
    tft.drawPixel(cx + x, cy + tropOff, currentTheme.seismoGrid);
  }

  // Meridians
  for (int off = -45; off <= 45; off += 22) {
    for (int y = cy - r + 5; y <= cy + r - 5; y += 2) {
      int dy = y - cy;
      float comp = sqrt(1.0 - (dy * dy) / (float)(r * r));
      tft.drawPixel(cx + (int)(off * comp), y, currentTheme.seismoGrid);
    }
  }

  // Helper lambda-style: draw polyline from int[][2] array
  // NORTH AMERICA (west coast outline, Alaska to Central America)
  const int na[][2] = {
    {cx - 52, cy - 55}, {cx - 55, cy - 48}, {cx - 50, cy - 42},  // Alaska
    {cx - 42, cy - 42}, {cx - 38, cy - 38}, {cx - 37, cy - 32},  // Canada west
    {cx - 35, cy - 28}, {cx - 33, cy - 22}, {cx - 30, cy - 16},  // US west
    {cx - 28, cy - 10}, {cx - 25, cy - 4},  {cx - 22, cy + 2},   // Mexico
    {cx - 20, cy + 6},                                              // Central America
  };
  for (int i = 0; i < 12; i++)
    tft.drawLine(na[i][0], na[i][1], na[i+1][0], na[i+1][1], currentTheme.mapOutline);

  // NA east coast
  const int nae[][2] = {
    {cx - 52, cy - 55}, {cx - 45, cy - 50},                        // Arctic Canada
    {cx - 38, cy - 48}, {cx - 30, cy - 44}, {cx - 22, cy - 42},   // Hudson Bay area
    {cx - 18, cy - 38}, {cx - 16, cy - 32}, {cx - 15, cy - 26},   // US east
    {cx - 18, cy - 20}, {cx - 20, cy - 14}, {cx - 22, cy - 8},    // Florida
    {cx - 20, cy + 6},                                              // Caribbean
  };
  for (int i = 0; i < 11; i++)
    tft.drawLine(nae[i][0], nae[i][1], nae[i+1][0], nae[i+1][1], currentTheme.mapOutline);

  // SOUTH AMERICA
  const int sa[][2] = {
    {cx - 20, cy + 6},  {cx - 18, cy + 10}, {cx - 16, cy + 16},   // NE coast
    {cx - 18, cy + 24}, {cx - 20, cy + 32}, {cx - 22, cy + 38},   // E coast / Brazil bulge
    {cx - 24, cy + 44}, {cx - 20, cy + 50}, {cx - 16, cy + 56},   // Patagonia
    {cx - 22, cy + 52}, {cx - 26, cy + 44}, {cx - 28, cy + 36},   // W coast
    {cx - 26, cy + 28}, {cx - 24, cy + 20}, {cx - 22, cy + 12},   // Peru/Ecuador
    {cx - 20, cy + 6},                                              // Close
  };
  for (int i = 0; i < 15; i++)
    tft.drawLine(sa[i][0], sa[i][1], sa[i+1][0], sa[i+1][1], currentTheme.mapOutline);

  // EUROPE
  const int eu[][2] = {
    {cx + 2, cy - 50}, {cx + 6, cy - 46}, {cx + 10, cy - 42},
    {cx + 14, cy - 38}, {cx + 16, cy - 32}, {cx + 14, cy - 28},
    {cx + 10, cy - 24}, {cx + 6, cy - 22}, {cx + 4, cy - 18},
  };
  for (int i = 0; i < 8; i++)
    tft.drawLine(eu[i][0], eu[i][1], eu[i+1][0], eu[i+1][1], currentTheme.mapOutline);

  // AFRICA
  const int af[][2] = {
    {cx + 4, cy - 18},  {cx + 8, cy - 16},  {cx + 14, cy - 12},
    {cx + 18, cy - 4},  {cx + 20, cy + 4},  {cx + 22, cy + 12},
    {cx + 18, cy + 20}, {cx + 14, cy + 28}, {cx + 10, cy + 34},
    {cx + 12, cy + 28}, {cx + 10, cy + 20}, {cx + 6, cy + 12},
    {cx + 2, cy + 4},   {cx + 0, cy - 4},   {cx + 2, cy - 12},
    {cx + 4, cy - 18},
  };
  for (int i = 0; i < 15; i++)
    tft.drawLine(af[i][0], af[i][1], af[i+1][0], af[i+1][1], currentTheme.mapOutline);

  // ASIA (east coast visible on globe)
  const int as[][2] = {
    {cx + 16, cy - 32}, {cx + 22, cy - 38}, {cx + 30, cy - 42},
    {cx + 40, cy - 40}, {cx + 48, cy - 34}, {cx + 50, cy - 24},
    {cx + 48, cy - 14}, {cx + 44, cy - 4},  {cx + 38, cy + 4},
    {cx + 30, cy + 8},  {cx + 28, cy + 4},  {cx + 34, cy - 2},
    {cx + 40, cy - 10}, {cx + 38, cy - 18},
  };
  for (int i = 0; i < 13; i++)
    tft.drawLine(as[i][0], as[i][1], as[i+1][0], as[i+1][1], currentTheme.mapOutline);

  // AUSTRALIA
  const int au[][2] = {
    {cx + 38, cy + 18}, {cx + 44, cy + 22}, {cx + 48, cy + 28},
    {cx + 46, cy + 36}, {cx + 40, cy + 40}, {cx + 34, cy + 38},
    {cx + 32, cy + 32}, {cx + 34, cy + 24}, {cx + 38, cy + 18},
  };
  for (int i = 0; i < 8; i++)
    tft.drawLine(au[i][0], au[i][1], au[i+1][0], au[i+1][1], currentTheme.mapOutline);

  // Ring of Fire (subtle dotted arc)
  for (int angle = -60; angle <= 240; angle += 4) {
    float rad = angle * 0.0174533f;
    int x = cx + (int)((r - 10) * cos(rad));
    int y = cy + (int)((r - 10) * sin(rad));
    int dist = (int)sqrt((float)((x - cx) * (x - cx) + (y - cy) * (y - cy)));
    if (dist < r - 3) tft.drawPixel(x, y, TFT_RED);
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
  
  // Stream-parse directly from HTTP response to avoid loading full payload into heap
  WiFiClient* stream = http.getStreamPtr();

  // Filter to reduce memory: only keep fields we need
  DynamicJsonDocument filter(200);
  filter["features"][0]["geometry"]["coordinates"] = true;
  filter["features"][0]["properties"]["mag"] = true;
  filter["features"][0]["properties"]["place"] = true;
  filter["features"][0]["properties"]["time"] = true;
  filter["features"][0]["properties"]["magnitude"] = true;
  filter["features"][0]["properties"]["locality"] = true;
  filter["features"][0]["properties"]["depth"] = true;
  filter["features"][0]["properties"]["publicID"] = true;
  filter["features"][0]["id"] = true;

  DynamicJsonDocument doc(32768);  // 32KB with filtering is enough
  DeserializationError error = deserializeJson(doc, *stream, DeserializationOption::Filter(filter));
  http.end();
  
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
  
  for (int i = 0; i < totalFeatures && processedCount < MAX_RECENT_QUAKES; i++) {
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
    
    if (recentQuakeCount < MAX_RECENT_QUAKES) {
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
      strncpy(highestRegionalQuake.location, h["properties"]["locality"] | "Unknown", sizeof(highestRegionalQuake.location) - 1);
      highestRegionalQuake.timestamp = parseISOToEpoch(h["properties"]["time"].as<const char*>());
    } else {
      highestRegionalQuake.latitude = h["geometry"]["coordinates"][1];
      highestRegionalQuake.longitude = h["geometry"]["coordinates"][0];
      highestRegionalQuake.depth = h["geometry"]["coordinates"][2];
      strncpy(highestRegionalQuake.location, h["properties"]["place"] | "Unknown", sizeof(highestRegionalQuake.location) - 1);
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
        loc = latest["properties"]["locality"] | "Unknown";
      } else {
        lon = latest["geometry"]["coordinates"][0];
        lat = latest["geometry"]["coordinates"][1];
        mag = latest["properties"]["mag"];
        depth = latest["geometry"]["coordinates"][2];
        loc = latest["properties"]["place"] | "Unknown";
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
  tft.setTextColor(magColor);
  tft.drawCentreString("EQ DETECTED", 160, 6, 2);
  int htw = tft.textWidth("EQ DETECTED", 2);
  int htx = 160 - htw / 2;
  tft.fillTriangle(htx - 16, 20, htx - 10, 6, htx - 4, 20, magColor);
  tft.fillTriangle(160 + htw / 2 + 4, 20, 160 + htw / 2 + 10, 6, 160 + htw / 2 + 16, 20, magColor);

  // Thin divider
  tft.drawFastHLine(20, 28, 280, currentTheme.divider);

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

  // Location (truncate with ellipsis if too wide)
  tft.setTextColor(currentTheme.textPrimary);
  char locBuf[64];
  strncpy(locBuf, quake->location, sizeof(locBuf) - 1);
  locBuf[sizeof(locBuf) - 1] = '\0';
  while (strlen(locBuf) > 3 && tft.textWidth(locBuf, 2) > 290) {
    locBuf[strlen(locBuf) - 1] = '\0';
  }
  tft.drawCentreString(locBuf, 160, dataY, 2);

  // Depth
  char depthBuf[32];
  snprintf(depthBuf, sizeof(depthBuf), "Depth: %.0fkm", quake->depth);
  tft.setTextColor(currentTheme.textSecondary);
  tft.drawCentreString(depthBuf, 160, dataY + 22, 2);
}

// ═══════════════════════════════════════════════════════════════════════════
// BUTTON HANDLING
// ═══════════════════════════════════════════════════════════════════════════

void handleButton() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    unsigned long now = millis();
    if (now - lastButtonPress > DEBOUNCE_DELAY) {
      lastButtonPress = now;
      displayMode = (displayMode + 1) % 2;
      drawActivityPanel();
      lastActivity = now;
      isRestMode = false;
      Serial.println("Button");
    }
  }
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
  server.onNotFound(handleWebNotFound);
  server.begin();
  Serial.println("Web server started");
}

String htmlEscape(const char* raw) {
  String out;
  out.reserve(strlen(raw));
  for (int i = 0; raw[i]; i++) {
    switch (raw[i]) {
      case '&':  out += "&amp;";  break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;";  break;
      default:   out += raw[i];   break;
    }
  }
  return out;
}

void handleWebRoot() {
  String html = R"(<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>SeisMonitor</title><style>*{margin:0;padding:0;box-sizing:border-box}body{font-family:-apple-system,sans-serif;background:#0a0a0a;color:#e8e8e8;padding:20px}h1{text-align:center;margin:20px 0;color:#C5A3;letter-spacing:2px}.container{max-width:500px;margin:0 auto;background:#1a1a1a;padding:30px;border:1px solid#333}label{display:block;margin:15px 0 5px;font-size:12px;text-transform:uppercase;color:#888}input,select{width:100%;padding:12px;background:#0f0f0f;border:1px solid#333;color:#fff;border-radius:5px;font-size:16px}input[type=range]{padding:0;height:40px;-webkit-appearance:none}input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:20px;height:20px;background:#C5A3;border-radius:50%;cursor:pointer}input[type=range]::-webkit-slider-runnable-track{height:4px;background:#333;border-radius:2px}o{display:block;text-align:center;color:#C5A3;font-size:24px;font-weight:600;margin:10px 0}button{width:100%;padding:15px;background:#C5A3;color:#000;border:none;border-radius:5px;font-size:14px;font-weight:600;margin-top:20px;cursor:pointer}button:hover{background:#d4b871}</style></head><body><h1>SEISMONITOR</h1><div class="container"><form action="/save" method="POST"><label>WiFi Network</label><input name="ssid" value=")";
  html += htmlEscape(config.wifiSSID);
  html += R"(" required><label>WiFi Password</label><input type="password" name="password" value=")";
  html += htmlEscape(config.wifiPassword);
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

  html += R"(<button type="submit">SAVE</button></form></div></body></html>)";
  
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
  
  saveConfig();
  
  String html = R"(<!DOCTYPE html><html><head><meta http-equiv="refresh" content="3;url=/"><style>body{font-family:Arial;background:#0a0a0a;color:#C5A3;display:flex;align-items:center;justify-content:center;height:100vh;margin:0;text-align:center}h1{letter-spacing:3px}</style></head><body><div><h1>SAVED</h1><p>Restarting...</p></div></body></html>)";
  
  server.send(200, "text/html", html);
  delay(2000);
  ESP.restart();
}

void handleWebNotFound() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}
