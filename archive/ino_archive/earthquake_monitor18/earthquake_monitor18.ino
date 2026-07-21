#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
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
bool isConfigMode = false;  // AP mode for first-time setup

// DNS server for captive portal
const byte DNS_PORT = 53;

// API endpoints - dynamically selected based on region
String currentAPI = "";

// Region API endpoints
const char* nzAPI = "https://api.geonet.org.nz/quake?MMI=2";
const char* globalAPI = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/4.5_day.geojson";
const char* japanAPI = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/2.5_day.geojson";  // Japan region filter
const char* taiwanAPI = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/2.5_day.geojson";  // Taiwan region filter
const char* californiaAPI = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/2.5_day.geojson";  // California filter

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

void setup() {
  Serial.begin(115200);
  
  // Initialize preferences
  preferences.begin("seismonitor", false);
  
  // Load settings from memory
  loadSettings();
  
  // Initialize quake history
  for (int i = 0; i < 20; i++) {
    recentQuakes[i].valid = false;
  }
  
  lastQuake.hasData = false;
  highestNZ.hasData = false;
  highestGlobal.hasData = false;
  
  // Setup button
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // Initialize display
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  
  // Check if we need to enter config mode (no WiFi credentials saved)
  if (wifi_ssid == "" || wifi_password == "") {
    isConfigMode = true;
    startConfigPortal();
    return;  // Stay in config mode until settings are saved
  }
  
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
  
  // Invert to dark mode (this is now the standard display)
  tft.invertDisplay(true);
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
  
  // Draw vertical divider line
  tft.drawLine(160, 0, 160, 240, TFT_DARKGREY);
  
  // LEFT SIDE - Seismograph (centered vertically)
  tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN);  // Back to bright green
  tft.setCursor(5, 5);
  tft.println("SEISMOGRAPH");
  
  // Draw center line for seismograph at the calculated center
  tft.drawLine(5, SEISMO_CENTER, 155, SEISMO_CENTER, TFT_WHITE);  // White centerline
  
  // Draw static box for Latest Activity section (much bigger now - 115 pixels high)
  tft.drawRect(0, 125, 160, 115, TFT_DARKGREY);
  
  // RIGHT SIDE - Map
  tft.setTextColor(0x4E9F);  // Softer cyan instead of bright cyan
  tft.setCursor(MAP_X + 5, 5);
  if (isGlobalMode) {
    tft.println("GLOBAL");
  } else {
    tft.println("NEW ZEALAND");
  }
  
  drawDetailedNZMap();
  
  // Plot recent quakes
  for (int i = 0; i < quakeCount; i++) {
    if (recentQuakes[i].valid) {
      plotQuakeOnMap(recentQuakes[i].lat, recentQuakes[i].lon, recentQuakes[i].magnitude);
    }
  }
  
  // Plot the last quake with a big red dot on top
  plotLastQuakeOnMap();
  
  // Latest activity section at bottom of seismograph area
  updateLatestActivity();
  
  seismographX = 5;
  lastY = SEISMO_CENTER;  // Start at the center
}

