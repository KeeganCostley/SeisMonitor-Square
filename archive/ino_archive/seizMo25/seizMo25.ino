#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <FS.h>
using namespace fs;  // Fix for ESP32 core 3.x namespace issue
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <DNSServer.h>

TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

// WiFi credentials (loaded from preferences)
String wifi_ssid = "";
String wifi_password = "";

// Settings (loaded from preferences)
String selectedRegion = "NZ";  // Default: NZ, Global, Japan, Taiwan, California
float magnitudeThreshold = 2.0;  // Default threshold
int fontSize = 2;  // Font size: 1=Small, 2=Medium, 3=Large
String aestheticMode = "cyberpunk";  // Aesthetic: elegant, matrix, cyberpunk
bool isConfigMode = false;  // AP mode for first-time setup

// DNS server for captive portal
const byte DNS_PORT = 53;

// Aesthetic color palettes
struct ColorPalette {
  uint16_t border1;        // Left panel border
  uint16_t border2;        // Right panel border
  uint16_t divider;        // Center divider
  uint16_t dividerGlow;    // Divider glow
  uint16_t accent1;        // Primary accent (headers)
  uint16_t accent2;        // Secondary accent (data)
  uint16_t seismoLine;     // Seismograph line
  uint16_t seismoGlow;     // Seismograph glow trail
  uint16_t mapOutline;     // Map outline color
  uint16_t cityMarker;     // City marker color
  uint16_t gridDots;       // Grid overlay dots
  uint16_t textPrimary;    // Primary text
  uint16_t textSecondary;  // Secondary text
  uint16_t cornerAccent;   // Corner triangles
};

ColorPalette getColorPalette() {
  ColorPalette palette;
  
  if (aestheticMode == "elegant") {
    // ELEGANT: Soft blues, whites, minimal
    palette.border1 = 0x4E9F;        // Soft blue
    palette.border2 = 0x6D5F;        // Soft purple-blue
    palette.divider = 0xC618;        // Light grey
    palette.dividerGlow = 0x8410;    // Medium grey
    palette.accent1 = 0xFFFF;        // White
    palette.accent2 = 0xC618;        // Light grey
    palette.seismoLine = 0x4E9F;     // Soft blue
    palette.seismoGlow = 0x2945;     // Dim blue
    palette.mapOutline = 0xC618;     // Light grey
    palette.cityMarker = 0x4E9F;     // Soft blue
    palette.gridDots = 0x2104;       // Very dim grey
    palette.textPrimary = 0xFFFF;    // White
    palette.textSecondary = 0x8410;  // Medium grey
    palette.cornerAccent = 0x6D5F;   // Soft purple-blue
    
  } else if (aestheticMode == "matrix") {
    // MATRIX: Pure green on black, hacker aesthetic
    palette.border1 = 0x07E0;        // Bright green
    palette.border2 = 0x07E0;        // Bright green
    palette.divider = 0x07E0;        // Bright green
    palette.dividerGlow = 0x0300;    // Dim green
    palette.accent1 = 0x07E0;        // Bright green
    palette.accent2 = 0x0300;        // Dim green
    palette.seismoLine = 0x07E0;     // Bright green
    palette.seismoGlow = 0x0300;     // Dim green
    palette.mapOutline = 0x0300;     // Dim green
    palette.cityMarker = 0x07E0;     // Bright green
    palette.gridDots = 0x0200;       // Very dim green
    palette.textPrimary = 0x07E0;    // Bright green
    palette.textSecondary = 0x0300;  // Dim green
    palette.cornerAccent = 0x07E0;   // Bright green
    
  } else {  // cyberpunk (default)
    // CYBERPUNK: Neon cyan/magenta, high contrast
    palette.border1 = 0x039F;        // Electric blue
    palette.border2 = 0xF81F;        // Neon magenta
    palette.divider = 0x07FF;        // Neon cyan
    palette.dividerGlow = 0x0410;    // Dim cyan
    palette.accent1 = 0x07FF;        // Neon cyan
    palette.accent2 = 0xF81F;        // Neon magenta
    palette.seismoLine = 0x07E0;     // Neon green
    palette.seismoGlow = 0x0300;     // Dim green
    palette.mapOutline = 0x5AEB;     // Medium grey
    palette.cityMarker = 0x4E9F;     // Soft cyan
    palette.gridDots = 0x2945;       // Dark grey
    palette.textPrimary = 0xFFFF;    // White
    palette.textSecondary = 0x0410;  // Dim cyan
    palette.cornerAccent = 0x07FF;   // Neon cyan
  }
  
  return palette;
}

// API endpoints - dynamically selected based on region
String currentAPI = "";

// Region API endpoints
const char* nzAPI = "https://api.geonet.org.nz/quake?MMI=2";
const char* usgsAPI = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/2.5_day.geojson";  // For all non-NZ regions

// Hardware button pin
#define BUTTON_PIN 0

// Variables
unsigned long lastQuakeTime = 0;
bool showingQuake = false;
String lastQuakeID = "";
int seismographX = 5;
int lastY = 120;
bool isGlobalMode = false;
bool isRestMode = false;
unsigned long lastActivityTime = 0;
#define REST_MODE_TIMEOUT 45000  // 45 seconds

// Display cycling for Latest Activity section
int displayMode = 0;  // 0 = latest, 1 = highest NZ
unsigned long lastDisplaySwitch = 0;
#define DISPLAY_CYCLE_TIME 60000  // 1 minute per display

// Store highest magnitude quakes
struct QuakeData {
  float magnitude;
  String locality;
  int depth;
  String time;
  unsigned long timestamp; // When we received it
  float lat;
  float lon;
  bool hasData;
} lastQuake, highestNZ, highestGlobal;

// Store recent quakes for map (up to 20)
struct QuakeLocation {
  float lat;
  float lon;
  float magnitude;
  bool valid;
} recentQuakes[20];

int quakeCount = 0;

// Button debounce
unsigned long lastButtonPress = 0;
#define DEBOUNCE_DELAY 300

// Map boundaries (right side of screen)
#define MAP_X 170
#define MAP_Y 10
#define MAP_WIDTH 145
#define MAP_HEIGHT 220

// Seismograph boundaries (left side - centered vertically)
#define SEISMO_WIDTH 155
#define SEISMO_Y_START 20    // Start below the title
#define SEISMO_Y_END 120     // End above the "Latest Activity" section (moved up more)
#define SEISMO_CENTER ((SEISMO_Y_START + SEISMO_Y_END) / 2)  // Vertical center

// NZ bounding box
#define NZ_LAT_MIN -47.3
#define NZ_LAT_MAX -34.0
#define NZ_LON_MIN 166.0
#define NZ_LON_MAX 179.0

// Forward declarations
void drawSeismographScreen();
void animateSeismograph();
void checkForNewQuakes();
void displayQuakeAlert(float magnitude, const char* locality, int depth, const char* time, bool saveData);
void fadeToSeismograph();
void checkButton();
void drawDetailedNZMap();
void plotQuakeOnMap(float lat, float lon, float magnitude);
void plotLastQuakeOnMap();
void addQuakeToHistory(float lat, float lon, float magnitude);
int mapLat(float lat);
int mapLon(float lon);
void updateLatestActivity();
String getTimeAgo(unsigned long timestamp);
void loadSettings();
void saveSettings();
void startConfigPortal();
void setupWebServer();
void handleRoot();
void handleSave();
void handleNotFound();
String getAPI();
bool isInSelectedRegion(float lat, float lon);

