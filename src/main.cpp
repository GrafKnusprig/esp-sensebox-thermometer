// senseBox sketch with BMP280, OLED display, combined OSeM upload, and stable visuals
#include <ESP8266WiFi.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <time.h>
#include "secrets.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

Adafruit_BMP280 bmp;

const char* host = "ingress.opensensemap.org";
const char* senseBoxId = OSEM_BOX_ID;
const char* sensorIdTemp = SENSOR_ID_TEMP;
const char* sensorIdPres = SENSOR_ID_PRES;
const char* authToken = OSEM_AUTH;

unsigned long lastDisplayUpdate = 0;
unsigned long lastUpload = 0;
unsigned long lastClockUpdate = 0;

bool bmpOk = false;
bool wifiOk = false;

float currentTemp = 0.0;
float currentPres = 0.0;

void updateClock();
void uploadToOSeM();
void postCombinedValues(float temp, float pres);
void showError(const char* message);
void syncTime();
void showBootScreen();
void showNoConnectionScreen();

void setup() {
  Serial.begin(115200);

  Wire.begin(2, 14);
  delay(100); // allow sensor to power up

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (true);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.display();

  showBootScreen();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.setPhyMode(WIFI_PHY_MODE_11N);
  Serial.print("Connecting to WiFi");
  int wifiTries = 0;
  while (WiFi.status() != WL_CONNECTED && wifiTries < 30) {
    delay(500);
    Serial.print(".");
    wifiTries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
    wifiOk = true;
    syncTime();
  } else {
    Serial.println("\nWiFi connection failed");
  }

  if (!bmp.begin(0x76)) {
    if (!bmp.begin(0x77)) {
      Serial.println("BMP not found on 0x76 or 0x77");
      showError("BMP280 MISSING");
    }
  } else {
    bmpOk = true;
  }

  if (bmpOk) {
    currentTemp = bmp.readTemperature() - 6.0;
    currentPres = bmp.readPressure() / 100.0F;
  }
}

void loop() {
  unsigned long now = millis();

  if (bmpOk && (now - lastDisplayUpdate > 5000 || lastDisplayUpdate == 0)) {
    currentTemp = bmp.readTemperature() - 6.0;
    currentPres = bmp.readPressure() / 100.0F;
    lastDisplayUpdate = now;
  }

  if (bmpOk && wifiOk && (now - lastUpload > 3600000 || lastUpload == 0)) {
    postCombinedValues(currentTemp, currentPres);
    lastUpload = now;
  }

  if (now - lastClockUpdate >= 1000) {
    updateClock();
    lastClockUpdate = now;
  }
}

void updateClock() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);

  char dateStr[11];
  snprintf(dateStr, sizeof(dateStr), "%02d.%02d.%04d", timeinfo->tm_mday, timeinfo->tm_mon + 1, 1900 + timeinfo->tm_year);

  char timeStr[9];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

  display.clearDisplay();

  if (!wifiOk) {
    showNoConnectionScreen();
    return;
  }

  display.setTextSize(1);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(dateStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 0);
  display.println(dateStr);

  display.setTextSize(2);
  display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 10);
  display.println(timeStr);

  char tempStr[16];
  snprintf(tempStr, sizeof(tempStr), "%.2f C", currentTemp);
  display.getTextBounds(tempStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 34);
  display.println(tempStr);

  char presStr[16];
  snprintf(presStr, sizeof(presStr), "%.2f hPa", currentPres);
  display.getTextBounds(presStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 50);
  display.println(presStr);

  display.display();
}

void postCombinedValues(float temp, float pres) {
  WiFiClient client;
  if (!client.connect(host, 80)) {
    Serial.println("Connection to OSeM failed");
    return;
  }

  String json = "[";
  json += "{\"sensor\":\"" + String(sensorIdTemp) + "\",\"value\":\"" + String(temp, 2) + "\"},";
  json += "{\"sensor\":\"" + String(sensorIdPres) + "\",\"value\":\"" + String(pres, 2) + "\"}";
  json += "]";

  client.print(String("POST /boxes/") + senseBoxId + "/data HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Authorization: " + authToken + "\r\n" +
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
  Serial.println("Combined upload done");
  client.stop();
}

void showError(const char* message) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 20);
  display.println(message);
  display.display();
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

void showNoConnectionScreen() {
  display.clearDisplay();
  display.setTextSize(2);

  char tempStr[16];
  snprintf(tempStr, sizeof(tempStr), "%.2f C", currentTemp);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(tempStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 10);
  display.println(tempStr);

  char presStr[16];
  snprintf(presStr, sizeof(presStr), "%.2f hPa", currentPres);
  display.getTextBounds(presStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 30);
  display.println(presStr);

  display.setTextSize(1);
  display.setCursor(20, 54);
  display.println("No connection");
  display.display();
}

void syncTime() {
  configTime(3600, 3600, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  Serial.print("Waiting for time sync");
  time_t now = time(nullptr);
  while (now < 100000) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("\nTime synced");
}