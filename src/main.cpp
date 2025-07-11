// Optimized senseBox ESP8266 sketch - low power mode with 1min updates
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <time.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <BH1750.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

Adafruit_BMP280 bmp;
OneWire oneWire(0); // D3 (GPIO 0)
DallasTemperature ds18b20(&oneWire);
BH1750 lightMeter;

#include "secrets.h"
#define HOST "ingress.opensensemap.org"

unsigned long lastSensorRead = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastUpload = 0;
unsigned long lastTimeSync = 0;

bool bmpOk = false;
float currentTemp = 0.0;
float currentPres = 0.0;
float currentDS18B20 = 0.0;
float currentLux = 0.0;

// Pressure trend variables
int pressureTrend = 2; // 0=hard up, 1=slight up, 2=no trend, 3=slight down, 4=hard down
unsigned long lastTrendUpdate = 0;

// Arrow bitmaps (16x16 pixels each)
// Each byte represents 8 horizontal pixels, MSB first
const unsigned char PROGMEM arrow_hard_up[] = {
  0x01, 0x80, // -------■--------
  0x03, 0xc0, // ------■■--------
  0x07, 0xe0, // -----■■■--------
  0x0f, 0xf0, // ----■■■■--------
  0x1f, 0xf8, // ---■■■■■--------
  0x3f, 0xfc, // --■■■■■■--------
  0x7f, 0xfe, // -■■■■■■■--------
  0xff, 0xff, // ■■■■■■■■--------
  0x03, 0xc0, // ------■■--------
  0x03, 0xc0, // ------■■--------
  0x03, 0xc0, // ------■■--------
  0x03, 0xc0, // ------■■--------
  0x03, 0xc0, // ------■■--------
  0x03, 0xc0, // ------■■--------
  0x03, 0xc0, // ------■■--------
  0x03, 0xc0  // ------■■--------
};

const unsigned char PROGMEM arrow_slight_up[] = {
  0x00, 0x01, // --------------■
  0x00, 0x03, // -------------■■
  0x00, 0x07, // ------------■■■
  0x00, 0x0f, // -----------■■■■
  0x00, 0x1f, // ----------■■■■■
  0x00, 0x3f, // ---------■■■■■■
  0x00, 0x7f, // --------■■■■■■■
  0x00, 0xff, // -------■■■■■■■■
  0x01, 0xc0, // -------■■■-----
  0x03, 0x80, // ------■■■------
  0x07, 0x00, // -----■■■-------
  0x0e, 0x00, // ----■■■--------
  0x1c, 0x00, // ---■■■---------
  0x38, 0x00, // --■■■----------
  0x70, 0x00, // -■■■-----------
  0xe0, 0x00  // ■■■------------
};

const unsigned char PROGMEM arrow_flat[] = {
  0x00, 0x00, // ----------------
  0x00, 0x00, // ----------------
  0x00, 0x00, // ----------------
  0x00, 0x00, // ----------------
  0x00, 0x00, // ----------------
  0x00, 0x01, // --------------■
  0x00, 0x03, // -------------■■
  0xff, 0xff, // ■■■■■■■■■■■■■■■■
  0xff, 0xff, // ■■■■■■■■■■■■■■■■
  0x00, 0x03, // -------------■■
  0x00, 0x01, // --------------■
  0x00, 0x00, // ----------------
  0x00, 0x00, // ----------------
  0x00, 0x00, // ----------------
  0x00, 0x00, // ----------------
  0x00, 0x00  // ----------------
};

const unsigned char PROGMEM arrow_slight_down[] = {
  0xe0, 0x00, // ■■■------------
  0x70, 0x00, // -■■■-----------
  0x38, 0x00, // --■■■----------
  0x1c, 0x00, // ---■■■---------
  0x0e, 0x00, // ----■■■--------
  0x07, 0x00, // -----■■■-------
  0x03, 0x80, // ------■■■------
  0x01, 0xc0, // -------■■■-----
  0x00, 0xff, // -------■■■■■■■■
  0x00, 0x7f, // --------■■■■■■■
  0x00, 0x3f, // ---------■■■■■■
  0x00, 0x1f, // ----------■■■■■
  0x00, 0x0f, // -----------■■■■
  0x00, 0x07, // ------------■■■
  0x00, 0x03, // -------------■■
  0x00, 0x01  // --------------■
};