void updateLatestActivity() {
  // Clear the latest activity area (much bigger now)
  tft.fillRect(1, 126, 158, 113, TFT_BLACK);
  
  // Choose which data to display based on mode
  QuakeData* displayQuake;
  String title;
  
  if (displayMode == 0) {
    // Latest activity
    displayQuake = &lastQuake;
    title = "LATEST ACTIVITY:";
  } else {
    // Highest NZ in last 24h
    displayQuake = &highestNZ;
    title = "HIGHEST NZ 24H:";
  }
  
  tft.setTextSize(1);
  tft.setTextColor(0xFD20);  // Softer amber/gold instead of bright yellow
  tft.setCursor(5, 132);
  tft.println(title);
  
  if (displayQuake->hasData) {
    tft.setTextColor(0xC618);  // Light grey instead of white
    
    // Show magnitude in BIG text at the top
    tft.setTextSize(3);
    tft.setCursor(5, 150);
    tft.print("M");
    tft.println(displayQuake->magnitude, 1);
    
    // Show location in small text across multiple lines
    tft.setTextSize(1);
    String loc = displayQuake->locality;
    
    // Split location into multiple lines (18 chars per line for size 1 font)
    int startPos = 0;
    int lineY = 178;
    int linesShown = 0;
    while (startPos < loc.length() && lineY < 215 && linesShown < 4) {
      String line;
      if (startPos + 18 < loc.length()) {
        // Try to break at a space
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
      
      tft.setCursor(5, lineY);
      tft.println(line);
      lineY += 9;
      linesShown++;
    }
    
    // Show time ago at the bottom in small text
    tft.setTextSize(1);
    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(5, 220);
    String timeAgo = getTimeAgo(displayQuake->timestamp);
    tft.print(timeAgo);
    tft.println(" ago");
    
  } else {
    tft.setTextColor(TFT_DARKGREY);
    tft.setTextSize(2);
    tft.setCursor(5, 170);
    tft.println("No data");
    tft.setCursor(5, 195);
    tft.println("available");
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
  int centerY = SEISMO_CENTER;  // Use the calculated center
  int maxAmplitude = 45; // Reduced amplitude for smaller vertical space
  
  // Random movement with occasional spikes
  int movement;
  if (random(100) < 5) {
    movement = random(-maxAmplitude, maxAmplitude);
  } else {
    movement = random(-12, 12);
  }
  
  int targetY = centerY + movement;
  int newY = (lastY * 3 + targetY) / 4; // Smooth the line
  
  // Keep within the seismograph bounds only
  if (newY < SEISMO_Y_START) newY = SEISMO_Y_START;
  if (newY > SEISMO_Y_END) newY = SEISMO_Y_END;
  
  // Draw new line segment
  tft.drawLine(seismographX, lastY, seismographX + 1, newY, TFT_GREEN);  // Back to green
  
  // Erase ahead of the line - but ONLY in the seismograph area
  int eraseX = (seismographX + 10) % SEISMO_WIDTH;
  if (eraseX < 5) eraseX = 5;
  tft.drawLine(eraseX, SEISMO_Y_START, eraseX, SEISMO_Y_END, TFT_BLACK);
  tft.drawPixel(eraseX, SEISMO_CENTER, TFT_WHITE); // Redraw white centerline pixel
  
  lastY = newY;
  seismographX++;
  if (seismographX >= 155) seismographX = 5; // Wrap around at right edge
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
      
      if (features.size() > 0) {
        // Process up to 20 quakes for the map and find highest in last 24h
        for (int i = 0; i < min((int)features.size(), 20); i++) {
          JsonObject quake = features[i];
          
          float magnitude, lat, lon;
          unsigned long quakeAge;
          
          if (isGlobalMode) {
            lon = quake["geometry"]["coordinates"][0];
            lat = quake["geometry"]["coordinates"][1];
            magnitude = quake["properties"]["mag"];
            
            // For global mode, we'd need to parse timestamp from API
            // For now, assume recent quakes are within 24h
            quakeAge = 0;  
            
            addQuakeToHistory(lat, lon, magnitude);
            
            // Track highest global magnitude
            if (magnitude > highestMag) {
              highestMag = magnitude;
              highestIndex = i;
            }
          } else {
            lat = quake["geometry"]["coordinates"][1];
            lon = quake["geometry"]["coordinates"][0];
            magnitude = quake["properties"]["magnitude"];
            
            addQuakeToHistory(lat, lon, magnitude);
            
            // Track highest NZ magnitude
            if (magnitude > highestMag) {
              highestMag = magnitude;
              highestIndex = i;
            }
          }
        }
        
        // Update the highest magnitude quake data
        if (highestIndex >= 0) {
          JsonObject highestQuake = features[highestIndex];
          
          if (isGlobalMode) {
            // Validate that we have the required fields
            if (highestQuake["properties"]["mag"].is<float>() && 
                highestQuake["properties"]["place"].is<const char*>()) {
              
              float mag = highestQuake["properties"]["mag"];
              if (!highestGlobal.hasData || mag > highestGlobal.magnitude) {
                highestGlobal.magnitude = mag;
                highestGlobal.locality = String(highestQuake["properties"]["place"].as<const char*>());
                highestGlobal.lon = highestQuake["geometry"]["coordinates"][0];
                highestGlobal.lat = highestQuake["geometry"]["coordinates"][1];
                highestGlobal.depth = highestQuake["geometry"]["coordinates"][2].is<int>() ? 
                                      highestQuake["geometry"]["coordinates"][2].as<int>() : 0;
                highestGlobal.time = "global";
                highestGlobal.timestamp = millis();
                highestGlobal.hasData = true;
              }
            }
          } else {
            // Validate NZ data
            if (highestQuake["properties"]["magnitude"].is<float>() && 
                highestQuake["properties"]["locality"].is<const char*>()) {
              
              float mag = highestQuake["properties"]["magnitude"];
              Serial.print("Highest NZ quake found: M");
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
            } else {
              Serial.println("NZ data validation failed!");
            }
          }
        }
        
        // Check if the latest quake is new
        JsonObject latestQuake = features[0];
        String quakeID;
        
        if (isGlobalMode) {
          quakeID = latestQuake["id"].as<const char*>();
        } else {
          quakeID = latestQuake["properties"]["publicID"].as<const char*>();
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
          
          if (isGlobalMode) {
            magnitude = latestQuake["properties"]["mag"];
            locality = latestQuake["properties"]["place"];
            lon = latestQuake["geometry"]["coordinates"][0];
            lat = latestQuake["geometry"]["coordinates"][1];
            depth = latestQuake["geometry"]["coordinates"][2];
            long timestamp = latestQuake["properties"]["time"];
            time = "global";
          } else {
            magnitude = latestQuake["properties"]["magnitude"];
            locality = latestQuake["properties"]["locality"];
            lat = latestQuake["geometry"]["coordinates"][1];
            lon = latestQuake["geometry"]["coordinates"][0];
            depth = latestQuake["properties"]["depth"];
            time = latestQuake["properties"]["time"];
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
  // Dark grey color for map outline (easier on eyes in dark mode)
  uint16_t mapColor = 0x5AEB;  // Medium-dark grey
  
  // NORTH ISLAND
  // Northland - narrow peninsula
  tft.drawLine(mapLon(173.8), mapLat(-34.4), mapLon(174.3), mapLat(-35.2), mapColor);
  tft.drawLine(mapLon(174.3), mapLat(-35.2), mapLon(174.5), mapLat(-35.8), mapColor);
  tft.drawLine(mapLon(174.5), mapLat(-35.8), mapLon(174.8), mapLat(-36.5), mapColor);
  
  // Auckland region - bulge out
  tft.drawLine(mapLon(174.8), mapLat(-36.5), mapLon(175.2), mapLat(-37.0), mapColor);
  
  // Coromandel Peninsula - stick out east
  tft.drawLine(mapLon(175.2), mapLat(-37.0), mapLon(175.8), mapLat(-37.2), mapColor);
  
  // Bay of Plenty to East Cape
  tft.drawLine(mapLon(175.8), mapLat(-37.2), mapLon(177.2), mapLat(-37.7), mapColor);
  tft.drawLine(mapLon(177.2), mapLat(-37.7), mapLon(178.3), mapLat(-37.8), mapColor);
  
  // East Cape curve
  tft.drawLine(mapLon(178.3), mapLat(-37.8), mapLon(178.5), mapLat(-38.5), mapColor);
  
  // Hawke's Bay - straight down
  tft.drawLine(mapLon(178.5), mapLat(-38.5), mapLon(177.8), mapLat(-39.5), mapColor);
  
  // Wairarapa to Wellington
  tft.drawLine(mapLon(177.8), mapLat(-39.5), mapLon(176.2), mapLat(-40.5), mapColor);
  tft.drawLine(mapLon(176.2), mapLat(-40.5), mapLon(175.5), mapLat(-41.1), mapColor);
  tft.drawLine(mapLon(175.5), mapLat(-41.1), mapLon(174.8), mapLat(-41.3), mapColor);
  
  // West coast North Island
  // Northland west
  tft.drawLine(mapLon(173.8), mapLat(-34.4), mapLon(173.5), mapLat(-35.5), mapColor);
  tft.drawLine(mapLon(173.5), mapLat(-35.5), mapLon(173.8), mapLat(-36.5), mapColor);
  
  // Taranaki - bulge out west
  tft.drawLine(mapLon(173.8), mapLat(-36.5), mapLon(174.0), mapLat(-38.0), mapColor);
  tft.drawLine(mapLon(174.0), mapLat(-38.0), mapLon(173.8), mapLat(-39.3), mapColor);
  
  // Whanganui to Wellington west coast
  tft.drawLine(mapLon(173.8), mapLat(-39.3), mapLon(174.5), mapLat(-40.5), mapColor);
  tft.drawLine(mapLon(174.5), mapLat(-40.5), mapLon(174.8), mapLat(-41.2), mapColor);
  
  // SOUTH ISLAND
  // Marlborough Sounds - top jagged bit
  tft.drawLine(mapLon(173.5), mapLat(-40.9), mapLon(173.8), mapLat(-41.0), mapColor);
  tft.drawLine(mapLon(173.8), mapLat(-41.0), mapLon(174.2), mapLat(-41.1), mapColor);
  tft.drawLine(mapLon(174.2), mapLat(-41.1), mapLon(174.0), mapLat(-41.3), mapColor);
  
  // Kaikoura - bump out
  tft.drawLine(mapLon(174.0), mapLat(-41.3), mapLon(174.2), mapLat(-42.0), mapColor);
  tft.drawLine(mapLon(174.2), mapLat(-42.0), mapLon(173.8), mapLat(-42.5), mapColor);
  
  // Banks Peninsula - small bump
  tft.drawLine(mapLon(173.8), mapLat(-42.5), mapLon(173.2), mapLat(-43.3), mapColor);
  tft.drawLine(mapLon(173.2), mapLat(-43.3), mapLon(172.8), mapLat(-43.6), mapColor);
  
  // Canterbury to Otago coast
  tft.drawLine(mapLon(172.8), mapLat(-43.6), mapLon(171.2), mapLat(-44.0), mapColor);
  tft.drawLine(mapLon(171.2), mapLat(-44.0), mapLon(170.5), mapLat(-45.0), mapColor);
  
  // Otago Peninsula
  tft.drawLine(mapLon(170.5), mapLat(-45.0), mapLon(170.8), mapLat(-45.8), mapColor);
  
  // Southland
  tft.drawLine(mapLon(170.8), mapLat(-45.8), mapLon(169.5), mapLat(-46.4), mapColor);
  tft.drawLine(mapLon(169.5), mapLat(-46.4), mapLon(168.3), mapLat(-46.6), mapColor);
  
  // Fiordland - very jagged west coast
  tft.drawLine(mapLon(168.3), mapLat(-46.6), mapLon(167.8), mapLat(-46.3), mapColor);
  tft.drawLine(mapLon(167.8), mapLat(-46.3), mapLon(167.2), mapLat(-45.8), mapColor);
  tft.drawLine(mapLon(167.2), mapLat(-45.8), mapLon(167.0), mapLat(-45.2), mapColor);
  tft.drawLine(mapLon(167.0), mapLat(-45.2), mapLon(166.8), mapLat(-44.5), mapColor);
  
  // West Coast - straight up
  tft.drawLine(mapLon(166.8), mapLat(-44.5), mapLon(167.5), mapLat(-43.8), mapColor);
  tft.drawLine(mapLon(167.5), mapLat(-43.8), mapLon(168.2), mapLat(-43.0), mapColor);
  
  // Westport area
  tft.drawLine(mapLon(168.2), mapLat(-43.0), mapLon(168.5), mapLat(-42.2), mapColor);
  
  // Tasman/Golden Bay
  tft.drawLine(mapLon(168.5), mapLat(-42.2), mapLon(172.5), mapLat(-40.7), mapColor);
  tft.drawLine(mapLon(172.5), mapLat(-40.7), mapLon(173.5), mapLat(-40.9), mapColor);
  
  // Mark major cities with softer cyan
  uint16_t cityColor = 0x4E9F;  // Softer cyan
  
  // Wellington
  int wellyX = mapLon(174.78);
  int wellyY = mapLat(-41.28);
  tft.fillCircle(wellyX, wellyY, 2, cityColor);
  
  // Auckland
  int auckX = mapLon(174.76);
  int auckY = mapLat(-36.85);
  tft.fillCircle(auckX, auckY, 2, cityColor);
  
  // Christchurch
  int chcX = mapLon(172.64);
  int chcY = mapLat(-43.53);
  tft.fillCircle(chcX, chcY, 2, cityColor);
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
  
  Serial.println("Settings loaded:");
  Serial.println("SSID: " + wifi_ssid);
  Serial.println("Region: " + selectedRegion);
  Serial.println("Threshold: " + String(magnitudeThreshold));
}

void saveSettings() {
  preferences.putString("wifi_ssid", wifi_ssid);
  preferences.putString("wifi_pass", wifi_password);
  preferences.putString("region", selectedRegion);
  preferences.putFloat("mag_thresh", magnitudeThreshold);
  
  Serial.println("Settings saved!");
}

String getAPI() {
  if (selectedRegion == "NZ") {
    return String(nzAPI);
  } else if (selectedRegion == "Global") {
    return String(globalAPI);
  } else if (selectedRegion == "Japan") {
    return String(japanAPI);
  } else if (selectedRegion == "Taiwan") {
    return String(taiwanAPI);
  } else if (selectedRegion == "California") {
    return String(californiaAPI);
  }
  return String(nzAPI);  // Default
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
            font-family: Arial, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
        }
        .container {
            max-width: 500px;
            margin: 0 auto;
            background: white;
            border-radius: 20px;
            padding: 30px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
        }
        h1 {
            color: #667eea;
            text-align: center;
            margin-bottom: 10px;
            font-size: 28px;
        }
        .subtitle {
            text-align: center;
            color: #888;
            margin-bottom: 30px;
            font-size: 14px;
        }
        .form-group {
            margin-bottom: 25px;
        }
        label {
            display: block;
            color: #333;
            font-weight: bold;
            margin-bottom: 8px;
            font-size: 14px;
        }
        input, select {
            width: 100%;
            padding: 12px 15px;
            border: 2px solid #e0e0e0;
            border-radius: 10px;
            font-size: 16px;
            transition: all 0.3s;
        }
        input:focus, select:focus {
            outline: none;
            border-color: #667eea;
            box-shadow: 0 0 0 3px rgba(102,126,234,0.1);
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
</head>
<body>
    <div class="container">
        <div class="wifi-icon">🌍</div>
        <h1>SeisMonitor</h1>
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
                        🇳🇿 New Zealand
                    </label>
                    <label class="region-option )rawliteral" + String(selectedRegion == "Global" ? "selected" : "") + R"rawliteral(">
                        <input type="radio" name="region" value="Global" )rawliteral" + String(selectedRegion == "Global" ? "checked" : "") + R"rawliteral(>
                        🌍 Global
                    </label>
                    <label class="region-option )rawliteral" + String(selectedRegion == "Japan" ? "selected" : "") + R"rawliteral(">
                        <input type="radio" name="region" value="Japan" )rawliteral" + String(selectedRegion == "Japan" ? "checked" : "") + R"rawliteral(>
                        🇯🇵 Japan
                    </label>
                    <label class="region-option )rawliteral" + String(selectedRegion == "Taiwan" ? "selected" : "") + R"rawliteral(">
                        <input type="radio" name="region" value="Taiwan" )rawliteral" + String(selectedRegion == "Taiwan" ? "checked" : "") + R"rawliteral(>
                        🇹🇼 Taiwan
                    </label>
                    <label class="region-option )rawliteral" + String(selectedRegion == "California" ? "selected" : "") + R"rawliteral(">
                        <input type="radio" name="region" value="California" )rawliteral" + String(selectedRegion == "California" ? "checked" : "") + R"rawliteral(>
                        🌴 California
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
            
            <button type="submit">💾 Save Settings</button>
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
            font-family: Arial;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
            margin: 0;
        }
        .success {
            background: white;
            padding: 40px;
            border-radius: 20px;
            text-align: center;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
        }
        .checkmark {
            font-size: 60px;
            color: #4CAF50;
            margin-bottom: 20px;
        }
        h1 { color: #667eea; margin-bottom: 10px; }
        p { color: #666; }
    </style>
</head>
<body>
    <div class="success">
        <div class="checkmark">✓</div>
        <h1>Settings Saved!</h1>
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
