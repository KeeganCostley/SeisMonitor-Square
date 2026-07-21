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
const int SEISMO_MAX_AMPLITUDE = 40;

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
// API ENDPOINTS
// ═══════════════════════════════════════════════════════════════════════════

const char* API_NZ = "https://api.geonet.org.nz/quake?MMI=2";
const char* API_USGS = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/2.5_day.geojson";

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
  return strcmp(region, "NZ") == 0 ? API_NZ : API_USGS;
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
  
  if (!quake->isValid) {
    tft.setCursor(ACTIVITY_X + 5, ACTIVITY_Y + 45);
    tft.print("NO DATA");
    return;
  }
  
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
  
  // Highlight displayed quake
  EarthquakeData* displayedQuake = (displayMode == 0) ? &latestQuake : &highestRegionalQuake;
  if (displayedQuake->isValid) {
    int x = mapLonToScreen(displayedQuake->longitude);
    int y = mapLatToScreen(displayedQuake->latitude);
    
    if (x >= MAP_X && x <= MAP_X + MAP_WIDTH && y >= MAP_Y && y <= MAP_Y + MAP_HEIGHT) {
      tft.drawCircle(x, y, 5, TFT_RED);
      tft.drawCircle(x, y, 4, TFT_RED);
    }
  }
}

void animateSeismograph() {
  int movement = (random(100) < 5) ? random(-SEISMO_MAX_AMPLITUDE, SEISMO_MAX_AMPLITUDE) : random(-15, 15);
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
    {-34.42, 172.68}, {-34.40, 173.05}, {-34.50, 173.18},
    {-35.10, 173.95}, {-35.32, 174.11}, {-35.68, 174.32},
    {-35.25, 174.08}, {-35.50, 174.15}, {-35.80, 174.35},
    {-36.40, 174.52}, {-36.85, 174.76}, {-37.05, 174.87},
    {-36.82, 175.50}, {-37.03, 175.68}, {-37.20, 175.85},
    {-37.50, 176.20}, {-37.70, 176.95}, {-37.92, 177.48},
    {-37.52, 178.03}, {-37.70, 178.35}, {-37.85, 178.55},
    {-38.50, 178.05}, {-38.92, 177.68}, {-39.30, 177.05},
    {-40.28, 176.25}, {-40.85, 175.56}, {-41.05, 175.38},
    {-41.28, 174.78}, {-41.35, 174.82}, {-41.25, 174.50},
    {-41.08, 174.05}, {-40.90, 174.65}, {-40.50, 174.88},
    {-39.70, 174.90}, {-39.28, 173.75}, {-39.05, 174.05},
    {-38.65, 174.08}, {-39.93, 175.05}, {-40.35, 175.35},
    {-40.47, 175.40}, {-40.60, 175.20}, {-39.05, 174.20},
    {-38.36, 174.55}, {-37.82, 174.75}, {-37.48, 174.52},
    {-37.02, 174.48}, {-36.50, 174.32}, {-35.92, 173.95},
    {-35.48, 173.85}, {-35.05, 173.42}, {-34.85, 173.08},
    {-34.60, 172.85}, {-34.42, 172.68}
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
  
  int northPoints = sizeof(northIsland) / sizeof(northIsland[0]);
  int southPoints = sizeof(southIsland) / sizeof(southIsland[0]);
  
  for (int i = 0; i < northPoints - 1; i++) {
    int x1 = mapLonToScreen(northIsland[i][1]);
    int y1 = mapLatToScreen(northIsland[i][0]);
    int x2 = mapLonToScreen(northIsland[i+1][1]);
    int y2 = mapLatToScreen(northIsland[i+1][0]);
    tft.drawLine(x1, y1, x2, y2, currentTheme.mapOutline);
  }
  
  for (int i = 0; i < southPoints - 1; i++) {
    int x1 = mapLonToScreen(southIsland[i][1]);
    int y1 = mapLatToScreen(southIsland[i][0]);
    int x2 = mapLonToScreen(southIsland[i+1][1]);
    int y2 = mapLatToScreen(southIsland[i+1][0]);
    tft.drawLine(x1, y1, x2, y2, currentTheme.mapOutline);
  }
  
  // Cities
  tft.fillCircle(mapLonToScreen(174.78), mapLatToScreen(-41.28), 1, currentTheme.mapCity); // Wellington
  tft.fillCircle(mapLonToScreen(174.76), mapLatToScreen(-36.85), 1, currentTheme.mapCity); // Auckland
  tft.fillCircle(mapLonToScreen(172.64), mapLatToScreen(-43.53), 1, currentTheme.mapCity); // Christchurch
}

// ═══════════════════════════════════════════════════════════════════════════
// REGIONAL MAPS - JAPAN
// ═══════════════════════════════════════════════════════════════════════════

