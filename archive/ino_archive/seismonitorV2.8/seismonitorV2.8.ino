/*
 * ═══════════════════════════════════════════════════════════════════════════
 * SEISMONITOR V2.0 - PRODUCTION EARTHQUAKE MONITORING SYSTEM
 * ═══════════════════════════════════════════════════════════════════════════
 * 
 * Professional, Swiss-minimalist earthquake monitor for ESP32 + TFT display
 * 
 * Features:
 * - Multi-region support (NZ, Japan, Taiwan, California, Global)
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
const unsigned long SEISMO_UPDATE_INTERVAL = 50;    // 50ms
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
const int SEISMO_HEIGHT = 90;
const int SEISMO_CENTER_Y = SEISMO_Y + (SEISMO_HEIGHT / 2);
const int SEISMO_MAX_AMPLITUDE = 80;  // Maximum vertical movement for seismograph

// Left panel - Activity display
const int ACTIVITY_X = 5;
const int ACTIVITY_Y = 130;
const int ACTIVITY_WIDTH = 150;
const int ACTIVITY_HEIGHT = 105;

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
const RegionBounds BOUNDS_TAIWAN = {21.9, 25.3, 120.0, 122.0};
const RegionBounds BOUNDS_CALIFORNIA = {32.5, 42.0, -124.5, -114.0};
const RegionBounds BOUNDS_GLOBAL = {-60.0, 75.0, -180.0, 180.0};

// ═══════════════════════════════════════════════════════════════════════════
// API ENDPOINTS - REGION SPECIFIC
// ═══════════════════════════════════════════════════════════════════════════

const char* API_NZ = "https://api.geonet.org.nz/quake?MMI=2";

// USGS Region-Specific Feeds (much better than global!)
const char* API_JAPAN = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/2.5_day.geojson"; // Will filter manually
const char* API_TAIWAN = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/2.5_day.geojson"; // Will filter manually
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
  
  preferences.end();
  Serial.println("Config saved");
}

// ═══════════════════════════════════════════════════════════════════════════
// REGIONAL FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

RegionBounds getRegionBounds(const char* region) {
  if (strcmp(region, "Japan") == 0) return BOUNDS_JAPAN;
  if (strcmp(region, "Taiwan") == 0) return BOUNDS_TAIWAN;
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
  if (strcmp(region, "Taiwan") == 0) return API_TAIWAN;
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
  return MAP_Y + (int)((bounds.latMax - lat) / (bounds.latMax - bounds.latMin) * MAP_HEIGHT);
}

int mapLonToScreen(float lon) {
  RegionBounds bounds = getRegionBounds(config.region);
  return MAP_X + (int)((lon - bounds.lonMin) / (bounds.lonMax - bounds.lonMin) * MAP_WIDTH);
}

// ═══════════════════════════════════════════════════════════════════════════
// UTILITY FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

String getTimeAgo(unsigned long timestamp) {
  unsigned long elapsed = (millis() - timestamp) / 1000;
  if (elapsed < 60) return String(elapsed) + "s";
  if (elapsed < 3600) return String(elapsed / 60) + "m";
  if (elapsed < 86400) return String(elapsed / 3600) + "h";
  return String(elapsed / 86400) + "d";
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
void drawTaiwanMap();
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
  
  tft.setTextSize(2);
  tft.setTextColor(currentTheme.textPrimary);
  tft.setCursor(70, 100);
  tft.print("SEISMONITOR");
  tft.setTextSize(1);
  tft.setCursor(100, 125);
  tft.print("Connecting...");
  
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
  
  setupWebServer();
  
  tft.setCursor(100, 145);
  tft.print("Connected");
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
    drawActivityPanel();
    lastDisplaySwitch = now;
  }
  
  if (!isRestMode && (now - lastActivity > REST_MODE_TIMEOUT)) {
    isRestMode = true;
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
  tft.setTextSize(1);
  tft.setTextColor(currentTheme.textSecondary);
  tft.setCursor(5, 10);
  tft.print("SEISMOGRAPH");
  tft.setCursor(MAP_X, 10);
  tft.print(config.region);
  
  drawSeismograph();
  drawActivityPanel();
  drawMap();
}

void drawSeismograph() {
  // Centerline
  tft.drawFastHLine(SEISMO_X, SEISMO_CENTER_Y, SEISMO_WIDTH, currentTheme.seismoGrid);
  
  // Grid dots
  for (int y = SEISMO_Y; y <= SEISMO_Y + SEISMO_HEIGHT; y += 20) {
    for (int x = SEISMO_X; x < SEISMO_X + SEISMO_WIDTH; x += 5) {
      tft.drawPixel(x, y, currentTheme.seismoGrid);
    }
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
  
  tft.setTextSize(1);
  tft.setTextColor(currentTheme.textSecondary);
  tft.setCursor(ACTIVITY_X + 5, ACTIVITY_Y + 5);
  tft.print(label);
  
  Serial.print("Drawing panel: ");
  Serial.print(label);
  Serial.print(" - Valid: ");
  Serial.println(quake->isValid ? "YES" : "NO");
  
  if (!quake->isValid) {
    tft.setCursor(ACTIVITY_X + 5, ACTIVITY_Y + 45);
    tft.print("NO DATA");
    Serial.println("Displaying: NO DATA");
    return;
  }
  
  Serial.print("Displaying: M");
  Serial.print(quake->magnitude);
  Serial.print(" at ");
  Serial.println(quake->location);
  
  // Magnitude
  tft.setTextSize(config.fontSize + 1);
  tft.setTextColor(currentTheme.textAccent);
  tft.setCursor(ACTIVITY_X + 5, ACTIVITY_Y + 25);
  tft.print("M");
  tft.print(quake->magnitude, 1);
  
  // Location (wrapped)
  tft.setTextSize(config.fontSize);
  tft.setTextColor(currentTheme.textPrimary);
  
  int y = ACTIVITY_Y + 55;
  int charsPerLine = 18;
  char* loc = quake->location;
  int len = strlen(loc);
  int pos = 0;
  
  while (pos < len && y < ACTIVITY_Y + ACTIVITY_HEIGHT - 15) {
    char line[20];
    int lineLen = min(charsPerLine, len - pos);
    
    if (pos + lineLen < len) {
      int breakPos = lineLen;
      while (breakPos > 0 && loc[pos + breakPos] != ' ') breakPos--;
      if (breakPos > 0) lineLen = breakPos;
    }
    
    strncpy(line, loc + pos, lineLen);
    line[lineLen] = '\0';
    
    tft.setCursor(ACTIVITY_X + 5, y);
    tft.print(line);
    
    pos += lineLen;
    if (loc[pos] == ' ') pos++;
    y += 9;
  }
  
  // Time
  tft.setTextSize(1);
  tft.setTextColor(currentTheme.textSecondary);
  tft.setCursor(ACTIVITY_X + 5, ACTIVITY_Y + ACTIVITY_HEIGHT - 12);
  tft.print(getTimeAgo(quake->timestamp));
  tft.print(" ago");
}

void drawMap() {
  if (strcmp(config.region, "NZ") == 0) drawNZMap();
  else if (strcmp(config.region, "Japan") == 0) drawJapanMap();
  else if (strcmp(config.region, "Taiwan") == 0) drawTaiwanMap();
  else if (strcmp(config.region, "California") == 0) drawCaliforniaMap();
  else drawGlobalMap();
  
  // Plot recent quakes
  for (int i = 0; i < recentQuakeCount; i++) {
    if (!recentQuakes[i].valid) continue;
    
    int x = mapLonToScreen(recentQuakes[i].lon);
    int y = mapLatToScreen(recentQuakes[i].lat);
    
    if (x < MAP_X || x > MAP_X + MAP_WIDTH || y < MAP_Y || y > MAP_Y + MAP_HEIGHT) continue;
    
    uint16_t color = getMagnitudeColor(recentQuakes[i].mag);
    int radius = (recentQuakes[i].mag >= 6.0) ? 3 : ((recentQuakes[i].mag >= 5.0) ? 2 : 1);
    
    tft.fillCircle(x, y, radius, color);
  }
  
  // Highlight LATEST quake location (red dot) - always show most recent event
  if (latestQuake.isValid) {
    int x = mapLonToScreen(latestQuake.longitude);
    int y = mapLatToScreen(latestQuake.latitude);
    
    if (x >= MAP_X && x <= MAP_X + MAP_WIDTH && y >= MAP_Y && y <= MAP_Y + MAP_HEIGHT) {
      // Red pulsing circle for latest quake
      tft.fillCircle(x, y, 3, TFT_RED);
      tft.drawCircle(x, y, 4, TFT_RED);
      tft.drawCircle(x, y, 5, TFT_RED);
    }
  }
}

void animateSeismograph() {
  int movement = (random(100) < 5) ? random(-SEISMO_MAX_AMPLITUDE, SEISMO_MAX_AMPLITUDE) : random(-30, 30);
  int targetY = SEISMO_CENTER_Y + movement;
  int newY = (seismoLastY * 3 + targetY) / 4;
  
  if (newY < SEISMO_Y) newY = SEISMO_Y;
  if (newY > SEISMO_Y + SEISMO_HEIGHT) newY = SEISMO_Y + SEISMO_HEIGHT;
  
  tft.drawLine(seismoX, seismoLastY, seismoX + 1, newY, currentTheme.seismoLine);
  
  int eraseX = (seismoX + 10) % SEISMO_WIDTH;
  if (eraseX < SEISMO_X) eraseX = SEISMO_X;
  tft.drawFastVLine(eraseX, SEISMO_Y, SEISMO_HEIGHT, currentTheme.background);
  
  if (eraseX % 5 == 0) {
    for (int y = SEISMO_Y; y <= SEISMO_Y + SEISMO_HEIGHT; y += 20) {
      tft.drawPixel(eraseX, y, currentTheme.seismoGrid);
    }
  }
  tft.drawPixel(eraseX, SEISMO_CENTER_Y, currentTheme.seismoGrid);
  
  seismoLastY = newY;
  seismoX++;
  if (seismoX >= SEISMO_X + SEISMO_WIDTH) seismoX = SEISMO_X;
}

// ═══════════════════════════════════════════════════════════════════════════
// REGIONAL MAPS - NEW ZEALAND
// ═══════════════════════════════════════════════════════════════════════════

void drawNZMap() {
  const float northIsland[][2] = {
    // Far North - Cape Reinga
    {-34.42, 172.68}, {-34.40, 173.05}, {-34.50, 173.18},
    // Northland East Coast
    {-35.10, 173.95}, {-35.32, 174.11}, {-35.68, 174.32},
    {-35.25, 174.08}, {-35.50, 174.15}, {-35.80, 174.35},
    // Auckland
    {-36.40, 174.52}, {-36.85, 174.76}, {-37.05, 174.87},
    // Coromandel Peninsula
    {-36.82, 175.50}, {-37.03, 175.68}, {-37.20, 175.85},
    // Bay of Plenty
    {-37.50, 176.20}, {-37.70, 176.95}, {-37.92, 177.48},
    // East Cape
    {-37.52, 178.03}, {-37.70, 178.35}, {-37.85, 178.55},
    // Hawke's Bay
    {-38.50, 178.05}, {-38.92, 177.68}, {-39.30, 177.05},
    // Wairarapa Coast
    {-40.28, 176.25}, {-40.85, 175.56}, {-41.05, 175.38},
    // Wellington (southern tip)
    {-41.28, 174.78}, {-41.35, 174.82},
    // Cook Strait / Wellington West
    {-41.25, 174.50}, {-41.08, 174.05},
    // West Coast (going back up north)
    {-40.90, 174.65}, {-40.50, 174.88}, {-39.70, 174.90},
    // Taranaki bulge
    {-39.28, 173.75}, {-39.05, 174.05}, {-38.65, 174.08},
    // Continue west coast northward
    {-38.36, 174.55}, {-37.82, 174.75}, {-37.48, 174.52},
    // Auckland West Coast
    {-37.02, 174.48}, {-36.50, 174.32},
    // Northland West Coast
    {-35.92, 173.95}, {-35.48, 173.85}, {-35.05, 173.42},
    // Back to start
    {-34.85, 173.08}, {-34.60, 172.85}, {-34.42, 172.68}
  };
  
  const float southIsland[][2] = {
    {-40.92, 173.95}, {-41.05, 174.02}, {-41.15, 174.25},
    {-41.08, 174.18}, {-41.22, 174.05}, {-41.12, 173.82},
    {-41.65, 174.12}, {-42.40, 173.68}, {-42.85, 173.12},
    {-43.58, 172.68}, {-43.65, 172.95}, {-43.82, 173.05},
    {-43.75, 172.88}, {-43.88, 172.72}, {-44.02, 171.78},
    {-44.58, 171.22}, {-45.05, 170.82}, {-45.82, 170.65},
    {-45.88, 170.52}, {-45.78, 170.70}, {-46.05, 170.28},
    {-46.42, 169.75}, {-46.58, 168.95}, {-46.65, 168.12},
    {-46.60, 167.92}, {-46.48, 167.72}, {-46.18, 167.48},
    {-45.92, 167.25}, {-45.68, 167.18}, {-45.42, 167.05},
    {-45.18, 166.98}, {-44.88, 167.05}, {-44.55, 167.48},
    {-44.15, 167.92}, {-43.72, 168.38}, {-43.25, 169.05},
    {-42.85, 169.48}, {-42.45, 170.95}, {-41.95, 171.48},
    {-41.52, 172.05}, {-41.28, 172.65}, {-41.05, 172.88},
    {-40.92, 173.52}, {-40.92, 173.95}
  };
  
  // Stewart Island (third main island, south of South Island)
  const float stewartIsland[][2] = {
    {-46.85, 168.15}, {-46.82, 168.35}, {-46.78, 168.48},
    {-46.88, 168.58}, {-46.98, 168.62}, {-47.08, 168.58},
    {-47.15, 168.48}, {-47.18, 168.32}, {-47.15, 168.15},
    {-47.08, 168.02}, {-46.98, 167.95}, {-46.88, 167.98},
    {-46.85, 168.15}
  };
  
  int northPoints = sizeof(northIsland) / sizeof(northIsland[0]);
  int southPoints = sizeof(southIsland) / sizeof(southIsland[0]);
  int stewartPoints = sizeof(stewartIsland) / sizeof(stewartIsland[0]);
  
  // Draw North Island
  for (int i = 0; i < northPoints - 1; i++) {
    int x1 = mapLonToScreen(northIsland[i][1]);
    int y1 = mapLatToScreen(northIsland[i][0]);
    int x2 = mapLonToScreen(northIsland[i+1][1]);
    int y2 = mapLatToScreen(northIsland[i+1][0]);
    tft.drawLine(x1, y1, x2, y2, currentTheme.mapOutline);
  }
  
  // Draw South Island
  for (int i = 0; i < southPoints - 1; i++) {
    int x1 = mapLonToScreen(southIsland[i][1]);
    int y1 = mapLatToScreen(southIsland[i][0]);
    int x2 = mapLonToScreen(southIsland[i+1][1]);
    int y2 = mapLatToScreen(southIsland[i+1][0]);
    tft.drawLine(x1, y1, x2, y2, currentTheme.mapOutline);
  }
  
  // Draw Stewart Island
  for (int i = 0; i < stewartPoints - 1; i++) {
    int x1 = mapLonToScreen(stewartIsland[i][1]);
    int y1 = mapLatToScreen(stewartIsland[i][0]);
    int x2 = mapLonToScreen(stewartIsland[i+1][1]);
    int y2 = mapLatToScreen(stewartIsland[i+1][0]);
    tft.drawLine(x1, y1, x2, y2, currentTheme.mapOutline);
  }
  
  // Cities
  tft.fillCircle(mapLonToScreen(174.78), mapLatToScreen(-41.28), 2, currentTheme.mapCity); // Wellington
  tft.fillCircle(mapLonToScreen(174.76), mapLatToScreen(-36.85), 2, currentTheme.mapCity); // Auckland
  tft.fillCircle(mapLonToScreen(172.64), mapLatToScreen(-43.53), 2, currentTheme.mapCity); // Christchurch
  tft.fillCircle(mapLonToScreen(170.50), mapLatToScreen(-45.87), 2, currentTheme.mapCity); // Dunedin
}

// ═══════════════════════════════════════════════════════════════════════════
// REGIONAL MAPS - JAPAN
// ═══════════════════════════════════════════════════════════════════════════

void drawJapanMap() {
  // HOKKAIDO - Clean northern island
  const float hokkaido[][2] = {
    {45.52, 141.35}, {45.50, 142.15}, {45.38, 142.95}, {45.18, 143.65},
    {44.88, 144.35}, {44.48, 145.15}, {43.98, 145.68}, {43.48, 145.88},
    {42.98, 145.48}, {42.48, 144.78}, {42.08, 143.98}, {41.78, 143.18},
    {41.58, 142.28}, {41.48, 141.38}, {41.58, 140.58}, {41.88, 140.18},
    {42.38, 139.98}, {42.98, 140.18}, {43.58, 140.58}, {44.18, 140.88},
    {44.78, 141.18}, {45.28, 141.35}, {45.52, 141.35}
  };
  
  // HONSHU - Main island (SIMPLE, NO LOOPS!)
  const float honshu[][2] = {
    // North tip
    {41.52, 140.90}, {41.18, 140.58}, {40.78, 140.28}, {40.38, 139.98},
    {39.98, 139.78}, {39.58, 139.68}, {39.18, 139.58}, {38.78, 139.48},
    // East coast down
    {38.38, 139.38}, {37.98, 139.18}, {37.58, 138.88}, {37.18, 138.58},
    {36.78, 138.18}, {36.48, 137.68}, {36.18, 137.28}, {35.88, 136.98},
    // Tokyo Bay bulge
    {35.58, 137.38}, {35.38, 137.88}, {35.28, 138.48}, {35.28, 138.98},
    {35.18, 139.48}, {35.08, 139.78}, {34.98, 139.88}, {34.88, 139.78},
    // South coast
    {34.78, 139.58}, {34.68, 139.28}, {34.58, 138.88}, {34.38, 138.38},
    {34.18, 137.78}, {33.98, 137.18}, {33.88, 136.48}, {33.88, 135.78},
    {33.98, 135.18}, {34.08, 134.58}, {34.28, 134.08}, {34.58, 133.58},
    // Southwest tip
    {34.98, 133.18}, {35.38, 132.88}, {35.88, 132.68}, {36.38, 132.88},
    // West coast going back up
    {36.88, 133.28}, {37.38, 133.78}, {37.88, 134.38}, {38.38, 135.08},
    {38.88, 135.88}, {39.38, 136.78}, {39.88, 137.78}, {40.38, 138.88},
    {40.88, 139.88}, {41.28, 140.68}, {41.52, 140.90}
  };
  
  // KYUSHU - Southern island
  const float kyushu[][2] = {
    {33.90, 131.20}, {33.68, 131.58}, {33.48, 131.88}, {33.18, 132.08},
    {32.88, 132.08}, {32.58, 131.88}, {32.38, 131.58}, {32.18, 131.18},
    {32.08, 130.68}, {32.18, 130.28}, {32.48, 130.08}, {32.88, 130.08},
    {33.28, 130.18}, {33.58, 130.38}, {33.78, 130.68}, {33.88, 131.08},
    {33.90, 131.20}
  };
  
  // SHIKOKU - Fourth island
  const float shikoku[][2] = {
    {34.15, 134.60}, {33.88, 134.78}, {33.58, 134.68}, {33.38, 134.38},
    {33.28, 133.98}, {33.28, 133.48}, {33.48, 133.08}, {33.78, 132.78},
    {34.08, 132.68}, {34.38, 132.88}, {34.58, 133.28}, {34.58, 133.78},
    {34.38, 134.18}, {34.15, 134.60}
  };
  
  int hokkaidoPoints = sizeof(hokkaido) / sizeof(hokkaido[0]);
  int honshuPoints = sizeof(honshu) / sizeof(honshu[0]);
  int kyushuPoints = sizeof(kyushu) / sizeof(kyushu[0]);
  int shikokuPoints = sizeof(shikoku) / sizeof(shikoku[0]);
  
  // Draw all islands
  for (int i = 0; i < hokkaidoPoints - 1; i++) {
    int x1 = mapLonToScreen(hokkaido[i][1]);
    int y1 = mapLatToScreen(hokkaido[i][0]);
    int x2 = mapLonToScreen(hokkaido[i+1][1]);
    int y2 = mapLatToScreen(hokkaido[i+1][0]);
    tft.drawLine(x1, y1, x2, y2, currentTheme.mapOutline);
  }
  
  for (int i = 0; i < honshuPoints - 1; i++) {
    int x1 = mapLonToScreen(honshu[i][1]);
    int y1 = mapLatToScreen(honshu[i][0]);
    int x2 = mapLonToScreen(honshu[i+1][1]);
    int y2 = mapLatToScreen(honshu[i+1][0]);
    tft.drawLine(x1, y1, x2, y2, currentTheme.mapOutline);
  }
  
  for (int i = 0; i < kyushuPoints - 1; i++) {
    int x1 = mapLonToScreen(kyushu[i][1]);
    int y1 = mapLatToScreen(kyushu[i][0]);
    int x2 = mapLonToScreen(kyushu[i+1][1]);
    int y2 = mapLatToScreen(kyushu[i+1][0]);
    tft.drawLine(x1, y1, x2, y2, currentTheme.mapOutline);
  }
  
  for (int i = 0; i < shikokuPoints - 1; i++) {
    int x1 = mapLonToScreen(shikoku[i][1]);
    int y1 = mapLatToScreen(shikoku[i][0]);
    int x2 = mapLonToScreen(shikoku[i+1][1]);
    int y2 = mapLatToScreen(shikoku[i+1][0]);
    tft.drawLine(x1, y1, x2, y2, currentTheme.mapOutline);
  }
  
  // Cities
  tft.fillCircle(mapLonToScreen(139.69), mapLatToScreen(35.68), 2, currentTheme.mapCity); // Tokyo
  tft.fillCircle(mapLonToScreen(135.50), mapLatToScreen(34.69), 2, currentTheme.mapCity); // Osaka
  tft.fillCircle(mapLonToScreen(130.42), mapLatToScreen(33.59), 2, currentTheme.mapCity); // Fukuoka
}

// ═══════════════════════════════════════════════════════════════════════════
// REGIONAL MAPS - TAIWAN
// ═══════════════════════════════════════════════════════════════════════════

void drawTaiwanMap() {
  // Clean Taiwan outline - elongated north-south oval
  const float taiwan[][2] = {
    // North tip (Taipei area)
    {25.30, 121.57}, {25.28, 121.70}, {25.22, 121.85}, {25.12, 121.96},
    // Northeast coast  
    {24.98, 122.00}, {24.80, 121.98}, {24.60, 121.92}, {24.40, 121.84},
    {24.20, 121.76}, {24.00, 121.68}, {23.80, 121.62}, {23.60, 121.56},
    // East coast (mountainous side)
    {23.40, 121.52}, {23.20, 121.48}, {23.00, 121.44}, {22.80, 121.38},
    {22.60, 121.30}, {22.40, 121.20}, {22.20, 121.08},
    // South tip (Kenting)
    {22.00, 120.85}, {21.95, 120.70}, {22.00, 120.55},
    // Southwest coast
    {22.10, 120.45}, {22.25, 120.35}, {22.40, 120.28}, {22.60, 120.24},
    {22.80, 120.20}, {23.00, 120.18}, {23.20, 120.16}, {23.40, 120.15},
    // West coast (plains side)
    {23.60, 120.14}, {23.80, 120.14}, {24.00, 120.16}, {24.20, 120.20},
    {24.40, 120.28}, {24.60, 120.40}, {24.80, 120.56}, {24.98, 120.75},
    // Northwest coast back to Taipei
    {25.10, 120.95}, {25.18, 121.15}, {25.24, 121.35}, {25.30, 121.57}
  };
  
  int points = sizeof(taiwan) / sizeof(taiwan[0]);
  
  // Draw outline
  for (int i = 0; i < points - 1; i++) {
    int x1 = mapLonToScreen(taiwan[i][1]);
    int y1 = mapLatToScreen(taiwan[i][0]);
    int x2 = mapLonToScreen(taiwan[i+1][1]);
    int y2 = mapLatToScreen(taiwan[i+1][0]);
    tft.drawLine(x1, y1, x2, y2, currentTheme.mapOutline);
  }
  
  // Cities with larger markers
  tft.fillCircle(mapLonToScreen(121.56), mapLatToScreen(25.03), 2, currentTheme.mapCity); // Taipei
  tft.fillCircle(mapLonToScreen(120.68), mapLatToScreen(24.15), 2, currentTheme.mapCity); // Taichung  
  tft.fillCircle(mapLonToScreen(120.31), mapLatToScreen(22.63), 2, currentTheme.mapCity); // Kaohsiung
}

// ═══════════════════════════════════════════════════════════════════════════
// REGIONAL MAPS - CALIFORNIA
// ═══════════════════════════════════════════════════════════════════════════

void drawCaliforniaMap() {
  // Ultra-detailed California coastline
  const float california[][2] = {
    // Oregon border
    {42.00, -124.21}, {41.85, -124.20}, {41.70, -124.18}, {41.55, -124.15},
    {41.40, -124.12}, {41.25, -124.18}, {41.10, -124.25}, {40.95, -124.35},
    // North coast
    {40.80, -124.38}, {40.65, -124.40}, {40.50, -124.38}, {40.35, -124.35},
    {40.20, -124.30}, {40.05, -124.28}, {39.90, -124.20}, {39.75, -124.05},
    {39.60, -123.88}, {39.45, -123.78}, {39.30, -123.75}, {39.15, -123.70},
    // Mendocino area
    {39.00, -123.65}, {38.85, -123.55}, {38.70, -123.45}, {38.55, -123.35},
    {38.40, -123.22}, {38.25, -123.08}, {38.10, -122.98}, {37.95, -122.92},
    // San Francisco Bay (indent)
    {37.80, -122.88}, {37.70, -122.78}, {37.62, -122.52}, {37.55, -122.38},
    {37.48, -122.28}, {37.40, -122.20}, {37.32, -122.15}, {37.25, -122.12},
    // Peninsula south of SF
    {37.15, -122.18}, {37.05, -122.25}, {36.95, -122.32}, {36.85, -122.40},
    {36.75, -122.48}, {36.65, -122.52}, {36.55, -122.45}, {36.45, -122.28},
    // Monterey Bay (indent)
    {36.35, -122.08}, {36.28, -121.92}, {36.22, -121.82}, {36.15, -121.75},
    {36.08, -121.68}, {36.00, -121.65}, {35.92, -121.62}, {35.85, -121.55},
    // Central coast
    {35.75, -121.42}, {35.65, -121.32}, {35.55, -121.22}, {35.45, -121.08},
    {35.35, -120.95}, {35.25, -120.88}, {35.15, -120.80}, {35.05, -120.72},
    {34.95, -120.68}, {34.85, -120.65}, {34.75, -120.60}, {34.65, -120.52},
    // Santa Barbara area
    {34.55, -120.45}, {34.45, -120.35}, {34.35, -120.22}, {34.25, -120.05},
    {34.15, -119.88}, {34.08, -119.75}, {34.02, -119.62}, {33.98, -119.48},
    // LA area
    {33.95, -119.28}, {33.92, -119.08}, {33.88, -118.88}, {33.82, -118.68},
    {33.75, -118.48}, {33.68, -118.32}, {33.60, -118.22}, {33.52, -118.15},
    // Orange County
    {33.45, -118.08}, {33.38, -118.02}, {33.30, -117.92}, {33.22, -117.78},
    {33.15, -117.65}, {33.08, -117.52}, {33.00, -117.40}, {32.92, -117.30},
    // San Diego
    {32.85, -117.25}, {32.75, -117.22}, {32.65, -117.18}, {32.55, -117.15},
    {32.53, -117.12},
    // Mexico border (straight across)
    {32.53, -117.12}, {32.60, -116.00}, {32.68, -115.00}, {32.72, -114.72},
    // East side (straight line up)
    {33.00, -114.62}, {33.50, -114.45}, {34.05, -114.28}, {34.60, -114.35},
    {35.15, -114.48}, {35.70, -114.52}, {36.25, -114.38}, {36.80, -114.25},
    {37.35, -114.32}, {37.90, -114.45}, {38.45, -114.58}, {39.00, -114.68},
    {39.55, -114.78}, {40.10, -114.88}, {40.65, -114.92}, {41.20, -114.88},
    {41.75, -114.78}, {42.00, -114.65},
    // North border (straight across)
    {42.00, -115.50}, {42.00, -117.00}, {42.00, -118.50}, {42.00, -120.00},
    {42.00, -121.50}, {42.00, -123.00}, {42.00, -124.21}
  };
  
  int points = sizeof(california) / sizeof(california[0]);
  
  // Draw outline
  for (int i = 0; i < points - 1; i++) {
    int x1 = mapLonToScreen(california[i][1]);
    int y1 = mapLatToScreen(california[i][0]);
    int x2 = mapLonToScreen(california[i+1][1]);
    int y2 = mapLatToScreen(california[i+1][0]);
    tft.drawLine(x1, y1, x2, y2, currentTheme.mapOutline);
  }
  
  // San Andreas Fault (detailed, accurate path)
  const float fault[][2] = {
    // North (Mendocino Triple Junction)
    {40.30, -124.30}, {39.80, -123.70}, {39.20, -123.30},
    // San Francisco area (curves inland)
    {38.50, -122.95}, {38.00, -122.55}, {37.70, -122.25},
    {37.40, -122.10}, {37.00, -121.85},
    // Central California (Parkfield area)
    {36.50, -121.20}, {36.00, -120.60}, {35.50, -120.10},
    {35.00, -119.60},
    // Big Bend area (curves northeast)
    {34.80, -119.20}, {34.60, -118.85}, {34.40, -118.55},
    // LA area (passes through)
    {34.20, -118.35}, {34.00, -118.20}, {33.80, -118.00},
    // Southeast to Salton Sea
    {33.60, -117.70}, {33.40, -117.35}, {33.20, -116.95},
    {33.00, -116.50}, {32.80, -116.10}, {32.60, -115.70}
  };
  
  int faultPoints = sizeof(fault) / sizeof(fault[0]);
  for (int i = 0; i < faultPoints - 1; i++) {
    int x1 = mapLonToScreen(fault[i][1]);
    int y1 = mapLatToScreen(fault[i][0]);
    int x2 = mapLonToScreen(fault[i+1][1]);
    int y2 = mapLatToScreen(fault[i+1][0]);
    tft.drawLine(x1, y1, x2, y2, TFT_RED);
  }
  
  // Major cities
  tft.fillCircle(mapLonToScreen(-122.42), mapLatToScreen(37.77), 2, currentTheme.mapCity); // SF
  tft.fillCircle(mapLonToScreen(-118.24), mapLatToScreen(34.05), 2, currentTheme.mapCity); // LA
  tft.fillCircle(mapLonToScreen(-117.16), mapLatToScreen(32.72), 2, currentTheme.mapCity); // SD
  tft.fillCircle(mapLonToScreen(-121.49), mapLatToScreen(38.58), 2, currentTheme.mapCity); // Sacramento
}

// ═══════════════════════════════════════════════════════════════════════════
// REGIONAL MAPS - GLOBAL
// ═══════════════════════════════════════════════════════════════════════════

void drawGlobalMap() {
  int centerX = MAP_X + MAP_WIDTH / 2;
  int centerY = MAP_Y + MAP_HEIGHT / 2;
  int radius = 65;
  
  // Globe outline
  tft.drawCircle(centerX, centerY, radius, currentTheme.mapOutline);
  
  // Equator
  tft.drawFastHLine(centerX - radius, centerY, radius * 2, currentTheme.seismoGrid);
  
  // Tropics
  int tropicOffset = radius * 0.4;
  tft.drawFastHLine(centerX - radius + 10, centerY - tropicOffset, radius * 2 - 20, currentTheme.seismoGrid);
  tft.drawFastHLine(centerX - radius + 10, centerY + tropicOffset, radius * 2 - 20, currentTheme.seismoGrid);
  
  // Meridians (every 60 degrees)
  for (int offset = -30; offset <= 30; offset += 30) {
    for (int y = centerY - radius; y <= centerY + radius; y += 2) {
      int dy = y - centerY;
      int maxDx = sqrt(radius * radius - dy * dy);
      int x = centerX + offset;
      if (abs(x - centerX) < maxDx) {
        tft.drawPixel(x, y, currentTheme.seismoGrid);
      }
    }
  }
  
  // Simplified continent outlines (stylized)
  // Americas (left side)
  const int americas[][2] = {
    {centerX - 35, centerY - 50}, {centerX - 30, centerY - 40},
    {centerX - 25, centerY - 30}, {centerX - 20, centerY - 20},
    {centerX - 25, centerY - 10}, {centerX - 30, centerY}, 
    {centerX - 25, centerY + 10}, {centerX - 20, centerY + 20},
    {centerX - 25, centerY + 30}, {centerX - 30, centerY + 40}
  };
  for (int i = 0; i < 9; i++) {
    tft.drawLine(americas[i][0], americas[i][1], americas[i+1][0], americas[i+1][1], currentTheme.mapOutline);
  }
  
  // Europe/Africa (center)
  const int eurAfrica[][2] = {
    {centerX + 5, centerY - 40}, {centerX + 10, centerY - 30},
    {centerX + 15, centerY - 20}, {centerX + 10, centerY - 10},
    {centerX + 15, centerY}, {centerX + 20, centerY + 10},
    {centerX + 15, centerY + 20}, {centerX + 10, centerY + 30},
    {centerX + 15, centerY + 40}
  };
  for (int i = 0; i < 8; i++) {
    tft.drawLine(eurAfrica[i][0], eurAfrica[i][1], eurAfrica[i+1][0], eurAfrica[i+1][1], currentTheme.mapOutline);
  }
  
  // Asia/Australia (right side)
  const int asiaPac[][2] = {
    {centerX + 25, centerY - 40}, {centerX + 35, centerY - 30},
    {centerX + 40, centerY - 20}, {centerX + 45, centerY - 10},
    {centerX + 40, centerY}, {centerX + 35, centerY + 10},
    {centerX + 40, centerY + 20}
  };
  for (int i = 0; i < 6; i++) {
    tft.drawLine(asiaPac[i][0], asiaPac[i][1], asiaPac[i+1][0], asiaPac[i+1][1], currentTheme.mapOutline);
  }
  
  // Australia dot
  tft.fillCircle(centerX + 35, centerY + 35, 3, currentTheme.mapOutline);
  
  // Pacific Ring of Fire (red arc)
  for (int angle = -45; angle <= 225; angle += 2) {
    float rad = angle * 0.0174533;
    int x = centerX + (radius - 8) * cos(rad);
    int y = centerY + (radius - 8) * sin(rad);
    
    // Only draw if within globe
    int dist = sqrt((x - centerX) * (x - centerX) + (y - centerY) * (y - centerY));
    if (dist < radius) {
      tft.drawPixel(x, y, TFT_RED);
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
  
  Serial.println("Fetching: " + String(apiURL));
  
  http.begin(apiURL);
  http.setTimeout(HTTP_TIMEOUT);
  
  int httpCode = http.GET();
  if (httpCode != 200) {
    Serial.println("HTTP error: " + String(httpCode));
    http.end();
    return;
  }
  
  String payload = http.getString();
  http.end();
  
  Serial.print("Payload size: ");
  Serial.print(payload.length());
  Serial.println(" bytes");
  
  // For USGS feeds, use streaming parser with filter to reduce memory
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
  DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  
  if (error) {
    Serial.println("JSON error");
    return;
  }
  
  JsonArray features = doc["features"];
  int totalFeatures = features.size();
  
  if (totalFeatures == 0) return;
  
  Serial.println("Features: " + String(totalFeatures));
  
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
    } else {
      highestRegionalQuake.latitude = h["geometry"]["coordinates"][1];
      highestRegionalQuake.longitude = h["geometry"]["coordinates"][0];
      highestRegionalQuake.depth = h["geometry"]["coordinates"][2];
      strncpy(highestRegionalQuake.location, h["properties"]["place"], sizeof(highestRegionalQuake.location) - 1);
    }
    
    highestRegionalQuake.timestamp = millis();
    highestRegionalQuake.isValid = true;
    
    Serial.println("Highest quake: M" + String(highestMag) + " at " + String(highestRegionalQuake.location));
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
      Serial.print("Found latest in-region quake at index: ");
      Serial.println(i);
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
      latestQuake.timestamp = millis();
      latestQuake.isValid = true;
      
      Serial.println("✓ Latest quake: M" + String(mag) + " - " + String(loc));
      
      if (isNewQuake && mag >= config.magThreshold) {
        Serial.println("ALERT!");
        displayEarthquakeAlert(&latestQuake);
      }
      
      if (needsInitialData || isNewQuake) {
        lastQuakeID = quakeID;
      }
      
      lastActivity = millis();
    }
  } else {
    Serial.println("No earthquakes in region for latest data");
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// EARTHQUAKE ALERT
// ═══════════════════════════════════════════════════════════════════════════

void displayEarthquakeAlert(EarthquakeData* quake) {
  showingAlert = true;
  alertStartTime = millis();
  
  tft.fillScreen(currentTheme.background);
  
  tft.setTextSize(2);
  tft.setTextColor(currentTheme.textPrimary);
  tft.setCursor(60, 40);
  tft.print("EARTHQUAKE");
  
  tft.setTextSize(4);
  tft.setTextColor(getMagnitudeColor(quake->magnitude));
  tft.setCursor(60, 80);
  tft.print("M");
  tft.print(quake->magnitude, 1);
  
  tft.setTextSize(2);
  tft.setTextColor(currentTheme.textPrimary);
  
  char* loc = quake->location;
  int len = strlen(loc);
  
  if (len > 20) {
    char line1[21];
    strncpy(line1, loc, 20);
    line1[20] = '\0';
    tft.setCursor(20, 140);
    tft.print(line1);
    
    if (len > 40) {
      char line2[21];
      strncpy(line2, loc + 20, 20);
      line2[20] = '\0';
      tft.setCursor(20, 165);
      tft.print(line2);
    } else {
      tft.setCursor(20, 165);
      tft.print(loc + 20);
    }
  } else {
    tft.setCursor(20, 140);
    tft.print(loc);
  }
  
  tft.setTextSize(1);
  tft.setTextColor(currentTheme.textSecondary);
  tft.setCursor(20, 205);
  tft.print("Depth: ");
  tft.print(quake->depth, 0);
  tft.print(" km");
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
  tft.setTextSize(2);
  tft.setTextColor(currentTheme.textPrimary);
  tft.setCursor(50, 80);
  tft.print("SETUP MODE");
  
  tft.setTextSize(1);
  tft.setTextColor(currentTheme.textSecondary);
  tft.setCursor(20, 115);
  tft.print("WiFi: SeisMonitor-Setup");
  tft.setCursor(20, 135);
  tft.print("URL: 192.168.4.1");
  
  setupWebServer();
}

void setupWebServer() {
  server.on("/", handleWebRoot);
  server.on("/save", HTTP_POST, handleWebSave);
  server.onNotFound(handleWebNotFound);
  server.begin();
  Serial.println("Web server started");
}

void handleWebRoot() {
  String html = R"(<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>SeisMonitor</title><style>*{margin:0;padding:0;box-sizing:border-box}body{font-family:-apple-system,sans-serif;background:#0a0a0a;color:#e8e8e8;padding:20px}h1{text-align:center;margin:20px 0;color:#C5A3;letter-spacing:2px}.container{max-width:500px;margin:0 auto;background:#1a1a1a;padding:30px;border:1px solid#333}label{display:block;margin:15px 0 5px;font-size:12px;text-transform:uppercase;color:#888}input,select{width:100%;padding:12px;background:#0f0f0f;border:1px solid#333;color:#fff;border-radius:5px;font-size:16px}button{width:100%;padding:15px;background:#C5A3;color:#000;border:none;border-radius:5px;font-size:14px;font-weight:600;margin-top:20px;cursor:pointer}button:hover{background:#d4b871}</style></head><body><h1>SEISMONITOR</h1><div class="container"><form action="/save" method="POST"><label>WiFi Network</label><input name="ssid" value=")";
  html += config.wifiSSID;
  html += R"(" required><label>WiFi Password</label><input type="password" name="password" value=")";
  html += config.wifiPassword;
  html += R"("><label>Region</label><select name="region"><option value="NZ")";
  if (strcmp(config.region, "NZ") == 0) html += " selected";
  html += R"(>New Zealand</option><option value="Japan")";
  if (strcmp(config.region, "Japan") == 0) html += " selected";
  html += R"(>Japan</option><option value="Taiwan")";
  if (strcmp(config.region, "Taiwan") == 0) html += " selected";
  html += R"(>Taiwan</option><option value="California")";
  if (strcmp(config.region, "California") == 0) html += " selected";
  html += R"(>California</option><option value="Global")";
  if (strcmp(config.region, "Global") == 0) html += " selected";
  html += R"(>Global</option></select><label>Font Size</label><select name="fontsize"><option value="1")";
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
  html += R"(>Monochrome</option></select><button type="submit">SAVE</button></form></div></body></html>)";
  
  server.send(200, "text/html", html);
}

void handleWebSave() {
  if (server.hasArg("ssid")) server.arg("ssid").toCharArray(config.wifiSSID, sizeof(config.wifiSSID));
  if (server.hasArg("password")) server.arg("password").toCharArray(config.wifiPassword, sizeof(config.wifiPassword));
  if (server.hasArg("region")) server.arg("region").toCharArray(config.region, sizeof(config.region));
  if (server.hasArg("fontsize")) config.fontSize = server.arg("fontsize").toInt();
  if (server.hasArg("aesthetic")) server.arg("aesthetic").toCharArray(config.aesthetic, sizeof(config.aesthetic));
  
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