void setup() {
  delay(100);  // Small delay helps with upload timing
  Serial.begin(115200);
  delay(1000);  // Give serial time to initialize
  Serial.println("\n\n=== SEISMONITOR STARTING ===");
  Serial.println("Initializing preferences...");
  
  // Initialize preferences
  preferences.begin("seismonitor", false);
  Serial.println("Preferences initialized!");
  
  // Load settings from memory
  Serial.println("Loading settings...");
  loadSettings();
  Serial.println("Settings loaded!");
  
  // Initialize quake history
  for (int i = 0; i < 20; i++) {
    recentQuakes[i].valid = false;
  }
  
  lastQuake.hasData = false;
  highestNZ.hasData = false;
  highestGlobal.hasData = false;
  Serial.println("Quake data structures initialized!");
  
  // Setup button
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.println("Button initialized!");
  
  // Initialize display
  Serial.println("Initializing display...");
  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(true);  // DARK MODE ALWAYS ON
  tft.fillScreen(TFT_BLACK);
  Serial.println("Display initialized!");
  
  // Check if we need to enter config mode (no WiFi credentials saved)
  Serial.print("WiFi SSID: '");
  Serial.print(wifi_ssid);
  Serial.println("'");
  
  if (wifi_ssid == "" || wifi_password == "") {
    Serial.println("No WiFi credentials - entering CONFIG MODE");
    isConfigMode = true;
    startConfigPortal();
    return;  // Stay in config mode until settings are saved
  }
  
  Serial.println("WiFi credentials found - connecting...");
  
  // Connect to WiFi
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Earthquake Monitor");
  tft.println("Connecting WiFi...");
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    // Failed to connect - enter config mode
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(10, 10);
    tft.println("WiFi Failed!");
    tft.println("Config Mode...");
    delay(2000);
    isConfigMode = true;
    startConfigPortal();
    return;
  }
  
  tft.println("WiFi Connected!");
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Start mDNS
  if (MDNS.begin("seismonitor")) {
    Serial.println("mDNS started: http://seismonitor.local");
    tft.setTextSize(1);
    tft.println("http://seismonitor.local");
  }
  
  // Setup web server
  setupWebServer();
  
  delay(5000);  // Show "WiFi Connected!" for 5 seconds
  
  // Already in dark mode from initialization
  isRestMode = true;
  
  // Initial quake check to populate last quake data
  checkForNewQuakes();
  
  // Initialize activity timer
  lastActivityTime = millis();
  lastDisplaySwitch = millis();
  
  // Start with seismograph display
  drawSeismographScreen();
}

void loop() {
  // Handle web server requests
  server.handleClient();
  
  // If in config mode, just run the server
  if (isConfigMode) {
    dnsServer.processNextRequest();
    delay(10);
    return;
  }
  
  checkButton();
  
  // Check for rest mode timeout (45 seconds of no activity)
  if (!showingQuake && !isRestMode && (millis() - lastActivityTime > REST_MODE_TIMEOUT)) {
    isRestMode = true;
    tft.invertDisplay(true);  // Invert colors
  }
  
  // Cycle through display modes every 60 seconds
  if (!showingQuake && (millis() - lastDisplaySwitch > DISPLAY_CYCLE_TIME)) {
    displayMode = (displayMode + 1) % 2;  // Cycle 0->1->0 (only 2 modes now)
    lastDisplaySwitch = millis();
    updateLatestActivity();
  }
  
  // Check for new earthquakes every 30 seconds
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 30000) {
    checkForNewQuakes();
    lastCheck = millis();
  }
  
  // Update "time ago" display every 60 seconds
  static unsigned long lastActivityUpdate = 0;
  if (!showingQuake && (millis() - lastActivityUpdate > 60000)) {
    updateLatestActivity();
    lastActivityUpdate = millis();
  }
  
  // If showing a quake alert, check if 30 seconds have passed
  if (showingQuake && (millis() - lastQuakeTime > 30000)) {
    showingQuake = false;
    fadeToSeismograph();
  }
  
  // Animate seismograph if not showing quake
  if (!showingQuake) {
    animateSeismograph();
    delay(50);
  }
}

void drawSeismographScreen() {
  tft.fillScreen(TFT_BLACK);
  
  // Get color palette based on aesthetic mode
  ColorPalette colors = getColorPalette();
  
  // === ANGULAR BORDER FRAMES ===
  tft.drawRect(0, 0, 160, 240, colors.border1);
  tft.drawRect(1, 1, 158, 238, colors.border1);
  tft.drawRect(161, 0, 159, 240, colors.border2);
  tft.drawRect(162, 1, 157, 238, colors.border2);
  
  // === CORNER ACCENTS (angular cuts) ===
  tft.fillTriangle(0, 0, 10, 0, 0, 10, colors.cornerAccent);
  tft.fillTriangle(320, 0, 310, 0, 320, 10, colors.cornerAccent);
  tft.fillTriangle(0, 240, 10, 240, 0, 230, colors.cornerAccent);
  tft.fillTriangle(320, 240, 310, 240, 320, 230, colors.cornerAccent);
  
  // === VERTICAL DIVIDER with GLOW ===
  tft.drawLine(160, 0, 160, 240, colors.divider);
  tft.drawLine(159, 0, 159, 240, colors.dividerGlow);
  tft.drawLine(161, 0, 161, 240, colors.dividerGlow);
  
  // === LEFT SIDE - SEISMOGRAPH ===
  tft.setTextSize(1);
  tft.setTextColor(colors.accent1);
  tft.setCursor(8, 8);
  tft.print("// SEISMO");
  
  // Draw horizontal grid lines (data overlay)
  for (int y = 40; y < 195; y += 20) {
    for (int x = 5; x < 155; x += 4) {
      tft.drawPixel(x, y, colors.gridDots);
    }
  }
  
  // Draw centerline with GLOW effect
  tft.drawLine(5, SEISMO_CENTER-1, 155, SEISMO_CENTER-1, colors.dividerGlow);
  tft.drawLine(5, SEISMO_CENTER, 155, SEISMO_CENTER, TFT_WHITE);
  tft.drawLine(5, SEISMO_CENTER+1, 155, SEISMO_CENTER+1, colors.dividerGlow);
  
  // === LATEST ACTIVITY BOX (angular corners) ===
  tft.drawRect(3, 125, 154, 112, colors.border2);
  // Corner cut effect
  tft.drawLine(3, 125, 10, 125, TFT_BLACK);
  tft.drawLine(3, 125, 3, 132, TFT_BLACK);
  tft.fillTriangle(3, 125, 10, 125, 3, 132, colors.cornerAccent);
  
  // === RIGHT SIDE - MAP ===
  tft.setTextColor(colors.accent2);
  tft.setCursor(MAP_X + 8, 8);
  tft.print("// ");
  if (isGlobalMode) {
    tft.print("GLOBAL");
  } else {
    tft.print(selectedRegion);
  }
  
  drawRegionalMap();  // Draw the appropriate map based on region
  
  // Plot recent quakes
  for (int i = 0; i < quakeCount; i++) {
    if (recentQuakes[i].valid) {
      plotQuakeOnMap(recentQuakes[i].lat, recentQuakes[i].lon, recentQuakes[i].magnitude);
    }
  }
  
  plotLastQuakeOnMap();
  updateLatestActivity();
  
  seismographX = 5;
  lastY = SEISMO_CENTER;
}

void updateLatestActivity() {
  // Clear the latest activity area
  tft.fillRect(4, 126, 152, 110, TFT_BLACK);
  
  // Get color palette
  ColorPalette colors = getColorPalette();
  
  // Choose which data to display based on mode
  QuakeData* displayQuake;
  String title;
  
  if (displayMode == 0) {
    displayQuake = &lastQuake;
    title = ">> LATEST_EVENT";
  } else {
    displayQuake = &highestNZ;
    title = ">> MAX_24H_NZ";
  }
  
  tft.setTextSize(1);
  tft.setTextColor(colors.accent1);
  tft.setCursor(8, 132);
  tft.println(title);
  
  if (displayQuake->hasData) {
    // Magnitude in BIG text
    tft.setTextSize(fontSize + 1);  // fontSize 1=size2, 2=size3, 3=size4
    tft.setTextColor(colors.accent2);
    tft.setCursor(10, 150);
    tft.print("M");
    tft.println(displayQuake->magnitude, 1);
    
    // Location text
    tft.setTextSize(fontSize);  // Use selected font size
    tft.setTextColor(colors.textPrimary);
    String loc = displayQuake->locality;
    
    // Split location into multiple lines
    int startPos = 0;
    int lineY = 178;
    int linesShown = 0;
    while (startPos < loc.length() && lineY < 215 && linesShown < 4) {
      String line;
      if (startPos + 18 < loc.length()) {
        int breakPos = loc.lastIndexOf(' ', startPos + 18);
        if (breakPos > startPos) {
          line = loc.substring(startPos, breakPos);
          startPos = breakPos + 1;
        } else {
          line = loc.substring(startPos, startPos + 18);
          startPos += 18;
        }
      } else {
        line = loc.substring(startPos);
        startPos = loc.length();
      }
      
      tft.setCursor(8, lineY);
      tft.print(">");
      tft.println(line);
      lineY += 9;
      linesShown++;
    }
    
    // Time ago
    tft.setTextSize(1);
    tft.setTextColor(colors.textSecondary);
    tft.setCursor(8, 220);
    String timeAgo = getTimeAgo(displayQuake->timestamp);
    tft.print("[");
    tft.print(timeAgo);
    tft.println(" AGO]");
    
  } else {
    tft.setTextColor(0x4208);  // Dark grey
    tft.setTextSize(1);
    tft.setCursor(8, 170);
    tft.println(">> NO_DATA");
    tft.setCursor(8, 185);
    tft.println(">> STANDBY");
  }
}

