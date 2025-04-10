// Optimized senseBox ESP8266 sketch - low power mode with 1min updates
#include <ESP8266WiFi.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <time.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

Adafruit_BMP280 bmp;

#include "secrets.h"
#define HOST "ingress.opensensemap.org"

unsigned long lastSensorRead = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastUpload = 0;
unsigned long lastTimeSync = 0;

bool bmpOk = false;
float currentTemp = 0.0;
float currentPres = 0.0;

void connectWiFi();
void disconnectWiFi();
void syncTime();
void uploadToOSeM();
void postCombinedValues(float temp, float pres);
void showBootScreen();
void showError(const char* msg);
bool isNight();
void updateSensor();
void updateDisplay();

void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
  } else {
    Serial.println("\nWiFi failed");
  }
}

void disconnectWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
}

void syncTime() {
  configTime(3600, 3600, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  time_t now = time(nullptr);
  while (now < 100000) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("\nTime synced");
}

void uploadToOSeM() {
  if (!bmpOk) return;
  postCombinedValues(currentTemp, currentPres);
  disconnectWiFi();
}

void postCombinedValues(float temp, float pres) {
  WiFiClient client;
  if (!client.connect(HOST, 80)) {
    Serial.println("Connection to OSeM failed");
    return;
  }

  String json = "[";
  json += "{\"sensor\":\"" + String(SENSOR_ID_TEMP) + "\",\"value\":\"" + String(temp, 2) + "\"},";
  json += "{\"sensor\":\"" + String(SENSOR_ID_PRES) + "\",\"value\":\"" + String(pres, 2) + "\"}";
  json += "]";

  client.print(String("POST /boxes/") + OSEM_BOX_ID + "/data HTTP/1.1\r\n" +
               "Host: " + HOST + "\r\n" +
               "Authorization: " + OSEM_AUTH + "\r\n" +
               "Content-Type: application/json\r\n" +
               "Connection: close\r\n" +
               "Content-Length: " + json.length() + "\r\n\r\n" +
               json);

  while (client.connected()) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      Serial.println(line);
    }
  }
  client.stop();
}

void showBootScreen() {
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

void showError(const char* msg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(10, 28);
  display.println(msg);
  display.display();
}

bool isNight() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  return (t->tm_hour >= 22 || t->tm_hour < 8);
}

void updateSensor() {
  if (!bmpOk) return;
  currentTemp = bmp.readTemperature() - 4.0; // Adjusted for calibration
  currentPres = bmp.readPressure() / 100.0F;
}

void updateDisplay() {
  if (isNight()) {
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    return;
  } else {
    display.ssd1306_command(SSD1306_DISPLAYON);
  }

  time_t now = time(nullptr);
  struct tm* t = localtime(&now);

  char dateStr[11];
  snprintf(dateStr, sizeof(dateStr), "%02d.%02d.%04d", t->tm_mday, t->tm_mon + 1, 1900 + t->tm_year);

  char timeStr[6];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", t->tm_hour, t->tm_min);

  char tempStr[16];
  snprintf(tempStr, sizeof(tempStr), "%.2f C", currentTemp);

  char presStr[16];
  snprintf(presStr, sizeof(presStr), "%.2f hPa", currentPres);

  display.clearDisplay();
  display.setTextSize(1);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(dateStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 0);
  display.println(dateStr);

  display.setTextSize(2);
  display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 12);
  display.println(timeStr);

  display.setTextSize(1);
  display.setCursor((SCREEN_WIDTH - strlen(tempStr) * 6) / 2, 38);
  display.println(tempStr);
  display.setCursor((SCREEN_WIDTH - strlen(presStr) * 6) / 2, 50);
  display.println(presStr);

  display.display();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(2, 14);
  delay(100);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED failed");
    while (1);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.display();
  showBootScreen();

  if (!bmp.begin(0x76)) {
    if (!bmp.begin(0x77)) {
      showError("BMP280 MISSING");
      return;
    }
  }
  bmpOk = true;

  connectWiFi();
  syncTime();
  disconnectWiFi();

  lastSensorRead = millis();
  lastDisplayUpdate = millis();
  lastUpload = millis();
  lastTimeSync = millis();

  updateSensor();
  updateDisplay();
}

void loop() {
  unsigned long now = millis();

  if (now - lastSensorRead >= 60000) {
    updateSensor();
    lastSensorRead = now;
  }

  if (now - lastDisplayUpdate >= 60000) {
    updateDisplay();
    lastDisplayUpdate = now;
  }

  if (now - lastUpload >= 3600000) {
    connectWiFi();
    uploadToOSeM();
    lastUpload = now;
  }

  if (now - lastTimeSync >= 3600000) {
    syncTime();
    disconnectWiFi();
    lastTimeSync = now;
  }

  delay(5000);
}