const unsigned char PROGMEM arrow_hard_down[] = {
  0x03, 0xc0, // ------■■--------
  0x03, 0xc0, // ------■■--------
  0x03, 0xc0, // ------■■--------
  0x03, 0xc0, // ------■■--------
  0x03, 0xc0, // ------■■--------
  0x03, 0xc0, // ------■■--------
  0x03, 0xc0, // ------■■--------
  0x03, 0xc0, // ------■■--------
  0xff, 0xff, // ■■■■■■■■■■■■■■■■
  0x7f, 0xfe, // -■■■■■■■■■■■■■■-
  0x3f, 0xfc, // --■■■■■■■■■■■■--
  0x1f, 0xf8, // ---■■■■■■■■■■---
  0x0f, 0xf0, // ----■■■■■■■■----
  0x07, 0xe0, // -----■■■■■■-----
  0x03, 0xc0, // ------■■■■------
  0x01, 0x80  // -------■■-------
};

void connectWiFi();
void disconnectWiFi();
void syncTime();
void uploadToOSeM();
void postCombinedValues();
void showBootScreen();
void showError(const char *msg);
bool isNight();
void updateSensor();
void updateDisplay();
void calculatePressureTrend();
void drawTrendArrow();

void connectWiFi()
{
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20)
  {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nWiFi connected");
  }
  else
  {
    Serial.println("\nWiFi failed");
  }
}

void disconnectWiFi()
{
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
}

void syncTime()
{
  configTime(3600, 3600, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  time_t now = time(nullptr);
  while (now < 100000)
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("\nTime synced");
}

void uploadToOSeM()
{
  if (!bmpOk)
    return;
  postCombinedValues();
}

void postCombinedValues()
{
  WiFiClient client;
  if (!client.connect(HOST, 80))
  {
    Serial.println("Connection to OSeM failed");
    return;
  }

  String json = "[";
  json += "{\"sensor\":\"" + String(SENSOR_ID_TEMP) + "\",\"value\":\"" + String(currentTemp, 2) + "\"},";
  json += "{\"sensor\":\"" + String(SENSOR_ID_PRES) + "\",\"value\":\"" + String(currentPres, 2) + "\"},";
  json += "{\"sensor\":\"" + String(SENSOR_ID_TEMP_OUT) + "\",\"value\":\"" + String(currentDS18B20, 2) + "\"},";
  json += "{\"sensor\":\"" + String(SENSOR_ID_LUM) + "\",\"value\":\"" + String(currentLux, 2) + "\"}";
  json += "]";

  client.print(String("POST /boxes/") + OSEM_BOX_ID + "/data HTTP/1.1\r\n" +
               "Host: " + HOST + "\r\n" +
               "Authorization: " + OSEM_AUTH + "\r\n" +
               "Content-Type: application/json\r\n" +
               "Connection: close\r\n" +
               "Content-Length: " + json.length() + "\r\n\r\n" +
               json);

  while (client.connected())
  {
    if (client.available())
    {
      String line = client.readStringUntil('\n');
      Serial.println(line);
    }
  }
  client.stop();
}

void showBootScreen()
{
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(10, 10);
  display.println("senseBox");
  display.setTextSize(1);
  display.setCursor(10, 35);
  display.println("Starting...");
  display.setCursor(10, 45);
  display.println("Connecting WiFi");
  display.display();
  delay(2000);
}

void showError(const char *msg)
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(10, 28);
  display.println(msg);
  display.display();
}

bool isNight()
{
  time_t now = time(nullptr);
  struct tm *t = localtime(&now);
  return (t->tm_hour >= 22 || t->tm_hour < 8);
}

void updateSensor()
{
  if (bmpOk)
  {
    currentTemp = bmp.readTemperature() - 4.0;
    currentPres = bmp.readPressure() / 100.0F;
  }

  ds18b20.requestTemperatures();
  currentDS18B20 = ds18b20.getTempCByIndex(0);

  currentLux = lightMeter.readLightLevel();
}