String getTimeAgo(unsigned long timestamp) {
  unsigned long elapsed = (millis() - timestamp) / 1000; // seconds
  
  if (elapsed < 60) {
    return String(elapsed) + "s";
  } else if (elapsed < 3600) {
    return String(elapsed / 60) + "m";
  } else if (elapsed < 86400) {
    return String(elapsed / 3600) + "h";
  } else {
    return String(elapsed / 86400) + "d";
  }
}

void animateSeismograph() {
  int centerY = SEISMO_CENTER;
  int maxAmplitude = 65;  // Increased from 45 to 65 for more dramatic movement
  
  // Get color palette
  ColorPalette colors = getColorPalette();
  
  // Random movement with occasional spikes
  int movement;
  if (random(100) < 5) {
    movement = random(-maxAmplitude, maxAmplitude);
  } else {
    movement = random(-20, 20);  // Increased from -12,12 to -20,20
  }
  
  int targetY = centerY + movement;
  int newY = (lastY * 3 + targetY) / 4;
  
  // Keep within bounds
  if (newY < SEISMO_Y_START) newY = SEISMO_Y_START;
  if (newY > SEISMO_Y_END) newY = SEISMO_Y_END;
  
  // Draw new line segment with GLOW
  tft.drawLine(seismographX, lastY, seismographX + 1, newY, colors.seismoLine);
  
  // Add glow trail (draw dimmer line behind)
  if (seismographX > 6) {
    tft.drawLine(seismographX-1, lastY, seismographX, newY, colors.seismoGlow);
  }
  
  // Erase ahead of the line
  int eraseX = (seismographX + 15) % SEISMO_WIDTH;
  if (eraseX < 5) eraseX = 5;
  tft.drawLine(eraseX, SEISMO_Y_START, eraseX, SEISMO_Y_END, TFT_BLACK);
  
  // Redraw grid dots on centerline
  if (eraseX % 4 == 0) {
    tft.drawPixel(eraseX, SEISMO_CENTER, colors.gridDots);
  }
  
  // Redraw white centerline pixel
  tft.drawPixel(eraseX, SEISMO_CENTER, TFT_WHITE);
  
  lastY = newY;
  seismographX++;
  if (seismographX >= 155) seismographX = 5;
}

// Helper function to check if coordinates are within selected region
bool isInSelectedRegion(float lat, float lon) {
  if (selectedRegion == "NZ") {
    return (lat >= NZ_LAT_MIN && lat <= NZ_LAT_MAX && 
            lon >= NZ_LON_MIN && lon <= NZ_LON_MAX);
  } else if (selectedRegion == "Japan") {
    return (lat >= JAPAN_LAT_MIN && lat <= JAPAN_LAT_MAX && 
            lon >= JAPAN_LON_MIN && lon <= JAPAN_LON_MAX);
  } else if (selectedRegion == "Taiwan") {
    return (lat >= TAIWAN_LAT_MIN && lat <= TAIWAN_LAT_MAX && 
            lon >= TAIWAN_LON_MIN && lon <= TAIWAN_LON_MAX);
  } else if (selectedRegion == "California") {
    return (lat >= CALIF_LAT_MIN && lat <= CALIF_LAT_MAX && 
            lon >= CALIF_LON_MIN && lon <= CALIF_LON_MAX);
  } else {  // Global - accept everything
    return true;
  }
}

void checkForNewQuakes() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    String apiToUse = getAPI();
    Serial.print("Fetching from: ");
    Serial.println(apiToUse);
    
    http.begin(apiToUse.c_str());
    int httpCode = http.GET();
    
    Serial.print("HTTP Response Code: ");
    Serial.println(httpCode);
    
    if (httpCode > 0) {
      String payload = http.getString();
      
      Serial.print("Payload size: ");
      Serial.println(payload.length());
      
      // Buffer sized for API responses (NZ can be quite large with many small quakes)
      DynamicJsonDocument doc(40960);  // 40KB - increased to handle busy seismic periods
      DeserializationError error = deserializeJson(doc, payload);
      
      if (error) {
        Serial.print("JSON parse error: ");
        Serial.println(error.c_str());
        http.end();
        return;
      }
      
      JsonArray features = doc["features"];
      Serial.print("Features found: ");
      Serial.println(features.size());
      
      if (features.size() == 0) {
        Serial.println("No earthquake data in API response!");
        http.end();
        return;
      }
      
      // Clear quake history
      quakeCount = 0;
      
      // Variables to track highest magnitude
      float highestMag = 0;
      int highestIndex = -1;
      unsigned long currentTime = millis();
      unsigned long twentyFourHours = 86400000;  // 24 hours in milliseconds
      
      // Determine if we're using NZ API or USGS API
      bool usingNZAPI = (selectedRegion == "NZ");
      
      if (features.size() > 0) {
        // Process up to 20 quakes for the map and find highest in last 24h
        int processedCount = 0;
        for (int i = 0; i < features.size() && processedCount < 20; i++) {
          JsonObject quake = features[i];
          
          float magnitude, lat, lon;
          
          if (usingNZAPI) {
            // NZ API format
            lat = quake["geometry"]["coordinates"][1];
            lon = quake["geometry"]["coordinates"][0];
            magnitude = quake["properties"]["magnitude"];
          } else {
            // USGS API format
            lon = quake["geometry"]["coordinates"][0];
            lat = quake["geometry"]["coordinates"][1];
            magnitude = quake["properties"]["mag"];
          }
          
          // Check if quake is in selected region
          if (!isInSelectedRegion(lat, lon)) {
            continue;  // Skip quakes outside region
          }
          
          // Add to history and track highest
          addQuakeToHistory(lat, lon, magnitude);
          processedCount++;
          
          // Track highest magnitude in this region
          if (magnitude > highestMag) {
            highestMag = magnitude;
            highestIndex = i;
          }
        }
        
        Serial.print("Quakes in region: ");
        Serial.println(processedCount);
        
        // Update the highest magnitude quake data for this region
        if (highestIndex >= 0) {
          JsonObject highestQuake = features[highestIndex];
          
          if (usingNZAPI) {
            // NZ API data format
            if (highestQuake["properties"]["magnitude"].is<float>() && 
                highestQuake["properties"]["locality"].is<const char*>()) {
              
              float mag = highestQuake["properties"]["magnitude"];
              Serial.print("Highest regional quake found: M");
              Serial.println(mag);
              
              if (!highestNZ.hasData || mag > highestNZ.magnitude) {
                highestNZ.magnitude = mag;
                highestNZ.locality = String(highestQuake["properties"]["locality"].as<const char*>());
                highestNZ.lat = highestQuake["geometry"]["coordinates"][1];
                highestNZ.lon = highestQuake["geometry"]["coordinates"][0];
                highestNZ.depth = highestQuake["properties"]["depth"];
                highestNZ.time = String(highestQuake["properties"]["time"].as<const char*>());
                highestNZ.timestamp = millis();
                highestNZ.hasData = true;
                Serial.println("Saved to highestNZ!");
              }
            }
          } else {
            // USGS API data format
            if (highestQuake["properties"]["mag"].is<float>() && 
                highestQuake["properties"]["place"].is<const char*>()) {
              
              float mag = highestQuake["properties"]["mag"];
              Serial.print("Highest regional quake found: M");
              Serial.println(mag);
              
              if (!highestNZ.hasData || mag > highestNZ.magnitude) {
                highestNZ.magnitude = mag;
                highestNZ.locality = String(highestQuake["properties"]["place"].as<const char*>());
                highestNZ.lon = highestQuake["geometry"]["coordinates"][0];
                highestNZ.lat = highestQuake["geometry"]["coordinates"][1];
                highestNZ.depth = highestQuake["geometry"]["coordinates"][2].is<float>() ? 
                                  highestQuake["geometry"]["coordinates"][2].as<float>() : 0;
                highestNZ.time = "recent";
                highestNZ.timestamp = millis();
                highestNZ.hasData = true;
                Serial.println("Saved to highestNZ!");
              }
            }
          }
        }
        
        // Check if the latest quake in our region is new
        JsonObject latestQuake = features[0];
        String quakeID;
        
        if (usingNZAPI) {
          quakeID = latestQuake["properties"]["publicID"].as<const char*>();
        } else {
          quakeID = latestQuake["id"].as<const char*>();
        }
        
        if (quakeID != lastQuakeID) {
          lastQuakeID = quakeID;
          
          // Reset activity timer - new quake is activity!
          lastActivityTime = millis();
          
          // Exit rest mode if we're in it
          if (isRestMode) {
            isRestMode = false;
            tft.invertDisplay(false);
          }
          
          float magnitude, lat, lon;
          const char* locality;
          int depth;
          const char* time;
          
          if (usingNZAPI) {
            // NZ API format
            magnitude = latestQuake["properties"]["magnitude"];
            locality = latestQuake["properties"]["locality"];
            lat = latestQuake["geometry"]["coordinates"][1];
            lon = latestQuake["geometry"]["coordinates"][0];
            depth = latestQuake["properties"]["depth"];
            time = latestQuake["properties"]["time"];
          } else {
            // USGS API format
            magnitude = latestQuake["properties"]["mag"];
            locality = latestQuake["properties"]["place"];
            lon = latestQuake["geometry"]["coordinates"][0];
            lat = latestQuake["geometry"]["coordinates"][1];
            depth = latestQuake["geometry"]["coordinates"][2];
            time = "recent";
          }
          
          // Check if this quake is in our selected region
          if (!isInSelectedRegion(lat, lon)) {
            Serial.println("Latest quake not in selected region, skipping alert");
            http.end();
            return;
          }
          
          // Save lat/lon to lastQuake before displaying alert
          lastQuake.lat = lat;
          lastQuake.lon = lon;
          
          displayQuakeAlert(magnitude, locality, depth, time, true);
          
          lastQuakeTime = millis();
          showingQuake = true;
        }
      }
    }
    http.end();
  }
}

