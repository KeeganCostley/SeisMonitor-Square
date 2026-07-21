#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

// WiFi credentials
const char* ssid = "MOOSENET";
const char* password = "hungrycurtain898";

// API endpoints
const char* nzAPI = "https://api.geonet.org.nz/quake?MMI=2";
const char* globalAPI = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/2.5_day.geojson";

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
int displayMode = 0;  // 0 = latest, 1 = highest NZ, 2 = highest Global
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

void setup() {
  Serial.begin(115200);
  
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
  
  // Connect to WiFi
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Earthquake Monitor");
  tft.println("Connecting WiFi...");
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  tft.println("WiFi Connected!");
  Serial.println("\nWiFi connected!");
  delay(2000);
  
  // Initial quake check to populate last quake data
  checkForNewQuakes();
  
  // Initialize activity timer
  lastActivityTime = millis();
  lastDisplaySwitch = millis();
  
  // Start with seismograph display
  drawSeismographScreen();
}

void loop() {
  checkButton();
  
  // Check for rest mode timeout (45 seconds of no activity)
  if (!showingQuake && !isRestMode && (millis() - lastActivityTime > REST_MODE_TIMEOUT)) {
    isRestMode = true;
    tft.invertDisplay(true);  // Invert colors
  }
  
  // Cycle through display modes every 60 seconds
  if (!showingQuake && (millis() - lastDisplaySwitch > DISPLAY_CYCLE_TIME)) {
    displayMode = (displayMode + 1) % 3;  // Cycle 0->1->2->0
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
  tft.setTextColor(TFT_GREEN);
  tft.setCursor(5, 5);
  tft.println("SEISMOGRAPH");
  
  // Draw center line for seismograph at the calculated center
  tft.drawLine(5, SEISMO_CENTER, 155, SEISMO_CENTER, TFT_DARKGREEN);
  
  // Draw static box for Latest Activity section (much bigger now - 115 pixels high)
  tft.drawRect(0, 125, 160, 115, TFT_DARKGREY);
  
  // RIGHT SIDE - Map
  tft.setTextColor(TFT_CYAN);
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
  } else if (displayMode == 1) {
    // Highest NZ in last 24h
    displayQuake = &highestNZ;
    title = "HIGHEST NZ 24H:";
  } else {
    // Highest Global in last 24h
    displayQuake = &highestGlobal;
    title = "HIGHEST GLOBAL 24H:";
  }
  
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(5, 132);
  tft.println(title);
  
  if (displayQuake->hasData) {
    tft.setTextColor(TFT_WHITE);
    
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
  tft.drawLine(seismographX, lastY, seismographX + 1, newY, TFT_GREEN);
  
  // Erase ahead of the line - but ONLY in the seismograph area
  int eraseX = (seismographX + 10) % SEISMO_WIDTH;
  if (eraseX < 5) eraseX = 5;
  tft.drawLine(eraseX, SEISMO_Y_START, eraseX, SEISMO_Y_END, TFT_BLACK);
  tft.drawPixel(eraseX, SEISMO_CENTER, TFT_DARKGREEN); // Redraw centerline pixel
  
  lastY = newY;
  seismographX++;
  if (seismographX >= 155) seismographX = 5; // Wrap around at right edge
}

void checkForNewQuakes() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    const char* apiToUse = isGlobalMode ? globalAPI : nzAPI;
    http.begin(apiToUse);
    int httpCode = http.GET();
    
    if (httpCode > 0) {
      String payload = http.getString();
      
      Serial.print("API Response Code: ");
      Serial.println(httpCode);
      Serial.print("Payload size: ");
      Serial.println(payload.length());
      
      // Larger buffer for global API (can be quite big)
      DynamicJsonDocument doc(32768);  // Increased from 16384 to 32768 bytes
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
              if (!highestNZ.hasData || mag > highestNZ.magnitude) {
                highestNZ.magnitude = mag;
                highestNZ.locality = String(highestQuake["properties"]["locality"].as<const char*>());
                highestNZ.lat = highestQuake["geometry"]["coordinates"][1];
                highestNZ.lon = highestQuake["geometry"]["coordinates"][0];
                highestNZ.depth = highestQuake["properties"]["depth"];
                highestNZ.time = String(highestQuake["properties"]["time"].as<const char*>());
                highestNZ.timestamp = millis();
                highestNZ.hasData = true;
              }
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
  // Cool retro color fade effect with smooth brightness reduction!
  // First: Color fade through rainbow
  uint16_t fadeColors[] = {
    TFT_RED,
    TFT_ORANGE, 
    TFT_YELLOW,
    TFT_GREEN,
    TFT_CYAN,
    TFT_BLUE,
    TFT_PURPLE
  };
  
  int numColors = 7;
  
  // Fade through each color quickly
  for (int i = 0; i < numColors; i++) {
    tft.fillScreen(fadeColors[i]);
    delay(80);  // Hold each color for 80ms
  }
  
  // Then: Smooth fade to black by drawing progressively darker rectangles
  // Create a fake "dimming" effect with grey overlays
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
  // NORTH ISLAND
  // Northland - narrow peninsula
  tft.drawLine(mapLon(173.8), mapLat(-34.4), mapLon(174.3), mapLat(-35.2), TFT_WHITE);
  tft.drawLine(mapLon(174.3), mapLat(-35.2), mapLon(174.5), mapLat(-35.8), TFT_WHITE);
  tft.drawLine(mapLon(174.5), mapLat(-35.8), mapLon(174.8), mapLat(-36.5), TFT_WHITE);
  
  // Auckland region - bulge out
  tft.drawLine(mapLon(174.8), mapLat(-36.5), mapLon(175.2), mapLat(-37.0), TFT_WHITE);
  
  // Coromandel Peninsula - stick out east
  tft.drawLine(mapLon(175.2), mapLat(-37.0), mapLon(175.8), mapLat(-37.2), TFT_WHITE);
  
  // Bay of Plenty to East Cape
  tft.drawLine(mapLon(175.8), mapLat(-37.2), mapLon(177.2), mapLat(-37.7), TFT_WHITE);
  tft.drawLine(mapLon(177.2), mapLat(-37.7), mapLon(178.3), mapLat(-37.8), TFT_WHITE);
  
  // East Cape curve
  tft.drawLine(mapLon(178.3), mapLat(-37.8), mapLon(178.5), mapLat(-38.5), TFT_WHITE);
  
  // Hawke's Bay - straight down
  tft.drawLine(mapLon(178.5), mapLat(-38.5), mapLon(177.8), mapLat(-39.5), TFT_WHITE);
  
  // Wairarapa to Wellington
  tft.drawLine(mapLon(177.8), mapLat(-39.5), mapLon(176.2), mapLat(-40.5), TFT_WHITE);
  tft.drawLine(mapLon(176.2), mapLat(-40.5), mapLon(175.5), mapLat(-41.1), TFT_WHITE);
  tft.drawLine(mapLon(175.5), mapLat(-41.1), mapLon(174.8), mapLat(-41.3), TFT_WHITE);
  
  // West coast North Island
  // Northland west
  tft.drawLine(mapLon(173.8), mapLat(-34.4), mapLon(173.5), mapLat(-35.5), TFT_WHITE);
  tft.drawLine(mapLon(173.5), mapLat(-35.5), mapLon(173.8), mapLat(-36.5), TFT_WHITE);
  
  // Taranaki - bulge out west
  tft.drawLine(mapLon(173.8), mapLat(-36.5), mapLon(174.0), mapLat(-38.0), TFT_WHITE);
  tft.drawLine(mapLon(174.0), mapLat(-38.0), mapLon(173.8), mapLat(-39.3), TFT_WHITE);
  
  // Whanganui to Wellington west coast
  tft.drawLine(mapLon(173.8), mapLat(-39.3), mapLon(174.5), mapLat(-40.5), TFT_WHITE);
  tft.drawLine(mapLon(174.5), mapLat(-40.5), mapLon(174.8), mapLat(-41.2), TFT_WHITE);
  
  // SOUTH ISLAND
  // Marlborough Sounds - top jagged bit
  tft.drawLine(mapLon(173.5), mapLat(-40.9), mapLon(173.8), mapLat(-41.0), TFT_WHITE);
  tft.drawLine(mapLon(173.8), mapLat(-41.0), mapLon(174.2), mapLat(-41.1), TFT_WHITE);
  tft.drawLine(mapLon(174.2), mapLat(-41.1), mapLon(174.0), mapLat(-41.3), TFT_WHITE);
  
  // Kaikoura - bump out
  tft.drawLine(mapLon(174.0), mapLat(-41.3), mapLon(174.2), mapLat(-42.0), TFT_WHITE);
  tft.drawLine(mapLon(174.2), mapLat(-42.0), mapLon(173.8), mapLat(-42.5), TFT_WHITE);
  
  // Banks Peninsula - small bump
  tft.drawLine(mapLon(173.8), mapLat(-42.5), mapLon(173.2), mapLat(-43.3), TFT_WHITE);
  tft.drawLine(mapLon(173.2), mapLat(-43.3), mapLon(172.8), mapLat(-43.6), TFT_WHITE);
  
  // Canterbury to Otago coast
  tft.drawLine(mapLon(172.8), mapLat(-43.6), mapLon(171.2), mapLat(-44.0), TFT_WHITE);
  tft.drawLine(mapLon(171.2), mapLat(-44.0), mapLon(170.5), mapLat(-45.0), TFT_WHITE);
  
  // Otago Peninsula
  tft.drawLine(mapLon(170.5), mapLat(-45.0), mapLon(170.8), mapLat(-45.8), TFT_WHITE);
  
  // Southland
  tft.drawLine(mapLon(170.8), mapLat(-45.8), mapLon(169.5), mapLat(-46.4), TFT_WHITE);
  tft.drawLine(mapLon(169.5), mapLat(-46.4), mapLon(168.3), mapLat(-46.6), TFT_WHITE);
  
  // Fiordland - very jagged west coast
  tft.drawLine(mapLon(168.3), mapLat(-46.6), mapLon(167.8), mapLat(-46.3), TFT_WHITE);
  tft.drawLine(mapLon(167.8), mapLat(-46.3), mapLon(167.2), mapLat(-45.8), TFT_WHITE);
  tft.drawLine(mapLon(167.2), mapLat(-45.8), mapLon(167.0), mapLat(-45.2), TFT_WHITE);
  tft.drawLine(mapLon(167.0), mapLat(-45.2), mapLon(166.8), mapLat(-44.5), TFT_WHITE);
  
  // West Coast - straight up
  tft.drawLine(mapLon(166.8), mapLat(-44.5), mapLon(167.5), mapLat(-43.8), TFT_WHITE);
  tft.drawLine(mapLon(167.5), mapLat(-43.8), mapLon(168.2), mapLat(-43.0), TFT_WHITE);
  
  // Westport area
  tft.drawLine(mapLon(168.2), mapLat(-43.0), mapLon(168.5), mapLat(-42.2), TFT_WHITE);
  
  // Tasman/Golden Bay
  tft.drawLine(mapLon(168.5), mapLat(-42.2), mapLon(172.5), mapLat(-40.7), TFT_WHITE);
  tft.drawLine(mapLon(172.5), mapLat(-40.7), mapLon(173.5), mapLat(-40.9), TFT_WHITE);
  
  // Mark major cities
  // Wellington
  int wellyX = mapLon(174.78);
  int wellyY = mapLat(-41.28);
  tft.fillCircle(wellyX, wellyY, 2, TFT_CYAN);
  
  // Auckland
  int auckX = mapLon(174.76);
  int auckY = mapLat(-36.85);
  tft.fillCircle(auckX, auckY, 2, TFT_CYAN);
  
  // Christchurch
  int chcX = mapLon(172.64);
  int chcY = mapLat(-43.53);
  tft.fillCircle(chcX, chcY, 2, TFT_CYAN);
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

void plotLastQuakeOnMap() {
  // Determine which quake to plot based on current display mode
  QuakeData* displayQuake;
  
  if (displayMode == 0) {
    displayQuake = &lastQuake;
  } else if (displayMode == 1) {
    displayQuake = &highestNZ;
  } else {
    displayQuake = &highestGlobal;
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
  
  // Draw a big red dot (radius 5) with white outline
  tft.fillCircle(x, y, 5, TFT_RED);
  tft.drawCircle(x, y, 6, TFT_WHITE);  // White outline for visibility
}