void updateDisplay()
{
  if (isNight())
  {
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    return;
  }
  else
  {
    display.ssd1306_command(SSD1306_DISPLAYON);
  }

  time_t now = time(nullptr);
  struct tm *t = localtime(&now);

  char dateStr[32];  // Much larger buffer to satisfy compiler warning checks
  snprintf(dateStr, sizeof(dateStr), "%02d.%02d.%04d", t->tm_mday, t->tm_mon + 1, 1900 + t->tm_year);

  char timeStr[6];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", t->tm_hour, t->tm_min);

  char tempStr[16];
  snprintf(tempStr, sizeof(tempStr), "%.2f C", currentTemp);

  char presStr[16];
  snprintf(presStr, sizeof(presStr), "%.2f hPa", currentPres);

  char extTempStr[16];
  snprintf(extTempStr, sizeof(extTempStr), "%.2f C", currentDS18B20);

  char luxStr[16];
  snprintf(luxStr, sizeof(luxStr), "%.0f lx", currentLux);

  display.clearDisplay();
  display.setTextSize(1);
  int16_t x1, y1;
  uint16_t w, h;

  // Draw pressure trend arrow first (top-left corner)
  drawTrendArrow();

  display.getTextBounds(dateStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 0);
  display.println(dateStr);

  display.setTextSize(2);
  display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 12);
  display.println(timeStr);

  display.setTextSize(1);
  display.setCursor(0, 38);
  display.println(tempStr);
  display.setCursor(64, 38);
  display.println(presStr);
  display.setCursor(0, 50);
  display.println(extTempStr);
  display.setCursor(64, 50);
  display.println(luxStr);

  display.display();
}