void displayQuakeAlert(float magnitude, const char* locality, int depth, const char* time, bool saveData) {
  // Fast retro intro sequence with darker colors
  uint16_t darkColors[] = {
    0x4000,  // Dark red
    0x6300,  // Dark orange
    0x6340,  // Dark yellow
    0x0300,  // Dark green
    0x0318,  // Dark cyan
    0x0014   // Dark blue
  };
  
  // Quick flash through colors
  for (int i = 0; i < 6; i++) {
    tft.fillScreen(darkColors[i]);
    delay(40);  // 40ms per color = 240ms total (very fast!)
  }
  
  // Now show the alert
  tft.fillScreen(TFT_BLACK);
  
  // Save to last quake data
  if (saveData) {
    lastQuake.magnitude = magnitude;
    lastQuake.locality = String(locality);
    lastQuake.depth = depth;
    lastQuake.time = String(time);
    lastQuake.timestamp = millis();
    lastQuake.hasData = true;
    // Note: lat/lon are saved in checkForNewQuakes() before calling this function
  }
  
  // Color based on magnitude
  uint16_t alertColor;
  if (magnitude >= 6.0) {
    alertColor = TFT_RED;
  } else if (magnitude >= 5.0) {
    alertColor = TFT_ORANGE;
  } else if (magnitude >= 4.0) {
    alertColor = TFT_YELLOW;
  } else {
    alertColor = TFT_GREEN;
  }
  
  // Draw alert
  tft.setTextColor(alertColor);
  tft.setTextSize(3);
  tft.setCursor(10, 30);
  tft.println("EARTHQUAKE");
  
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(4);
  tft.setCursor(10, 70);
  tft.print("M ");
  tft.println(magnitude, 1);
  
  tft.setTextSize(2);
  tft.setCursor(10, 120);
  tft.println(locality);
  
  tft.setTextSize(1);
  tft.setCursor(10, 160);
  tft.print("Depth: ");
  tft.print(depth);
  tft.println(" km");
  
  tft.setCursor(10, 180);
  tft.print("Time: ");
  tft.println(time);
}

void fadeToSeismograph() {
  // Smooth fade to black after earthquake alert
  // Fade through progressively darker shades
  for (int brightness = 5; brightness >= 0; brightness--) {
    uint16_t greyShade;
    switch(brightness) {
      case 5: greyShade = 0x39E7; break;  // Light grey
      case 4: greyShade = 0x2945; break;  // Medium-light grey
      case 3: greyShade = 0x18C3; break;  // Medium grey
      case 2: greyShade = 0x0841; break;  // Dark grey
      case 1: greyShade = 0x0020; break;  // Very dark grey
      case 0: greyShade = TFT_BLACK; break;
    }
    tft.fillScreen(greyShade);
    delay(100);  // Hold each brightness level
  }
  
  // Hold on black for a moment
  delay(200);
  
  // Now draw the normal seismograph screen
  drawSeismographScreen();
  
  // Immediately enter rest mode (inverted screen)
  isRestMode = true;
  tft.invertDisplay(true);
  lastActivityTime = millis();  // Reset the timer so it stays dark
}

void checkButton() {
  if (digitalRead(BUTTON_PIN) == LOW && (millis() - lastButtonPress > DEBOUNCE_DELAY)) {
    lastButtonPress = millis();
    
    // Reset activity timer - button press is activity!
    lastActivityTime = millis();
    
    // Exit rest mode if we're in it
    if (isRestMode) {
      isRestMode = false;
      tft.invertDisplay(false);
    }
    
    // Toggle mode
    isGlobalMode = !isGlobalMode;
    
    // Redraw screen
    if (!showingQuake) {
      drawSeismographScreen();
      checkForNewQuakes(); // Fetch new data for the mode
    }
  }
}

