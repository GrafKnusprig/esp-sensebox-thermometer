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

  struct tm *tm_now = gmtime(&now);
  struct tm *tm_12h = gmtime(&twelveHoursAgo);
  struct tm *tm_6h = gmtime(&sixHoursAgo);
  struct tm *tm_3h = gmtime(&threeHoursAgo);

  char time_now[32], time_12h[32], time_6h[32], time_3h[32];
  strftime(time_now, sizeof(time_now), "%Y-%m-%dT%H:%M:%SZ", tm_now);
  strftime(time_12h, sizeof(time_12h), "%Y-%m-%dT%H:%M:%SZ", tm_12h);
  strftime(time_6h, sizeof(time_6h), "%Y-%m-%dT%H:%M:%SZ", tm_6h);
  strftime(time_3h, sizeof(time_3h), "%Y-%m-%dT%H:%M:%SZ", tm_3h);

  // Use statistics API to get arithmetic means for 3-hour windows
  String url = "/statistics/descriptive?boxId=" + String(OSEM_BOX_ID) + 
               "&phenomenon=Pressure" +
               "&from-date=" + String(time_12h) +
               "&to-date=" + String(time_now) +
               "&operation=arithmeticMean" +
               "&window=3h" +
               "&format=tidy";
  
  Serial.print("Requesting URL: ");
  Serial.println(url);

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
  // Skip non-data lines and process actual data lines
  
  float pressureMeans[4] = {0, 0, 0, 0}; // Up to 4 time windows
  int validMeans = 0;
  
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
        
        if (line.length() > 0 && validMeans < 4)
        {
          // Skip obvious non-data lines
          if (line == "ad" || 
              line == "0" ||
              line.startsWith("sensorId,") ||
              line.indexOf(',') == -1)
          {
            Serial.print("Skipping non-data line: ");
            Serial.println(line);
          }
          else
          {
            // Parse CSV line: sensorId,time_start,value
            int firstComma = line.indexOf(',');
            int secondComma = line.indexOf(',', firstComma + 1);
            int thirdComma = line.indexOf(',', secondComma + 1);
            
            // Valid data line should have exactly 3 fields (2 commas)
            if (firstComma > 0 && secondComma > firstComma && thirdComma == -1)
            {
              String sensorId = line.substring(0, firstComma);
              String timeStart = line.substring(firstComma + 1, secondComma);
              String valueStr = line.substring(secondComma + 1);
              
              float pressureValue = valueStr.toFloat();
              
              // Validate: sensor ID should be long hex string, time should contain date format, pressure should be reasonable
              if (sensorId.length() > 20 && 
                  timeStart.indexOf('T') > 0 && 
                  pressureValue > 800 && 
                  pressureValue < 1200)
              {
                Serial.print("Valid CSV data - Sensor: ");
                Serial.print(sensorId);
                Serial.print(", Time: ");
                Serial.print(timeStart);
                Serial.print(", Value: ");
                Serial.print(pressureValue);
                Serial.println(" hPa");
                
                // Store the pressure value
                pressureMeans[validMeans] = pressureValue;
                validMeans++;
              }
              else
              {
                Serial.print("Invalid data values - Sensor ID len: ");
                Serial.print(sensorId.length());
                Serial.print(", Time: ");
                Serial.print(timeStart);
                Serial.print(", Pressure: ");
                Serial.print(pressureValue);
                Serial.print(" - Line: ");
                Serial.println(line);
              }
            }
            else
            {
              Serial.print("Malformed CSV structure (wrong comma count) - Line: ");
              Serial.println(line);
            }
          }
        }
      }
      lineStart = i + 1;
    }
  }

  if (validMeans < 2)
  {
    Serial.println("Not enough time windows for trend calculation");
    return;
  }

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
  // Draw arrow in top-left corner (10x10 pixels)
  int centerX = 10;
  int centerY = 10;
  int arrowLength = 8;

  display.drawPixel(centerX, centerY, SSD1306_WHITE); // Center point

  switch (pressureTrend)
  {
  case 0: // Hard upward (straight up)
    display.drawLine(centerX, centerY, centerX, centerY - arrowLength, SSD1306_WHITE);
    display.drawLine(centerX, centerY - arrowLength, centerX - 2, centerY - arrowLength + 2, SSD1306_WHITE);
    display.drawLine(centerX, centerY - arrowLength, centerX + 2, centerY - arrowLength + 2, SSD1306_WHITE);
    break;

  case 1: // Slight upward (45° up)
    display.drawLine(centerX, centerY, centerX + 6, centerY - 6, SSD1306_WHITE);
    display.drawLine(centerX + 6, centerY - 6, centerX + 4, centerY - 4, SSD1306_WHITE);
    display.drawLine(centerX + 6, centerY - 6, centerX + 4, centerY - 8, SSD1306_WHITE);
    break;

  case 2: // No trend (horizontal)
    display.drawLine(centerX, centerY, centerX + arrowLength, centerY, SSD1306_WHITE);
    display.drawLine(centerX + arrowLength, centerY, centerX + arrowLength - 2, centerY - 1, SSD1306_WHITE);
    display.drawLine(centerX + arrowLength, centerY, centerX + arrowLength - 2, centerY + 1, SSD1306_WHITE);
    break;

  case 3: // Slight downward (135°)
    display.drawLine(centerX, centerY, centerX + 6, centerY + 6, SSD1306_WHITE);
    display.drawLine(centerX + 6, centerY + 6, centerX + 4, centerY + 4, SSD1306_WHITE);
    display.drawLine(centerX + 6, centerY + 6, centerX + 8, centerY + 4, SSD1306_WHITE);
    break;

  case 4: // Hard downward (straight down)
    display.drawLine(centerX, centerY, centerX, centerY + arrowLength, SSD1306_WHITE);
    display.drawLine(centerX, centerY + arrowLength, centerX - 2, centerY + arrowLength - 2, SSD1306_WHITE);
    display.drawLine(centerX, centerY + arrowLength, centerX + 2, centerY + arrowLength - 2, SSD1306_WHITE);
    break;
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