void calculatePressureTrend()
{
  Serial.println("Starting pressure trend calculation using OpenSenseMap statistics API...");
  
  WiFiClientSecure client;
  client.setInsecure(); // Skip certificate verification for simplicity
  String host = "api.opensensemap.org";

  Serial.print("Connecting to: ");
  Serial.println(host);
  
  if (!client.connect(host.c_str(), 443)) // HTTPS port 443
  {
    Serial.println("Failed to connect to OpenSenseMap API");
    return;
  }
  
  Serial.println("Connected to API server");

  // Get current time for API request
  time_t now = time(nullptr);
  time_t twelveHoursAgo = now - (12 * 3600); // 12 hours ago
  time_t sixHoursAgo = now - (6 * 3600);     // 6 hours ago
  time_t threeHoursAgo = now - (3 * 3600);   // 3 hours ago

  // Copy the tm structures to avoid overwriting by subsequent gmtime calls
  struct tm tm_now = *gmtime(&now);
  struct tm tm_12h = *gmtime(&twelveHoursAgo);
  struct tm tm_6h = *gmtime(&sixHoursAgo);
  struct tm tm_3h = *gmtime(&threeHoursAgo);

  char time_now[32], time_12h[32], time_6h[32], time_3h[32];
  strftime(time_now, sizeof(time_now), "%Y-%m-%dT%H:%M:%SZ", &tm_now);
  strftime(time_12h, sizeof(time_12h), "%Y-%m-%dT%H:%M:%SZ", &tm_12h);
  strftime(time_6h, sizeof(time_6h), "%Y-%m-%dT%H:%M:%SZ", &tm_6h);
  strftime(time_3h, sizeof(time_3h), "%Y-%m-%dT%H:%M:%SZ", &tm_3h);

  // Debug time calculations
  Serial.print("Current time: ");
  Serial.println(time_now);
  Serial.print("12 hours ago: ");
  Serial.println(time_12h);
  
  // Use statistics API to get arithmetic means for 1-hour windows (more data points)
  String url = "/statistics/descriptive?boxId=" + String(OSEM_BOX_ID) + 
               "&phenomenon=Pressure" +
               "&from-date=" + String(time_12h) +
               "&to-date=" + String(time_now) +
               "&operation=arithmeticMean" +
               "&window=1h" +
               "&format=tidy";
  
  Serial.print("Requesting URL: ");
  Serial.println(url);
  Serial.print("Box ID: ");
  Serial.println(OSEM_BOX_ID);
  Serial.print("Time range: ");
  Serial.print(time_12h);
  Serial.print(" to ");
  Serial.println(time_now);

  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Connection: close\r\n\r\n");

  Serial.println("HTTP request sent, waiting for response...");
  
  String payload = "";
  bool headersPassed = false;
  unsigned long timeout = millis();

  while (client.connected() || client.available())
  {
    if (millis() - timeout > 15000) // 15 second timeout for HTTPS
    {
      Serial.println("HTTP request timeout");
      break;
    }
    
    if (client.available())
    {
      if (!headersPassed)
      {
        String line = client.readStringUntil('\n');
        if (line == "\r")
        {
          headersPassed = true;
          Serial.println("Headers received, reading payload...");
        }
      }
      else
      {
        char c = client.read();
        if (c > 0)
        {
          payload += c;
        }
      }
    }
  }
  client.stop();
  
  Serial.print("Statistics API Response length: ");
  Serial.println(payload.length());
  Serial.println("CSV Response:");
  Serial.println(payload);

  if (payload.length() == 0)
  {
    Serial.println("No statistics data received");
    return;
  }
  
  // Parse CSV response
  // Expected format: sensorId,time_start,arithmeticMean_3h
  // But let's be more flexible and debug what we're actually getting
  
  float pressureMeans[15] = {0}; // Increase array size to handle more windows
  int validMeans = 0;
  int totalLines = 0;
  
  // Split response into lines
  int lineStart = 0;
  
  for (int i = 0; i <= (int)payload.length(); i++)
  {
    if (i == (int)payload.length() || payload.charAt(i) == '\n')
    {
      if (i > lineStart)
      {
        String line = payload.substring(lineStart, i);
        line.trim(); // Remove whitespace and carriage returns
        totalLines++;
        
        Serial.print("Line ");
        Serial.print(totalLines);
        Serial.print(": '");
        Serial.print(line);
        Serial.println("'");
        
        if (line.length() > 0 && validMeans < 15)
        {
          // Skip obvious header lines but be less restrictive
          if (line.startsWith("sensorId,") || line == "ad" || line == "0")
          {
            Serial.println("  -> Skipping header/empty line");
          }
          else if (line.indexOf(',') == -1)
          {
            Serial.println("  -> Skipping line without comma");
          }
          else
          {
            // Parse CSV line - be more flexible with field count
            int firstComma = line.indexOf(',');
            int secondComma = line.indexOf(',', firstComma + 1);
            
            if (firstComma > 0 && secondComma > firstComma)
            {
              String sensorId = line.substring(0, firstComma);
              String timeStart = line.substring(firstComma + 1, secondComma);
              
              // Get value - could be in 3rd field or beyond
              String remainingPart = line.substring(secondComma + 1);
              int nextComma = remainingPart.indexOf(',');
              String valueStr = (nextComma > 0) ? remainingPart.substring(0, nextComma) : remainingPart;
              
              float pressureValue = valueStr.toFloat();
              
              Serial.print("  -> Parsed: SensorID='");
              Serial.print(sensorId);
              Serial.print("' Time='");
              Serial.print(timeStart);
              Serial.print("' Value='");
              Serial.print(valueStr);
              Serial.print("' (");
              Serial.print(pressureValue);
              Serial.println(" hPa)");
              
              // More relaxed validation - focus on reasonable pressure values
              if (pressureValue > 800 && pressureValue < 1200 && sensorId.length() > 10)
              {
                Serial.print("    -> ACCEPTED as valid measurement #");
                Serial.println(validMeans + 1);
                
                // Store the pressure value
                pressureMeans[validMeans] = pressureValue;
                validMeans++;
              }
              else
              {
                Serial.print("    -> REJECTED: pressure=");
                Serial.print(pressureValue);
                Serial.print(" (need 800-1200), sensorID len=");
                Serial.println(sensorId.length());
              }
            }
            else
            {
              Serial.println("  -> Malformed CSV structure (not enough commas)");
            }
          }
        }
      }
      lineStart = i + 1;
    }
  }
  
  Serial.print("Total lines processed: ");
  Serial.print(totalLines);
  Serial.print(", Valid measurements found: ");
  Serial.println(validMeans);

  if (validMeans < 2)
  {
    Serial.print("Not enough time windows for trend calculation. Found: ");
    Serial.print(validMeans);
    Serial.println(" valid data points (need at least 2)");
    return;
  }

  Serial.print("Successfully parsed ");
  Serial.print(validMeans);
  Serial.println(" valid pressure measurements");

  // Calculate trend based on difference between recent and older pressure means
  // Compare most recent window with oldest available window
  float recentPressure = pressureMeans[validMeans - 1]; // Most recent
  float olderPressure = pressureMeans[0]; // Oldest
  float pressureDiff = recentPressure - olderPressure;

  Serial.print("Pressure difference (recent - old): ");
  Serial.print(pressureDiff);
  Serial.println(" hPa");

  // Convert to trend categories based on pressure difference over time
  // Thresholds based on typical barometric pressure change rates
  if (pressureDiff > 1.5)
  {
    pressureTrend = 0; // Hard upward trend
    Serial.println("-> Hard upward trend");
  }
  else if (pressureDiff > 0.5)
  {
    pressureTrend = 1; // Slight upward trend
    Serial.println("-> Slight upward trend");
  }
  else if (pressureDiff > -0.5)
  {
    pressureTrend = 2; // No significant trend
    Serial.println("-> No significant trend");
  }
  else if (pressureDiff > -1.5)
  {
    pressureTrend = 3; // Slight downward trend
    Serial.println("-> Slight downward trend");
  }
  else
  {
    pressureTrend = 4; // Hard downward trend
    Serial.println("-> Hard downward trend");
  }

  Serial.print("Pressure trend calculated: ");
  Serial.print(pressureDiff);
  Serial.print(" hPa difference, category: ");
  Serial.println(pressureTrend);
}