void drawDetailedNZMap() {
  // Get color palette
  ColorPalette colors = getColorPalette();
  
  // HIGH-DETAIL NEW ZEALAND POLYGON
  // Stored as lat/lon pairs for accuracy
  
  // NORTH ISLAND - More detailed coastline
  const float northIsland[][2] = {
    // Far North - Cape Reinga area
    {-34.42, 172.68}, {-34.40, 173.05}, {-34.50, 173.18},
    // Northland East Coast
    {-35.10, 173.95}, {-35.32, 174.11}, {-35.68, 174.32},
    // Bay of Islands
    {-35.25, 174.08}, {-35.50, 174.15}, {-35.80, 174.35},
    // Auckland Region
    {-36.40, 174.52}, {-36.85, 174.76}, {-37.05, 174.87},
    // Coromandel Peninsula (sticks out east)
    {-36.82, 175.50}, {-37.03, 175.68}, {-37.20, 175.85},
    // Bay of Plenty
    {-37.50, 176.20}, {-37.70, 176.95}, {-37.92, 177.48},
    // East Cape (furthest east point)
    {-37.52, 178.03}, {-37.70, 178.35}, {-37.85, 178.55},
    // Hawke's Bay straight section
    {-38.50, 178.05}, {-38.92, 177.68}, {-39.30, 177.05},
    // Wairarapa Coast
    {-40.28, 176.25}, {-40.85, 175.56}, {-41.05, 175.38},
    // Wellington (southern tip)
    {-41.28, 174.78}, {-41.35, 174.82},
    // Cook Strait northern edge
    {-41.25, 174.50}, {-41.08, 174.05},
    // Wellington West Coast
    {-40.90, 174.65}, {-40.50, 174.88}, {-39.70, 174.90},
    // Taranaki (bulge west)
    {-39.28, 173.75}, {-39.05, 174.05}, {-38.65, 174.08},
    // Whanganui region
    {-39.93, 175.05}, {-40.35, 175.35},
    // Manawatu coast
    {-40.47, 175.40}, {-40.60, 175.20},
    // Back up west coast to Taranaki
    {-39.05, 174.20}, {-38.36, 174.55}, {-37.82, 174.75},
    // Auckland West Coast
    {-37.48, 174.52}, {-37.02, 174.48}, {-36.50, 174.32},
    // Northland West Coast
    {-35.92, 173.95}, {-35.48, 173.85}, {-35.05, 173.42},
    // Back to start
    {-34.85, 173.08}, {-34.60, 172.85}, {-34.42, 172.68}
  };
  
  // SOUTH ISLAND - Detailed coastline
  const float southIsland[][2] = {
    // Marlborough Sounds (top, very jagged)
    {-40.92, 173.95}, {-41.05, 174.02}, {-41.15, 174.25},
    {-41.08, 174.18}, {-41.22, 174.05}, {-41.12, 173.82},
    // Kaikoura (bump out on east coast)
    {-41.65, 174.12}, {-42.40, 173.68}, {-42.85, 173.12},
    // Banks Peninsula (Christchurch area bump)
    {-43.58, 172.68}, {-43.65, 172.95}, {-43.82, 173.05},
    {-43.75, 172.88}, {-43.88, 172.72},
    // Canterbury Coast (straight down)
    {-44.02, 171.78}, {-44.58, 171.22}, {-45.05, 170.82},
    // Otago Peninsula (Dunedin)
    {-45.82, 170.65}, {-45.88, 170.52}, {-45.78, 170.70},
    // Southland Coast
    {-46.05, 170.28}, {-46.42, 169.75}, {-46.58, 168.95},
    {-46.65, 168.12}, {-46.60, 167.92},
    // Fiordland (very jagged west coast)
    {-46.48, 167.72}, {-46.18, 167.48}, {-45.92, 167.25},
    {-45.68, 167.18}, {-45.42, 167.05}, {-45.18, 166.98},
    {-44.88, 167.05}, {-44.55, 167.48}, {-44.15, 167.92},
    // West Coast (straight up)
    {-43.72, 168.38}, {-43.25, 169.05}, {-42.85, 169.48},
    {-42.45, 170.95}, {-41.95, 171.48}, {-41.52, 172.05},
    // Tasman / Golden Bay
    {-41.28, 172.65}, {-41.05, 172.88}, {-40.92, 173.52},
    // Back to Marlborough Sounds
    {-40.92, 173.95}
  };
  
  int northPoints = sizeof(northIsland) / sizeof(northIsland[0]);
  int southPoints = sizeof(southIsland) / sizeof(southIsland[0]);
  
  // Draw North Island
  for (int i = 0; i < northPoints - 1; i++) {
    int x1 = mapLon(northIsland[i][1]);
    int y1 = mapLat(northIsland[i][0]);
    int x2 = mapLon(northIsland[i+1][1]);
    int y2 = mapLat(northIsland[i+1][0]);
    tft.drawLine(x1, y1, x2, y2, colors.mapOutline);
  }
  
  // Draw South Island
  for (int i = 0; i < southPoints - 1; i++) {
    int x1 = mapLon(southIsland[i][1]);
    int y1 = mapLat(southIsland[i][0]);
    int x2 = mapLon(southIsland[i+1][1]);
    int y2 = mapLat(southIsland[i+1][0]);
    tft.drawLine(x1, y1, x2, y2, colors.mapOutline);
  }
  
  // Mark major cities
  // Wellington
  int wellyX = mapLon(174.78);
  int wellyY = mapLat(-41.28);
  tft.fillCircle(wellyX, wellyY, 2, colors.cityMarker);
  
  // Auckland
  int auckX = mapLon(174.76);
  int auckY = mapLat(-36.85);
  tft.fillCircle(auckX, auckY, 2, colors.cityMarker);
  
  // Christchurch
  int chcX = mapLon(172.64);
  int chcY = mapLat(-43.53);
  tft.fillCircle(chcX, chcY, 2, colors.cityMarker);
}

// ========== REGIONAL MAP DISPATCHER ==========
void drawRegionalMap() {
  if (selectedRegion == "NZ") {
    drawDetailedNZMap();
  } else if (selectedRegion == "Japan") {
    drawJapanMap();
  } else if (selectedRegion == "Taiwan") {
    drawTaiwanMap();
  } else if (selectedRegion == "California") {
    drawCaliforniaMap();
  } else {  // Global
    drawGlobalMap();
  }
}

// ========== JAPAN MAP ==========
void drawJapanMap() {
  ColorPalette colors = getColorPalette();
  
  // Japan main islands polygon
  const float japan[][2] = {
    // Hokkaido (northern island)
    {45.52, 141.35}, {45.48, 142.05}, {45.32, 142.88}, {44.98, 143.55},
    {44.52, 144.68}, {43.98, 145.48}, {43.42, 145.92}, {42.88, 145.58},
    {42.35, 144.88}, {41.95, 143.88}, {41.58, 142.35}, {41.48, 141.08},
    {41.88, 140.35}, {42.58, 140.05}, {43.42, 140.52}, {44.25, 140.88},
    {45.05, 141.25}, {45.52, 141.35},
    
    // Honshu (main island) - simplified outline  
    {41.52, 140.88}, {40.88, 140.25}, {39.88, 139.82}, {38.92, 139.62},
    {37.88, 138.92}, {36.88, 137.95}, {35.92, 137.35}, {35.42, 138.25},
    {35.15, 139.45}, {35.05, 139.78}, {34.92, 139.95}, {34.72, 139.78},
    {34.42, 138.88}, {34.12, 137.58}, {33.88, 135.88}, {33.92, 134.52},
    {34.15, 133.35}, {34.52, 132.88}, {35.15, 132.52}, {35.92, 133.05},
    {36.72, 133.88}, {37.58, 135.22}, {38.42, 136.88}, {39.35, 138.52},
    {40.25, 139.88}, {41.05, 140.72}, {41.52, 140.88},
    
    // Kyushu (southern island)
    {33.88, 131.12}, {33.58, 131.52}, {33.25, 131.88}, {32.88, 131.92},
    {32.52, 131.58}, {32.25, 130.92}, {32.35, 130.32}, {32.88, 130.12},
    {33.42, 130.28}, {33.82, 130.72}, {33.88, 131.12}
  };
  
  int points = sizeof(japan) / sizeof(japan[0]);
  
  // Draw Japan outline
  for (int i = 0; i < points - 1; i++) {
    int x1 = mapLon(japan[i][1]);
    int y1 = mapLat(japan[i][0]);
    int x2 = mapLon(japan[i+1][1]);
    int y2 = mapLat(japan[i+1][0]);
    
    // Only draw if both points are on screen
    if (x1 >= MAP_X && x1 <= MAP_X + MAP_WIDTH && y1 >= MAP_Y && y1 <= MAP_Y + MAP_HEIGHT) {
      tft.drawLine(x1, y1, x2, y2, colors.mapOutline);
    }
  }
  
  // Major cities
  // Tokyo
  int tokyoX = mapLon(139.69);
  int tokyoY = mapLat(35.68);
  if (tokyoX >= MAP_X && tokyoX <= MAP_X + MAP_WIDTH) {
    tft.fillCircle(tokyoX, tokyoY, 2, colors.cityMarker);
  }
  
  // Osaka
  int osakaX = mapLon(135.50);
  int osakaY = mapLat(34.69);
  if (osakaX >= MAP_X && osakaX <= MAP_X + MAP_WIDTH) {
    tft.fillCircle(osakaX, osakaY, 2, colors.cityMarker);
  }
}

// ========== TAIWAN MAP ==========
void drawTaiwanMap() {
  ColorPalette colors = getColorPalette();
  
  // Taiwan island outline
  const float taiwan[][2] = {
    {25.30, 121.57}, {25.28, 121.88}, {25.18, 122.00},  // North tip
    {24.95, 121.95}, {24.68, 121.78}, {24.35, 121.52},  // East coast
    {23.95, 121.38}, {23.52, 121.32}, {23.08, 121.08},  // Southeast
    {22.68, 120.85}, {22.35, 120.62}, {21.92, 120.75},  // South tip
    {22.25, 120.38}, {22.68, 120.28}, {23.15, 120.18},  // West coast
    {23.58, 120.12}, {24.05, 120.22}, {24.52, 120.48},  // Northwest
    {24.95, 120.88}, {25.15, 121.15}, {25.30, 121.57}   // Back to north
  };
  
  int points = sizeof(taiwan) / sizeof(taiwan[0]);
  
  // Draw Taiwan outline
  for (int i = 0; i < points - 1; i++) {
    int x1 = mapLon(taiwan[i][1]);
    int y1 = mapLat(taiwan[i][0]);
    int x2 = mapLon(taiwan[i+1][1]);
    int y2 = mapLat(taiwan[i+1][0]);
    tft.drawLine(x1, y1, x2, y2, colors.mapOutline);
  }
  
  // Taipei
  int taipeiX = mapLon(121.56);
  int taipeiY = mapLat(25.03);
  tft.fillCircle(taipeiX, taipeiY, 2, colors.cityMarker);
  
  // Kaohsiung
  int kaohsiungX = mapLon(120.31);
  int kaohsiungY = mapLat(22.63);
  tft.fillCircle(kaohsiungX, kaohsiungY, 2, colors.cityMarker);
}

