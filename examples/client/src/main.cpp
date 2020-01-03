#include <Arduino.h>
#include <ESPReactWifiManager.h>
#include <ESPAsyncWebServer.h>

namespace {

AsyncWebServer *server = nullptr;
ESPReactWifiManager *wifiManager = nullptr;

} // namespace

void setup()
{
    delay(1000);

    Serial.begin(115200);
    Serial.println(F("\nHappy debugging!"));
    Serial.flush();

    if (!SPIFFS.begin()) {
        Serial.println(F("An Error has occurred while mounting SPIFFS"));
        return;
    }

#if defined(ESP8266)
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
#endif

    server = new AsyncWebServer(80);
    server->serveStatic(PSTR("/static/js/"), SPIFFS, PSTR("/"))
        .setCacheControl(PSTR("max-age=86400"));
    server->serveStatic(PSTR("/static/css/"), SPIFFS, PSTR("/"))
        .setCacheControl(PSTR("max-age=86400"));
    server->serveStatic(PSTR("/"), SPIFFS, PSTR("/"))
        .setCacheControl(PSTR("max-age=86400"))
        .setDefaultFile(PSTR("wifi.html"));

    wifiManager = new ESPReactWifiManager();
    wifiManager->onFinished([](bool isAPMode) {
        server->begin();
    });
    wifiManager->onNotFound([](AsyncWebServerRequest* request) {
        request->send(SPIFFS, F("wifi.html"));
    });
    wifiManager->setupHandlers(server);
    wifiManager->autoConnect(F("REACT"));
}

void loop()
{
    wifiManager->loop();
}