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

  char dateStr[11];
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
  Serial.println("Starting pressure trend calculation...");
  
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

  // Get pressure data from last 2 days
  String url = "/boxes/" + String(OSEM_BOX_ID) + "/data/" + String(SENSOR_ID_PRES) + "?format=json";
  
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
        Serial.print("Header: ");
        Serial.println(line);
        if (line == "\r")
        {
          headersPassed = true;
          Serial.println("Headers received, reading payload...");
        }
      }
      else
      {
        // Read payload character by character to avoid line break issues
        char c = client.read();
        if (c > 0) // Valid character
        {
          payload += c;
        }
      }
    }
  }
  client.stop();
  
  Serial.print("Payload received length: ");
  Serial.println(payload.length());

  if (payload.length() == 0)
  {
    Serial.println("No pressure data received");
    return;
  }
  
  // Clean up payload - remove any chunked encoding artifacts
  // Remove chunked encoding length markers (hex numbers on separate lines)
  String cleanedPayload = "";
  bool inChunkSize = false;
  
  for (unsigned int i = 0; i < payload.length(); i++)
  {
    char c = payload.charAt(i);
    
    // Skip chunked encoding size markers (hex digits followed by \r\n)
    if (c == '\r' || c == '\n')
    {
      inChunkSize = false;
      continue;
    }
    
    // Check if we're at the start of a chunk size (hex digits)
    if (!inChunkSize && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
    {
      // Look ahead to see if this is a chunk size line
      unsigned int endPos = i;
      while (endPos < payload.length() && payload.charAt(endPos) != '\r' && payload.charAt(endPos) != '\n')
      {
        endPos++;
      }
      
      // If the line only contains hex digits, it's likely a chunk size
      bool isChunkSize = true;
      for (unsigned int j = i; j < endPos; j++)
      {
        char ch = payload.charAt(j);
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')))
        {
          isChunkSize = false;
          break;
        }
      }
      
      if (isChunkSize && (endPos - i) <= 4) // Chunk sizes are typically short
      {
        inChunkSize = true;
        i = endPos - 1; // Skip to end of chunk size line
        continue;
      }
    }
    
    if (!inChunkSize)
    {
      cleanedPayload += c;
    }
  }
  
  payload = cleanedPayload;
  
  // Find the start of JSON array
  int jsonStart = payload.indexOf('[');
  if (jsonStart > 0)
  {
    payload = payload.substring(jsonStart);
    Serial.println("Cleaned JSON starting from '[' character");
  }
  
  // Find the end of JSON array and remove any trailing data
  int jsonEnd = payload.lastIndexOf(']');
  if (jsonEnd > 0 && jsonEnd < (int)payload.length() - 1)
  {
    payload = payload.substring(0, jsonEnd + 1);
    Serial.println("Cleaned JSON ending at ']' character");
  }
  
  Serial.print("Cleaned API Response length: ");
  Serial.println(payload.length());
  Serial.println("First 500 chars of cleaned response:");
  Serial.println(payload.substring(0, 500));  // Parse JSON and calculate trend
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  
  if (error)
  {
    Serial.print("JSON parsing failed: ");
    Serial.println(error.c_str());
    return;
  }

  JsonArray measurements = doc.as<JsonArray>();
  int numMeasurements = measurements.size();

  if (numMeasurements < 10)
  {
    Serial.println("Not enough data for trend calculation");
    return;
  }

  // Use last 20 measurements for trend calculation
  int dataPoints = min(20, numMeasurements);
  float pressureValues[20];
  float timeValues[20];

  // Extract pressure values - API returns newest first, so we need to reverse
  // Take the oldest measurements for trend calculation
  for (int i = 0; i < dataPoints; i++)
  {
    // OpenSenseMap returns data newest first, so reverse to get oldest first
    JsonObject measurement = measurements[numMeasurements - 1 - i];
    pressureValues[i] = measurement["value"].as<float>();

    // Use index as time (measurements are now chronological from oldest to newest)
    timeValues[i] = i;
    
    Serial.print("Data point ");
    Serial.print(i);
    Serial.print(" (time order): ");
    Serial.print(pressureValues[i]);
    Serial.println(" hPa");
  }  // Calculate linear regression slope
  float sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
  
  for (int i = 0; i < dataPoints; i++)
  {
    sumX += timeValues[i];
    sumY += pressureValues[i];
    sumXY += timeValues[i] * pressureValues[i];
    sumX2 += timeValues[i] * timeValues[i];
  }
  
  float denominator = dataPoints * sumX2 - sumX * sumX;
  if (abs(denominator) < 0.0001)
  {
    Serial.println("Cannot calculate trend - insufficient variance");
    return;
  }
  
  float slope = (dataPoints * sumXY - sumX * sumY) / denominator;

  // Convert slope to trend categories
  // Slope is in hPa per measurement interval (typically 10 minutes)
  // Scale to hPa per hour for interpretation
  float slopePerHour = slope * 6; // 6 measurements per hour (assuming 10-min intervals)

  Serial.print("Raw slope: ");
  Serial.print(slope, 6);
  Serial.print(" hPa/interval, Slope per hour: ");
  Serial.println(slopePerHour, 3);

  if (slopePerHour > 0.3)
  {
    pressureTrend = 0; // Hard upward trend
    Serial.println("-> Hard upward trend");
  }
  else if (slopePerHour > 0.1)
  {
    pressureTrend = 1; // Slight upward trend
    Serial.println("-> Slight upward trend");
  }
  else if (slopePerHour > -0.1)
  {
    pressureTrend = 2; // No significant trend
    Serial.println("-> No significant trend");
  }
  else if (slopePerHour > -0.3)
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
  Serial.print(slopePerHour);
  Serial.print(" hPa/hour, category: ");
  Serial.println(pressureTrend);
  
  // Debug: Print first and last pressure values (oldest to newest)
  Serial.print("Pressure range (oldest to newest): ");
  Serial.print(pressureValues[0]);
  Serial.print(" -> ");
  Serial.print(pressureValues[dataPoints - 1]);
  Serial.print(" hPa over ");
  Serial.print(dataPoints);
  Serial.println(" measurements");
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