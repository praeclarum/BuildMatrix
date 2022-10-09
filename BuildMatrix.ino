#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
//#include <WiFiClientSecureBearSSL.h>
#include <Adafruit_GFX.h>
#include <FastLED_NeoMatrix.h>
#include <FastLED.h>
#include <esp_log.h>
#include <ArduinoJson.h>
#include <vector>
#include <fauxmoESP.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include "Secrets.h"

using namespace std;

class App
{
public:
  String name;
  String status;
  String message;
  uint16_t color;
  App()
    : color(0xFFFFu)
  {
  }
};

fauxmoESP fauxmo;

#define ALEXA_ID            "Matrix"

#define SERIAL_BAUDRATE     115200

#define MAX_BRIGHTNESS 64
#define MIN_BRIGHTNESS 4

bool isOn = true;
uint8_t brightness = MAX_BRIGHTNESS;

vector<App> apps;

#define UPDATE_APPS_MILLIS  (5*60*1000)

const char PAT[] = BITRISE_PAT;

WiFiMulti WiFiMulti;

#define MATRIX_PIN    12
#define MATRIX_WIDTH  32
#define MATRIX_HEIGHT 8
#define NUMMATRIX     (MATRIX_WIDTH*MATRIX_HEIGHT)

CRGB matrixleds[NUMMATRIX];
FastLED_NeoMatrix matrix = FastLED_NeoMatrix(
  matrixleds,
  MATRIX_WIDTH, MATRIX_HEIGHT,
  //MATRIX_PIN,
  NEO_MATRIX_TOP + NEO_MATRIX_LEFT + NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG);

inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
  if (!isOn)
    return 0;
  uint16_t sr = (r * brightness) / 256;
  uint16_t sg = (g * brightness) / 256;
  uint16_t sb = (b * brightness) / 256;
  return ((sr >> 3) << 11) | ((sg >> 2) << 5) | ((sb >> 3) << 0);
}

void setClock() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print(F("Waiting for NTP time sync: "));
  time_t nowSecs = time(nullptr);
  while (nowSecs < 8 * 3600 * 2) {
    delay(500);
    Serial.print(F("."));
    yield();
    nowSecs = time(nullptr);
  }
  Serial.println();
  struct tm timeinfo;
  gmtime_r(&nowSecs, &timeinfo);
  Serial.print(F("Current time: "));
  Serial.print(asctime(&timeinfo));
}

String get(const String &path)
{
  WiFiClientSecure client;//new BearSSL::WiFiClientSecure);
  client.setInsecure();
  HTTPClient https;  
  auto url = "https://ninua.azurewebsites.net" + path;
  Serial.printf("[HTTPS] GET %s\n", url.c_str()); 
  if (https.begin(client, url)) {  // HTTPS
//    https.addHeader("accept", "application/json");
    int httpCode = https.GET();
    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        return https.getString();        
      }
    } else {
      Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
    }
    https.end();
  } else {
    Serial.printf("[HTTPS] Unable to connect\n");
  }
  return "";
}

void displayStatus(const String &message)
{
//  matrix.clear();
  matrix.setTextWrap(false);  // we don't wrap text so it scrolls nicely
  matrix.setTextSize(1);
  matrix.setRotation(0);

  uint16_t color = rgb(128, 128, 128);
//  matrix.setScale();
  matrix.clear();
  matrix.setTextColor(color);
  matrix.setCursor(0,0);
  matrix.print(message);
  matrix.show();
}

void getApps()
{
  auto json = get("/library/builds.json");
//  Serial.println(json);

  DynamicJsonDocument doc(10000);
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    Serial.print(F("getApps deserializeJson() failed: "));
    Serial.println(error.c_str());
    return;
  }
  
  apps.clear ();
  const char *title = doc["data"][0]["name"];
  auto i = 0;
  while (title) {
    App app;
    app.name = title;
    app.status = (const char *)doc["data"][i]["status"];
    const char *colorText = (const char *)doc["data"][i]["color"];
    if (strcasecmp(colorText, "red") == 0) {
      app.color = rgb(255, 0, 0);
    }
    else if (strcasecmp(colorText, "green") == 0) {
      app.color = rgb(0, 255, 0);
    }
    else if (strcasecmp(colorText, "blue") == 0) {
      app.color = rgb(0, 0, 255);
    }
    else if (strcasecmp(colorText, "yellow") == 0) {
      app.color = rgb(255, 255, 0);
    }
    else if (strcasecmp(colorText, "white") == 0) {
      app.color = rgb(255, 255, 255);
    }
    else if (strcasecmp(colorText, "gray") == 0) {
      app.color = rgb(128, 128, 128);
    }
    else if (strcasecmp(colorText, "black") == 0) {
      app.color = rgb(0, 0, 0);
    }
    else {
      app.color = rgb(128, 128, 128);
    }
    apps.push_back(app);
    Serial.printf("%s = %s\n", app.name.c_str(), app.status.c_str());
    
    i++;
    title = doc["data"][i]["name"];
  }
}

const int backgroundCanvasScale = 1;
GFXcanvas16 backgroundCanvas(MATRIX_WIDTH*backgroundCanvasScale, MATRIX_HEIGHT*backgroundCanvasScale);
GFXcanvas16 backgroundCanvasScaled(MATRIX_WIDTH, MATRIX_HEIGHT);