// ========== CALIFORNIA MAP ==========
void drawCaliforniaMap() {
  ColorPalette colors = getColorPalette();
  uint16_t faultColor = 0xF800;  // Red for San Andreas fault
  
  // California outline
  const float california[][2] = {
    // North coast
    {42.00, -124.21}, {41.75, -124.18}, {41.45, -124.08},
    {40.95, -124.35}, {40.58, -124.38}, {40.15, -124.28},
    {39.72, -123.82}, {39.25, -123.75}, {38.92, -123.52},
    // SF Bay Area
    {38.25, -123.08}, {37.88, -122.95}, {37.52, -122.48},
    {37.25, -122.15}, {36.95, -121.92}, {36.58, -121.88},
    // Central coast
    {36.15, -121.65}, {35.72, -121.28}, {35.35, -120.88},
    {34.95, -120.65}, {34.58, -120.45}, {34.25, -119.95},
    // LA area
    {34.05, -118.95}, {33.88, -118.35}, {33.55, -118.12},
    {33.25, -117.48}, {32.95, -117.25}, {32.55, -117.12},
    // Border
    {32.53, -117.12}, {32.72, -114.72},
    // East side (straight up)
    {33.00, -114.62}, {34.05, -114.15}, {35.00, -114.58},
    {36.00, -114.05}, {37.00, -114.42}, {38.00, -114.58},
    {39.00, -114.72}, {40.00, -114.95}, {41.00, -114.88},
    {42.00, -114.48},
    // North border (straight across)
    {42.00, -120.00}, {42.00, -124.21}
  };
  
  int points = sizeof(california) / sizeof(california[0]);
  
  // Draw California outline
  for (int i = 0; i < points - 1; i++) {
    int x1 = mapLon(california[i][1]);
    int y1 = mapLat(california[i][0]);
    int x2 = mapLon(california[i+1][1]);
    int y2 = mapLat(california[i+1][0]);
    tft.drawLine(x1, y1, x2, y2, colors.mapOutline);
  }
  
  // Draw San Andreas Fault (simplified)
  const float fault[][2] = {
    {36.00, -120.50}, {35.50, -120.00}, {35.00, -119.50},
    {34.50, -118.80}, {34.00, -118.30}, {33.70, -117.00}
  };
  
  int faultPoints = sizeof(fault) / sizeof(fault[0]);
  for (int i = 0; i < faultPoints - 1; i++) {
    int x1 = mapLon(fault[i][1]);
    int y1 = mapLat(fault[i][0]);
    int x2 = mapLon(fault[i+1][1]);
    int y2 = mapLat(fault[i+1][0]);
    tft.drawLine(x1, y1, x2, y2, faultColor);
  }
  
  // Major cities
  // San Francisco
  int sfX = mapLon(-122.42);
  int sfY = mapLat(37.77);
  tft.fillCircle(sfX, sfY, 2, colors.cityMarker);
  
  // Los Angeles
  int laX = mapLon(-118.24);
  int laY = mapLat(34.05);
  tft.fillCircle(laX, laY, 2, colors.cityMarker);
  
  // San Diego
  int sdX = mapLon(-117.16);
  int sdY = mapLat(32.72);
  tft.fillCircle(sdX, sdY, 2, colors.cityMarker);
}

// ========== GLOBAL MAP (Stylized Globe) ==========
void drawGlobalMap() {
  ColorPalette colors = getColorPalette();
  
  // Draw stylized globe circle
  int centerX = MAP_X + MAP_WIDTH / 2;
  int centerY = MAP_Y + MAP_HEIGHT / 2;
  int radius = 65;
  
  // Draw globe outline (circle)
  tft.drawCircle(centerX, centerY, radius, colors.mapOutline);
  tft.drawCircle(centerX, centerY, radius-1, colors.mapOutline);
  
  // Draw latitude lines (horizontal)
  for (int lat = -60; lat <= 60; lat += 30) {
    int y = mapLat(lat);
    // Calculate ellipse width at this latitude
    float latRad = lat * 0.0174533;
    int halfWidth = radius * cos(latRad);
    
    // Draw curved latitude line
    for (int x = centerX - halfWidth; x <= centerX + halfWidth; x += 2) {
      int dx = x - centerX;
      int dy = sqrt(radius * radius - dx * dx);
      int yTop = centerY - dy;
      int yBottom = centerY + dy;
      
      if (abs(y - yTop) < 2) tft.drawPixel(x, yTop, colors.gridDots);
      if (abs(y - yBottom) < 2) tft.drawPixel(x, yBottom, colors.gridDots);
    }
  }
  
  // Draw longitude lines (vertical) 
  for (int lon = -150; lon <= 150; lon += 60) {
    int x = mapLon(lon);
    
    // Draw curved longitude line
    for (int y = centerY - radius; y <= centerY + radius; y += 2) {
      int dy = y - centerY;
      int dx = sqrt(radius * radius - dy * dy);
      int xLeft = centerX - dx;
      int xRight = centerX + dx;
      
      if (abs(x - xLeft) < 2) tft.drawPixel(xLeft, y, colors.gridDots);
      if (abs(x - xRight) < 2) tft.drawPixel(xRight, y, colors.gridDots);
    }
  }
  
  // Draw simplified continents as shapes
  // Pacific Ring of Fire arc (stylized)
  uint16_t fireColor = 0xF800;  // Red
  float ringRadius = radius * 0.85;
  
  for (int angle = -30; angle <= 210; angle += 3) {
    float rad = angle * 0.0174533;
    int x = centerX + ringRadius * cos(rad);
    int y = centerY + ringRadius * sin(rad);
    tft.drawPixel(x, y, fireColor);
    tft.drawPixel(x+1, y, fireColor);
  }
}

void plotQuakeOnMap(float lat, float lon, float magnitude) {
  int x = mapLon(lon);
  int y = mapLat(lat);
  
  if (x < MAP_X || x > MAP_X + MAP_WIDTH || y < MAP_Y || y > MAP_Y + MAP_HEIGHT) {
    return;
  }
  
  uint16_t color;
  int radius;
  
  if (magnitude >= 6.0) {
    color = TFT_RED;
    radius = 4;
  } else if (magnitude >= 5.0) {
    color = TFT_ORANGE;
    radius = 3;
  } else if (magnitude >= 4.0) {
    color = TFT_YELLOW;
    radius = 2;
  } else {
    color = TFT_GREEN;
    radius = 1;
  }
  
  tft.fillCircle(x, y, radius, color);
}

void addQuakeToHistory(float lat, float lon, float magnitude) {
  if (quakeCount < 20) {
    recentQuakes[quakeCount].lat = lat;
    recentQuakes[quakeCount].lon = lon;
    recentQuakes[quakeCount].magnitude = magnitude;
    recentQuakes[quakeCount].valid = true;
    quakeCount++;
  }
}

int mapLat(float lat) {
  return MAP_Y + (int)((NZ_LAT_MAX - lat) / (NZ_LAT_MAX - NZ_LAT_MIN) * MAP_HEIGHT);
}

int mapLon(float lon) {
  return MAP_X + (int)((lon - NZ_LON_MIN) / (NZ_LON_MAX - NZ_LON_MIN) * MAP_WIDTH);
}

// ========== SETTINGS SYSTEM ==========

void loadSettings() {
  wifi_ssid = preferences.getString("wifi_ssid", "");
  wifi_password = preferences.getString("wifi_pass", "");
  selectedRegion = preferences.getString("region", "NZ");
  magnitudeThreshold = preferences.getFloat("mag_thresh", 2.0);
  fontSize = preferences.getInt("font_size", 2);
  aestheticMode = preferences.getString("aesthetic", "cyberpunk");
  
  Serial.println("Settings loaded:");
  Serial.println("SSID: " + wifi_ssid);
  Serial.println("Region: " + selectedRegion);
  Serial.println("Threshold: " + String(magnitudeThreshold));
  Serial.println("Font Size: " + String(fontSize));
  Serial.println("Aesthetic: " + aestheticMode);
}

void saveSettings() {
  preferences.putString("wifi_ssid", wifi_ssid);
  preferences.putString("wifi_pass", wifi_password);
  preferences.putString("region", selectedRegion);
  preferences.putFloat("mag_thresh", magnitudeThreshold);
  preferences.putInt("font_size", fontSize);
  preferences.putString("aesthetic", aestheticMode);
  
  Serial.println("Settings saved!");
}

