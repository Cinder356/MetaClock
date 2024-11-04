#include <Arduino.h>
#include <RtcDS1302.h>
#include <LiquidCrystal_I2C.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include "Certs.h"
// #include <DHT.h>
// | tt° HH%    tt° |
// |dd.MM  hh:mm:ss |

#define DEBUG 0

// #define DHT11_PIN D3
#define BUTTON_PIN A0
// #define DHT_UPDATE_DELAY 20e3
#define TIME_CHECK_DELAY 200
#define BUTTON_PRESS_DELAY 300
#define MAX_WIFI_CONNECTION_TIME 30e3
#define DATA_SYNC_DELAY 60e3 * 30

const char *ssid = "RT-GPON-D470";
const char *password = "X4QngXMe";

const char *tempAPI = "https://api.open-meteo.com/v1/forecast?latitude=55.0529&longitude=74.5751&current=temperature_2m"; // by coordinates

X509List cert(IRG_Root_X1);

// DHT dht(DHT11_PIN, DHT11);
LiquidCrystal_I2C lcd(0x27, 16, 2);
ThreeWire myWire(D5, D0, D6); // IO, SCLK, CE
RtcDS1302<ThreeWire> rtc(myWire);

bool backlight = 1;

int8_t last_second = -1;
int8_t last_minute = -1;
uint32_t last_time_check = TIME_CHECK_DELAY;
uint32_t last_data_sync = 0;
uint32_t last_button_press = 0;
// uint32_t last_dht_update;

void checkUpdateTime()
{
  if (millis() > last_time_check + TIME_CHECK_DELAY)
  {
    last_time_check = millis();
    return;
  }
  RtcDateTime now = rtc.GetDateTime();
  if (last_second != now.Second())
  {
    last_second = now.Second();
    if (last_minute == now.Minute())
    {
      lcd.setCursor(13, 1);
      lcd.printf("%02d", now.Second());
      return;
    }
    last_minute = now.Minute();
    lcd.setCursor(0, 1);
    lcd.printf("%02d.%02d  %02d:%02d:%02d", now.Day(), now.Month(), now.Hour(), now.Minute(), now.Second());
  }
}

// void updateDHT()
// {
//   if (millis() > last_dht_update + DHT_UPDATE_DELAY)
//   {
//     float t = dht.readTemperature();
//     float h = dht.readHumidity();
//     Serial.print("temp: ");
//     Serial.println(t);
//     lcd.setCursor(1, 0);
//     lcd.printf("%02d* %02d%%", (int8_t)t, (int8_t)h);
//     last_dht_update = millis();
//   }
// }

void printErr(const char *errMessage)
{
  lcd.setCursor(0, 0);
  lcd.print(errMessage);
}

bool connectWithWiFi()
{
  // Connect to Wi-Fi
  WiFi.mode(WIFI_STA);
  if (!WiFi.begin(ssid, password))
    return 0;
#if DEBUG
  Serial.print("Connecting to WiFi ..");
#endif
  uint32_t startTime = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
#if DEBUG
    Serial.print('.');
#endif
    delay(500);
    if (millis() > MAX_WIFI_CONNECTION_TIME + startTime)
      return 0;
  }
  return 1;
}

void configESPTime()
{
  // Set time via NTP, as required for x.509 validation
  configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
#if DEBUG
  Serial.print("Waiting for NTP time sync: ");
#endif
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2)
  {
    delay(500);
#if DEBUG
    Serial.print(".");
#endif
    now = time(nullptr);
  }
#if DEBUG
  Serial.println("");
#endif
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
#if DEBUG
  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));
#endif
}

void syncTimeOnRTC()
{
  // rtc module time sync
  time_t now = time(nullptr);
  time_t local_now = now + 3600 * 3;
  struct tm currentTime;
  localtime_r(&local_now, &currentTime);
  RtcDateTime compiled = RtcDateTime(currentTime.tm_year, currentTime.tm_mon + 1, currentTime.tm_mday, currentTime.tm_hour, currentTime.tm_min, currentTime.tm_sec);
  rtc.SetDateTime(compiled);
  last_second = -1;
  last_minute = -1;
}

String getTempAPIResponse()
{
  // temperature get
  String payload;
  WiFiClientSecure client;
  client.setTrustAnchors(&cert);
  HTTPClient https;

#ifdef DEBUG
  Serial.print("[HTTPS] begin...\n");
#endif
  if (https.begin(client, tempAPI))
  {
#ifdef DEBUG
    Serial.print("[HTTPS] GET...\n");
#endif
    // start connection and send HTTP header
    int httpCode = https.GET();
    // httpCode will be negative on error
    if (httpCode > 0)
    {
#ifdef DEBUG
      // HTTP header has been send and Server response header has been handled
      Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
#endif
      // file found at server
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
      {
        payload = https.getString();
#ifdef DEBUG
        Serial.println(payload);
#endif
      }
    }
#ifdef DEBUG
    else
      Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
#endif
    https.end();
  }
#ifdef DEBUG
  else
    Serial.printf("[HTTPS] Unable to connect\n");
#endif
  return payload;
}

void updateTemp(float t)
{
  lcd.setCursor(10, 0);
  lcd.print(t, 1);
  lcd.write(223); // °
  lcd.print("C");
}

void syncData()
{
  if (!connectWithWiFi())
  {
    printErr("WiFi e");
    return;
  }
  configESPTime();
  syncTimeOnRTC();
  String response = getTempAPIResponse();
  if (response == NULL)
  {
    printErr("API e");
    return;
  }
  JsonDocument parsed;
  if (deserializeJson(parsed, response))
  {
    printErr("JSON e");
    return;
  }
  float t = parsed["current"]["temperature_2m"];
  updateTemp(t);
  WiFi.disconnect();
}

void setup()
{
#if DEBUG
  Serial.begin(115200);
#endif
  pinMode(BUTTON_PIN, INPUT);
  lcd.init();
  lcd.setBacklight(backlight);
  lcd.clear();
  rtc.Begin();
  // dht.begin();

  syncData();
}

void loop()
{
  checkUpdateTime();
  if (millis() > last_data_sync + DATA_SYNC_DELAY)
  {
    syncData();
    last_data_sync = millis();
  }
  if (analogRead(BUTTON_PIN) > 1000 && millis() > last_button_press + BUTTON_PRESS_DELAY)
  {
    backlight = !backlight;
    lcd.setBacklight(backlight);
    last_button_press = millis();
  }
}