void scaleBackground()
{
  auto sb = backgroundCanvas.getBuffer();
  auto sw = backgroundCanvas.width();
  auto sh = backgroundCanvas.height();
//  auto db = backgroundCanvasScaled.getBuffer();
  auto dw = backgroundCanvasScaled.width();
  auto dh = backgroundCanvasScaled.height();

  const int s = backgroundCanvasScale;

  backgroundCanvasScaled.startWrite();
  for (int16_t y=0; y<dh; y++) {
    for (int16_t x=0; x<dw; x++) {
      // x + y * WIDTH
      uint16_t red = 0;
      uint16_t green = 0;
      uint16_t blue = 0;
      for (int16_t j=0; j<s; j++) {
        for (int16_t i=0; i<s; i++) {
          auto color = sb[(s*x+i) + (s*y+j)*sw];
          red += (color >> 11);
          green += (color >> 5) & 0x3F;
          blue += color & 0x1F;
        }
      }
      auto d = s*s;
      red /= d;
      green /= d;
      blue /= d;
      auto color = (red << 11) | (green << 5) | (blue);
      backgroundCanvasScaled.writePixel(x, y, color);
    }
  }
  backgroundCanvasScaled.endWrite();
}

void displayApp(App &app)
{
  String message = app.status + " " + app.name;

  auto *g = &backgroundCanvas;
  
//  matrix.clear();
  g->setTextWrap(false);  // we don't wrap text so it scrolls nicely
  g->setTextSize(1);
  g->setRotation(0);

  g->fillRect(           0,             0, MATRIX_WIDTH, MATRIX_HEIGHT, rgb(128, 128,   0));
  g->fillRect(           0, MATRIX_HEIGHT, MATRIX_WIDTH, MATRIX_HEIGHT, rgb(128, 128, 128));
  g->fillRect(MATRIX_WIDTH,             0, MATRIX_WIDTH, MATRIX_HEIGHT, rgb(  0, 128, 128));
  g->fillRect(MATRIX_WIDTH, MATRIX_HEIGHT, MATRIX_WIDTH, MATRIX_HEIGHT, rgb(128,   0, 128));

  uint16_t color = app.color;
//  {
//    size_t endMessageIndex = 0;
//    while (endMessageIndex < app.buildCommitMessage.length()) {
//      if (app.buildCommitMessage[endMessageIndex] == '\n')
//        break;
//      endMessageIndex++;
//    }
//    if (app.buildCommitMessage.length() > 0) {
//      message += ": " + app.buildCommitMessage.substring(0, endMessageIndex);
//    }
//  }
  int width = (int)(message.length() * 6);
  for (int x=MATRIX_WIDTH; x>=-(width - MATRIX_WIDTH) && isOn; x--) {
    g->fillScreen(0);
    g->setTextColor(color);
    g->setCursor(x,0);
    g->print(message);

    scaleBackground();
    matrix.drawRGBBitmap(0, 0, backgroundCanvasScaled.getBuffer(), backgroundCanvasScaled.width(), backgroundCanvasScaled.height());
    matrix.show();

    delay(37);
  }

  if (!isOn) {
    matrix.clear();
    matrix.show();
  }
}

void setupAlexa()
{
    fauxmo.createServer(true); // not needed, this is the default value
    fauxmo.setPort(80); // This is required for gen3 devices
    fauxmo.enable(true);
    fauxmo.addDevice(ALEXA_ID);
    fauxmo.onSetState([](unsigned char device_id, const char * device_name, bool state, unsigned char value) {
      Serial.printf("[ALEXA] Device #%d (%s) state: %s value: %d\n", device_id, device_name, state ? "ON" : "OFF", value);
      if (strcmp(device_name, ALEXA_ID)==0) {
        brightness = (value * MAX_BRIGHTNESS) / 256;
        isOn = state;
        if (isOn && brightness < MIN_BRIGHTNESS) {
          brightness = MIN_BRIGHTNESS;
        }
        Serial.printf("IsOn = %d, Brightness = %d", isOn?1:0, brightness);
      }
    });
    fauxmo.setState(ALEXA_ID, true, (255 * brightness) / MAX_BRIGHTNESS);
}

#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif

void setup() {
//  esp_log_level_set("*", ESP_LOG_DEBUG);
  Serial.begin(115200);
//  Serial.setDebugOutput(true);

  FastLED.addLeds<NEOPIXEL,MATRIX_PIN>(matrixleds, NUMMATRIX); 
  matrix.begin();
  displayStatus("WiFi");

  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP(WIFI_SSID, WIFI_PASS);

  // wait for WiFi connection
  Serial.print("Waiting for WiFi to connect...");
  while ((WiFiMulti.run() != WL_CONNECTED)) {
    Serial.print(".");
  }
  Serial.println(" connected");

  displayStatus("Update");

  ArduinoOTA.setHostname(ALEXA_ID);

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();

  displayStatus("Clock");
  
  setClock();

  displayStatus("Alexa");

  setupAlexa();

  displayStatus("Load");

//  Scheduler.startLoop(loopBackground);
  xTaskCreatePinnedToCore(loopBackground, "loopBackground", 4096, NULL, 1, NULL, ARDUINO_RUNNING_CORE);
}

void loopBackground(void *)
{
//  Serial.println("\n\n\n\n\nFAUXMOOOOO\n\n\n\n\n");
  for (;;) {
    fauxmo.handle();
    ArduinoOTA.handle();
  }
}

unsigned long lastUpdateMillis = 0;

void loop()
{
  auto needsUpdate = false;
  auto nowMillis = millis();
  
  if (isOn && (lastUpdateMillis == 0 || (millis() - lastUpdateMillis) > UPDATE_APPS_MILLIS)) {
    needsUpdate = true;
    lastUpdateMillis = millis();
    getApps();
  }
  
  for (size_t i = 0; isOn && i < apps.size(); i++) {
    displayApp(apps[i]);
    delay(1000);
  }
}