String getAPI() {
  if (selectedRegion == "NZ") {
    return String(nzAPI);
  } else {
    // All other regions use USGS and filter by coordinates
    return String(usgsAPI);
  }
}

void startConfigPortal() {
  Serial.println("Starting Config Portal...");
  
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(2);
  tft.setCursor(10, 30);
  tft.println("SETUP MODE");
  
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 70);
  tft.println("1. Connect to WiFi:");
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(10, 85);
  tft.println("   SeisMonitor-Setup");
  
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 105);
  tft.println("2. Open browser:");
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(10, 120);
  tft.println("   192.168.4.1");
  
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 140);
  tft.println("3. Scan QR code:");
  
  // Draw simple QR placeholder
  tft.fillRect(60, 155, 60, 60, TFT_WHITE);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(75, 180);
  tft.println("QR CODE");
  
  // Start AP mode
  WiFi.mode(WIFI_AP);
  WiFi.softAP("SeisMonitor-Setup");
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  
  // Start DNS server for captive portal
  dnsServer.start(DNS_PORT, "*", IP);
  
  // Setup web server
  setupWebServer();
  
  isConfigMode = true;
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("Web server started");
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>SeisMonitor Settings</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Courier New', monospace;
            background: #0a0a0a;
            min-height: 100vh;
            padding: 0;
            overflow-x: hidden;
        }
        .mountain-banner {
            width: 100%;
            height: 200px;
            background: linear-gradient(to bottom, #1a1a2e 0%, #16213e 50%, #0f3460 100%);
            position: relative;
            overflow: hidden;
            margin-bottom: -50px;
        }
        .mountain {
            position: absolute;
            bottom: 0;
            width: 0;
            height: 0;
            border-style: solid;
        }
        .mountain1 {
            border-width: 0 200px 150px 200px;
            border-color: transparent transparent #1a1a2e transparent;
            left: -50px;
        }
        .mountain2 {
            border-width: 0 180px 120px 180px;
            border-color: transparent transparent #2a2a3e transparent;
            left: 150px;
        }
        .mountain3 {
            border-width: 0 220px 180px 220px;
            border-color: transparent transparent #16213e transparent;
            right: -80px;
        }
        .stars {
            position: absolute;
            width: 100%;
            height: 100%;
        }
        .star {
            position: absolute;
            width: 2px;
            height: 2px;
            background: white;
            border-radius: 50%;
        }
        .container {
            max-width: 500px;
            margin: 0 auto;
            background: #1a1a2e;
            border-radius: 15px;
            padding: 40px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.8);
            border: 1px solid #0ff;
            position: relative;
            z-index: 10;
        }
        h1 {
            color: #0ff;
            text-align: center;
            margin-bottom: 5px;
            font-size: 32px;
            text-shadow: 0 0 10px #0ff;
            letter-spacing: 3px;
        }
        .subtitle {
            text-align: center;
            color: #888;
            margin-bottom: 30px;
            font-size: 12px;
            text-transform: uppercase;
            letter-spacing: 2px;
        }
        .form-group {
            margin-bottom: 25px;
        }
        label {
            display: block;
            color: #0ff;
            font-weight: bold;
            margin-bottom: 8px;
            font-size: 12px;
            text-transform: uppercase;
            letter-spacing: 1px;
        }
        input, select {
            width: 100%;
            padding: 12px 15px;
            background: #0f0f1e;
            border: 1px solid #0ff;
            border-radius: 5px;
            font-size: 16px;
            color: #fff;
            font-family: 'Courier New', monospace;
            transition: all 0.3s;
        }
        input:focus, select:focus {
            outline: none;
            border-color: #f0f;
            box-shadow: 0 0 10px rgba(0,255,255,0.3);
        }
        .slider-container {
            margin-top: 10px;
        }
        input[type="range"] {
            width: 100%;
            height: 6px;
            border-radius: 5px;
            background: #0f0f1e;
            outline: none;
            border: 1px solid #0ff;
        }
        input[type="range"]::-webkit-slider-thumb {
            width: 20px;
            height: 20px;
            border-radius: 50%;
            background: #0ff;
            cursor: pointer;
            box-shadow: 0 0 10px #0ff;
        }
        .threshold-value {
            text-align: center;
            font-size: 28px;
            color: #f0f;
            font-weight: bold;
            margin-top: 10px;
            text-shadow: 0 0 10px #f0f;
        }
        button {
            width: 100%;
            padding: 15px;
            background: linear-gradient(135deg, #0ff 0%, #f0f 100%);
            color: #000;
            border: none;
            border-radius: 5px;
            font-size: 18px;
            font-weight: bold;
            cursor: pointer;
            transition: transform 0.2s;
            text-transform: uppercase;
            letter-spacing: 2px;
            font-family: 'Courier New', monospace;
        }
        button:hover {
            transform: translateY(-2px);
            box-shadow: 0 5px 20px rgba(0,255,255,0.5);
        }
        button:active {
            transform: translateY(0);
        }
        .region-grid {
            display: grid;
            grid-template-columns: repeat(2, 1fr);
            gap: 10px;
            margin-top: 10px;
        }
        .region-option {
            padding: 12px;
            border: 1px solid #0ff;
            border-radius: 5px;
            text-align: center;
            cursor: pointer;
            transition: all 0.3s;
            background: #0f0f1e;
            color: #fff;
            font-family: 'Courier New', monospace;
        }
        .region-option:hover {
            border-color: #f0f;
            background: #1a1a2e;
            box-shadow: 0 0 15px rgba(255,0,255,0.3);
        }
        .region-option.selected {
            border-color: #f0f;
            background: linear-gradient(135deg, #0ff 0%, #f0f 100%);
            color: #000;
            box-shadow: 0 0 20px rgba(255,0,255,0.5);
        }
        input[type="radio"] {
            display: none;
        }
    </style>
    <script>
        // Generate random stars
        window.onload = function() {
            const starsContainer = document.querySelector('.stars');
            for(let i = 0; i < 50; i++) {
                const star = document.createElement('div');
                star.className = 'star';
                star.style.left = Math.random() * 100 + '%';
                star.style.top = Math.random() * 80 + '%';
                star.style.opacity = Math.random();
                starsContainer.appendChild(star);
            }
        }
    </script>
</head>
<body>
    <div class="mountain-banner">
        <div class="stars"></div>
        <div class="mountain mountain1"></div>
        <div class="mountain mountain2"></div>
        <div class="mountain mountain3"></div>
    </div>
    
    <div class="container">
        <h1>SEISMONITOR</h1>
        <p class="subtitle">Earthquake Monitoring System</p>
        }
        .slider-container {
            margin-top: 10px;
        }
        input[type="range"] {
            width: 100%;
            height: 6px;
            border-radius: 5px;
            background: #e0e0e0;
            outline: none;
        }
        input[type="range"]::-webkit-slider-thumb {
            width: 20px;
            height: 20px;
            border-radius: 50%;
            background: #667eea;
            cursor: pointer;
        }
        .threshold-value {
            text-align: center;
            font-size: 24px;
            color: #667eea;
            font-weight: bold;
            margin-top: 10px;
        }
        button {
            width: 100%;
            padding: 15px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            border: none;
            border-radius: 10px;
            font-size: 18px;
            font-weight: bold;
            cursor: pointer;
            transition: transform 0.2s;
        }
        button:hover {
            transform: translateY(-2px);
        }
        button:active {
            transform: translateY(0);
        }
        .wifi-icon {
            text-align: center;
            font-size: 40px;
            margin-bottom: 20px;
        }
        .region-grid {
            display: grid;
            grid-template-columns: repeat(2, 1fr);
            gap: 10px;
            margin-top: 10px;
        }
        .region-option {
            padding: 15px;
            border: 2px solid #e0e0e0;
            border-radius: 10px;
            text-align: center;
            cursor: pointer;
            transition: all 0.3s;
        }
        .region-option:hover {
            border-color: #667eea;
            background: #f5f7ff;
        }
        .region-option.selected {
            border-color: #667eea;
            background: #667eea;
            color: white;
        }
        input[type="radio"] {
            display: none;
        }
    </style>
    <script>
        // Generate random stars
        window.onload = function() {
            const starsContainer = document.querySelector('.stars');
            for(let i = 0; i < 50; i++) {
                const star = document.createElement('div');
                star.className = 'star';
                star.style.left = Math.random() * 100 + '%';
                star.style.top = Math.random() * 80 + '%';
                star.style.opacity = Math.random();
                starsContainer.appendChild(star);
            }
        }
    </script>
</head>
<body>
    <div class="mountain-banner">
        <div class="stars"></div>
        <div class="mountain mountain1"></div>
        <div class="mountain mountain2"></div>
        <div class="mountain mountain3"></div>
    </div>
    
    <div class="container">
        <h1>SEISMONITOR</h1>
        <p class="subtitle">Earthquake Monitoring System</p>
        
        <form action="/save" method="POST">
            <div class="form-group">
                <label>WiFi Network</label>
                <input type="text" name="ssid" placeholder="Your WiFi Name" value=")rawliteral" + wifi_ssid + R"rawliteral(" required>
            </div>
            
            <div class="form-group">
                <label>WiFi Password</label>
                <input type="password" name="password" placeholder="Your WiFi Password" value=")rawliteral" + wifi_password + R"rawliteral(">
            </div>
            
            <div class="form-group">
                <label>Region</label>
                <div class="region-grid">
                    <label class="region-option )rawliteral" + String(selectedRegion == "NZ" ? "selected" : "") + R"rawliteral(">
                        <input type="radio" name="region" value="NZ" )rawliteral" + String(selectedRegion == "NZ" ? "checked" : "") + R"rawliteral(>
                        NEW ZEALAND
                    </label>
                    <label class="region-option )rawliteral" + String(selectedRegion == "Global" ? "selected" : "") + R"rawliteral(">
                        <input type="radio" name="region" value="Global" )rawliteral" + String(selectedRegion == "Global" ? "checked" : "") + R"rawliteral(>
                        GLOBAL
                    </label>
                    <label class="region-option )rawliteral" + String(selectedRegion == "Japan" ? "selected" : "") + R"rawliteral(">
                        <input type="radio" name="region" value="Japan" )rawliteral" + String(selectedRegion == "Japan" ? "checked" : "") + R"rawliteral(>
                        JAPAN
                    </label>
                    <label class="region-option )rawliteral" + String(selectedRegion == "Taiwan" ? "selected" : "") + R"rawliteral(">
                        <input type="radio" name="region" value="Taiwan" )rawliteral" + String(selectedRegion == "Taiwan" ? "checked" : "") + R"rawliteral(>
                        TAIWAN
                    </label>
                    <label class="region-option )rawliteral" + String(selectedRegion == "California" ? "selected" : "") + R"rawliteral(">
                        <input type="radio" name="region" value="California" )rawliteral" + String(selectedRegion == "California" ? "checked" : "") + R"rawliteral(>
                        CALIFORNIA
                    </label>
                </div>
            </div>
            
            <div class="form-group">
                <label>Magnitude Threshold</label>
                <div class="slider-container">
                    <input type="range" name="threshold" min="1" max="6" step="0.1" value=")rawliteral" + String(magnitudeThreshold) + R"rawliteral(" oninput="updateThreshold(this.value)">
                    <div class="threshold-value">M <span id="thresholdValue">)rawliteral" + String(magnitudeThreshold, 1) + R"rawliteral(</span></div>
                </div>
            </div>
            
            <div class="form-group">
                <label>Display Font Size</label>
                <select name="fontsize">
                    <option value="1" )rawliteral" + String(fontSize == 1 ? "selected" : "") + R"rawliteral(>Small</option>
                    <option value="2" )rawliteral" + String(fontSize == 2 ? "selected" : "") + R"rawliteral(>Medium</option>
                    <option value="3" )rawliteral" + String(fontSize == 3 ? "selected" : "") + R"rawliteral(>Large</option>
                </select>
            </div>
            
            <div class="form-group">
                <label>Visual Aesthetic</label>
                <select name="aesthetic">
                    <option value="elegant" )rawliteral" + String(aestheticMode == "elegant" ? "selected" : "") + R"rawliteral(>Elegant (Minimal Blue)</option>
                    <option value="matrix" )rawliteral" + String(aestheticMode == "matrix" ? "selected" : "") + R"rawliteral(>Matrix (Pure Green)</option>
                    <option value="cyberpunk" )rawliteral" + String(aestheticMode == "cyberpunk" ? "selected" : "") + R"rawliteral(>Cyberpunk (Neon)</option>
                </select>
            </div>
            
            <button type="submit">SAVE SETTINGS</button>
        </form>
    </div>
    
    <script>
        function updateThreshold(value) {
            document.getElementById('thresholdValue').textContent = parseFloat(value).toFixed(1);
        }
        
        // Handle region selection visual feedback
        document.querySelectorAll('.region-option').forEach(option => {
            option.addEventListener('click', function() {
                document.querySelectorAll('.region-option').forEach(o => o.classList.remove('selected'));
                this.classList.add('selected');
                this.querySelector('input[type="radio"]').checked = true;
            });
        });
    </script>
</body>
</html>
)rawliteral";
  
  server.send(200, "text/html", html);
}