void drawJapanMap() {
  // Hokkaido (northern island) - separate polygon
  const float hokkaido[][2] = {
    {45.52, 141.35}, {45.48, 142.05}, {45.32, 142.88}, {44.98, 143.55},
    {44.52, 144.68}, {43.98, 145.48}, {43.42, 145.92}, {42.88, 145.58},
    {42.35, 144.88}, {41.95, 143.88}, {41.58, 142.35}, {41.48, 141.08},
    {41.88, 140.35}, {42.58, 140.05}, {43.42, 140.52}, {44.25, 140.88},
    {45.05, 141.25}, {45.52, 141.35}
  };
  
  // Honshu (main island) - separate polygon
  const float honshu[][2] = {
    {41.52, 140.88}, {40.88, 140.25}, {39.88, 139.82}, {38.92, 139.62},
    {37.88, 138.92}, {36.88, 137.95}, {35.92, 137.35}, {35.42, 138.25},
    {35.15, 139.45}, {35.05, 139.78}, {34.92, 139.95}, {34.72, 139.78},
    {34.42, 138.88}, {34.12, 137.58}, {33.88, 135.88}, {33.92, 134.52},
    {34.15, 133.35}, {34.52, 132.88}, {35.15, 132.52}, {35.92, 133.05},
    {36.72, 133.88}, {37.58, 135.22}, {38.42, 136.88}, {39.35, 138.52},
    {40.25, 139.88}, {41.05, 140.72}, {41.52, 140.88}
  };
  
  // Kyushu (southern island) - separate polygon
  const float kyushu[][2] = {
    {33.88, 131.12}, {33.58, 131.52}, {33.25, 131.88}, {32.88, 131.92},
    {32.52, 131.58}, {32.25, 130.92}, {32.35, 130.32}, {32.88, 130.12},
    {33.42, 130.28}, {33.82, 130.72}, {33.88, 131.12}
  };
  
  // Draw Hokkaido
  int hokkaidoPoints = sizeof(hokkaido) / sizeof(hokkaido[0]);
  for (int i = 0; i < hokkaidoPoints - 1; i++) {
    int x1 = mapLonToScreen(hokkaido[i][1]);
    int y1 = mapLatToScreen(hokkaido[i][0]);
    int x2 = mapLonToScreen(hokkaido[i+1][1]);
    int y2 = mapLatToScreen(hokkaido[i+1][0]);
    tft.drawLine(x1, y1, x2, y2, currentTheme.mapOutline);
  }
  
  // Draw Honshu
  int honshuPoints = sizeof(honshu) / sizeof(honshu[0]);
  for (int i = 0; i < honshuPoints - 1; i++) {
    int x1 = mapLonToScreen(honshu[i][1]);
    int y1 = mapLatToScreen(honshu[i][0]);
    int x2 = mapLonToScreen(honshu[i+1][1]);
    int y2 = mapLatToScreen(honshu[i+1][0]);
    tft.drawLine(x1, y1, x2, y2, currentTheme.mapOutline);
  }
  
  // Draw Kyushu
  int kyushuPoints = sizeof(kyushu) / sizeof(kyushu[0]);
  for (int i = 0; i < kyushuPoints - 1; i++) {
    int x1 = mapLonToScreen(kyushu[i][1]);
    int y1 = mapLatToScreen(kyushu[i][0]);
    int x2 = mapLonToScreen(kyushu[i+1][1]);
    int y2 = mapLatToScreen(kyushu[i+1][0]);
    tft.drawLine(x1, y1, x2, y2, currentTheme.mapOutline);
  }
  
  // Cities
  tft.fillCircle(mapLonToScreen(139.69), mapLatToScreen(35.68), 2, currentTheme.mapCity); // Tokyo
  tft.fillCircle(mapLonToScreen(135.50), mapLatToScreen(34.69), 2, currentTheme.mapCity); // Osaka
}

// ═══════════════════════════════════════════════════════════════════════════
// REGIONAL MAPS - TAIWAN
// ═══════════════════════════════════════════════════════════════════════════

void drawTaiwanMap() {
  const float taiwan[][2] = {
    {25.30, 121.57}, {25.28, 121.88}, {25.18, 122.00},
    {24.95, 121.95}, {24.68, 121.78}, {24.35, 121.52},
    {23.95, 121.38}, {23.52, 121.32}, {23.08, 121.08},
    {22.68, 120.85}, {22.35, 120.62}, {21.92, 120.75},
    {22.25, 120.38}, {22.68, 120.28}, {23.15, 120.18},
    {23.58, 120.12}, {24.05, 120.22}, {24.52, 120.48},
    {24.95, 120.88}, {25.15, 121.15}, {25.30, 121.57}
  };
  
  int points = sizeof(taiwan) / sizeof(taiwan[0]);
  
  // Draw outline (thicker for visibility)
  for (int i = 0; i < points - 1; i++) {
    int x1 = mapLonToScreen(taiwan[i][1]);
    int y1 = mapLatToScreen(taiwan[i][0]);
    int x2 = mapLonToScreen(taiwan[i+1][1]);
    int y2 = mapLatToScreen(taiwan[i+1][0]);
    tft.drawLine(x1, y1, x2, y2, currentTheme.mapOutline);
    // Draw slightly offset for thickness
    tft.drawLine(x1+1, y1, x2+1, y2, currentTheme.mapOutline);
  }
  
  // Cities (larger markers)
  tft.fillCircle(mapLonToScreen(121.56), mapLatToScreen(25.03), 2, currentTheme.mapCity); // Taipei
  tft.fillCircle(mapLonToScreen(120.31), mapLatToScreen(22.63), 2, currentTheme.mapCity); // Kaohsiung
}