void drawTrendArrow()
{
  // Draw 16x16 pixel arrow in top-left corner
  int x = 2;  // Small margin from edge
  int y = 2;

  const unsigned char* bitmap = nullptr;
  
  // Select the appropriate arrow bitmap
  switch (pressureTrend)
  {
    case 0: // Hard upward trend
      bitmap = arrow_hard_up;
      break;
    case 1: // Slight upward trend
      bitmap = arrow_slight_up;
      break;
    case 2: // No trend (flat)
      bitmap = arrow_flat;
      break;
    case 3: // Slight downward trend
      bitmap = arrow_slight_down;
      break;
    case 4: // Hard downward trend
      bitmap = arrow_hard_down;
      break;
    default:
      bitmap = arrow_flat; // Fallback
      break;
  }
  
  // Draw the 16x16 bitmap
  if (bitmap != nullptr)
  {
    display.drawBitmap(x, y, bitmap, 16, 16, SSD1306_WHITE);
  }
}

void setup()
{
  Serial.begin(115200);
  Wire.begin(2, 14);
  delay(100);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    Serial.println("OLED failed");
    while (1)
      ;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.display();
  showBootScreen();

  if (!bmp.begin(0x76))
  {
    if (!bmp.begin(0x77))
    {
      showError("BMP280 MISSING");
      return;
    }
  }
  bmpOk = true;

  ds18b20.begin();

  if (!lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE))
  {
    showError("BH1750 MISSING");
    return;
  }

  connectWiFi();
  syncTime();
  
  // Calculate initial pressure trend during startup
  calculatePressureTrend();
  lastTrendUpdate = millis();
  
  disconnectWiFi();

  lastSensorRead = millis();
  lastDisplayUpdate = millis();
  lastUpload = millis();
  lastTimeSync = millis();

  updateSensor();
  updateDisplay();
}

void loop()
{
  unsigned long now = millis();

  if (now - lastSensorRead >= 60000)
  {
    updateSensor();
    lastSensorRead = now;
  }

  if (now - lastDisplayUpdate >= 60000)
  {
    updateDisplay();
    lastDisplayUpdate = now;
  }

  bool uploadDue = (now - lastUpload >= 600000);
  bool timeSyncDue = (now - lastTimeSync >= 600000);
  
  if (uploadDue || timeSyncDue)
  {
    connectWiFi();
    
    if (uploadDue)
    {
      uploadToOSeM();
      // Calculate pressure trend after uploading data (while WiFi is still connected)
      calculatePressureTrend();
      lastTrendUpdate = millis();
      lastUpload = now;
    }
    
    if (timeSyncDue)
    {
      syncTime();
      lastTimeSync = now;
    }
    
    disconnectWiFi();
  }

  delay(5000);
}