void handleSave() {
  // Get form data
  if (server.hasArg("ssid")) wifi_ssid = server.arg("ssid");
  if (server.hasArg("password")) wifi_password = server.arg("password");
  if (server.hasArg("region")) selectedRegion = server.arg("region");
  if (server.hasArg("threshold")) magnitudeThreshold = server.arg("threshold").toFloat();
  if (server.hasArg("fontsize")) fontSize = server.arg("fontsize").toInt();
  if (server.hasArg("aesthetic")) aestheticMode = server.arg("aesthetic");
  
  // Save to preferences
  saveSettings();
  
  // Send success page
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <meta http-equiv="refresh" content="3;url=/">
    <title>Settings Saved</title>
    <style>
        body {
            font-family: 'Courier New', monospace;
            background: #0a0a0a;
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
            margin: 0;
        }
        .success {
            background: #1a1a2e;
            padding: 50px;
            border-radius: 15px;
            text-align: center;
            box-shadow: 0 20px 60px rgba(0,0,0,0.8);
            border: 1px solid #0ff;
        }
        .checkmark {
            font-size: 60px;
            color: #0ff;
            margin-bottom: 20px;
            text-shadow: 0 0 20px #0ff;
        }
        h1 { 
            color: #0ff; 
            margin-bottom: 10px;
            text-shadow: 0 0 10px #0ff;
            letter-spacing: 2px;
        }
        p { 
            color: #888;
            text-transform: uppercase;
            letter-spacing: 1px;
            font-size: 12px;
        }
    </style>
</head>
<body>
    <div class="success">
        <div class="checkmark">[ OK ]</div>
        <h1>SETTINGS SAVED</h1>
        <p>Restarting SeisMonitor...</p>
    </div>
</body>
</html>
)rawliteral";
  
  server.send(200, "text/html", html);
  
  delay(2000);
  ESP.restart();  // Restart to apply new settings
}

void handleNotFound() {
  // Redirect all unknown pages to root (captive portal behavior)
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void plotLastQuakeOnMap() {
  // Always plot the 24h highest NZ quake with an orange dot
  if (highestNZ.hasData) {
    int x24h = mapLon(highestNZ.lon);
    int y24h = mapLat(highestNZ.lat);
    
    // Check if it's within the map bounds
    if (x24h >= MAP_X && x24h <= MAP_X + MAP_WIDTH && y24h >= MAP_Y && y24h <= MAP_Y + MAP_HEIGHT) {
      // Draw orange dot for 24h high
      tft.fillCircle(x24h, y24h, 4, TFT_ORANGE);
      tft.drawCircle(x24h, y24h, 5, TFT_WHITE);
    }
  }
  
  // Plot the current display mode quake with a red dot
  QuakeData* displayQuake;
  
  if (displayMode == 0) {
    displayQuake = &lastQuake;
  } else {
    displayQuake = &highestNZ;
  }
  
  if (!displayQuake->hasData) {
    return;  // No quake to plot
  }
  
  int x = mapLon(displayQuake->lon);
  int y = mapLat(displayQuake->lat);
  
  // Check if it's within the map bounds
  if (x < MAP_X || x > MAP_X + MAP_WIDTH || y < MAP_Y || y > MAP_Y + MAP_HEIGHT) {
    return;  // Outside map area
  }
  
  // Draw a big red dot (radius 5) with white outline - this goes on top
  tft.fillCircle(x, y, 5, TFT_RED);
  tft.drawCircle(x, y, 6, TFT_WHITE);  // White outline for visibility
}