// ═══════════════════════════════════════════════════════════════════════════
// REGIONAL MAPS - CALIFORNIA
// ═══════════════════════════════════════════════════════════════════════════

void drawCaliforniaMap() {
  const float california[][2] = {
    {42.00, -124.21}, {41.75, -124.18}, {41.45, -124.08},
    {40.95, -124.35}, {40.58, -124.38}, {40.15, -124.28},
    {39.72, -123.82}, {39.25, -123.75}, {38.92, -123.52},
    {38.25, -123.08}, {37.88, -122.95}, {37.52, -122.48},
    {37.25, -122.15}, {36.95, -121.92}, {36.58, -121.88},
    {36.15, -121.65}, {35.72, -121.28}, {35.35, -120.88},
    {34.95, -120.65}, {34.58, -120.45}, {34.25, -119.95},
    {34.05, -118.95}, {33.88, -118.35}, {33.55, -118.12},
    {33.25, -117.48}, {32.95, -117.25}, {32.55, -117.12},
    {32.53, -117.12}, {32.72, -114.72},
    {33.00, -114.62}, {34.05, -114.15}, {35.00, -114.58},
    {36.00, -114.05}, {37.00, -114.42}, {38.00, -114.58},
    {39.00, -114.72}, {40.00, -114.95}, {41.00, -114.88},
    {42.00, -114.48}, {42.00, -120.00}, {42.00, -124.21}
  };
  
  int points = sizeof(california) / sizeof(california[0]);
  
  // Draw outline (double thickness)
  for (int i = 0; i < points - 1; i++) {
    int x1 = mapLonToScreen(california[i][1]);
    int y1 = mapLatToScreen(california[i][0]);
    int x2 = mapLonToScreen(california[i+1][1]);
    int y2 = mapLatToScreen(california[i+1][0]);
    tft.drawLine(x1, y1, x2, y2, currentTheme.mapOutline);
    tft.drawLine(x1+1, y1, x2+1, y2, currentTheme.mapOutline);
  }
  
  // San Andreas Fault (thicker red line)
  const float fault[][2] = {
    {36.00, -120.50}, {35.50, -120.00}, {35.00, -119.50},
    {34.50, -118.80}, {34.00, -118.30}, {33.70, -117.00}
  };
  
  int faultPoints = sizeof(fault) / sizeof(fault[0]);
  for (int i = 0; i < faultPoints - 1; i++) {
    int x1 = mapLonToScreen(fault[i][1]);
    int y1 = mapLatToScreen(fault[i][0]);
    int x2 = mapLonToScreen(fault[i+1][1]);
    int y2 = mapLatToScreen(fault[i+1][0]);
    tft.drawLine(x1, y1, x2, y2, TFT_RED);
    tft.drawLine(x1, y1+1, x2, y2+1, TFT_RED);
  }
  
  // Cities (larger markers)
  tft.fillCircle(mapLonToScreen(-122.42), mapLatToScreen(37.77), 2, currentTheme.mapCity); // SF
  tft.fillCircle(mapLonToScreen(-118.24), mapLatToScreen(34.05), 2, currentTheme.mapCity); // LA
  tft.fillCircle(mapLonToScreen(-117.16), mapLatToScreen(32.72), 2, currentTheme.mapCity); // SD
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
  
  DynamicJsonDocument doc(65536);  // 64KB buffer - USGS can be large!
  DeserializationError error = deserializeJson(doc, payload);
  
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
  }
  
  JsonObject latest = features[0];
  String quakeID;
  
  if (usingNZ) {
    quakeID = latest["properties"]["publicID"].as<String>();
  } else {
    quakeID = latest["id"].as<String>();
  }
  
  // Always process the latest quake (for initial boot or when lastQuakeID is empty)
  bool isNewQuake = (quakeID != lastQuakeID && quakeID.length() > 0);
  bool needsInitialData = (lastQuakeID.length() == 0);  // First fetch
  
  if (isNewQuake || needsInitialData) {
    if (isNewQuake) lastQuakeID = quakeID;
    
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
    
    if (isInRegion(lat, lon, config.region)) {
      latestQuake.magnitude = mag;
      latestQuake.latitude = lat;
      latestQuake.longitude = lon;
      latestQuake.depth = depth;
      strncpy(latestQuake.location, loc, sizeof(latestQuake.location) - 1);
      latestQuake.timestamp = millis();
      latestQuake.isValid = true;
      
      Serial.println("Latest quake: M" + String(mag));
      
      // Only show alert if it's truly a NEW quake (not initial data load)
      if (isNewQuake && mag >= config.magThreshold) {
        displayEarthquakeAlert(&latestQuake);
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
