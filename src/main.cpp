// Optimized senseBox ESP8266 sketch - low power mode with 1min updates
#include <ESP8266WiFi.h>
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
  disconnectWiFi();
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

  if (now - lastUpload >= 3600000)
  {
    connectWiFi();
    uploadToOSeM();
    lastUpload = now;
  }

  if (now - lastTimeSync >= 3600000)
  {
    syncTime();
    disconnectWiFi();
    lastTimeSync = now;
  }

  delay(5000);
}