#pragma once

#include <Arduino.h>

class AsyncWebServer;
class AsyncWebServerRequest;
class ESPReactWifiManager
{
public:
    ESPReactWifiManager(const String &hostname = String());

    struct WifiResult {
        String ssid;
        uint8_t encryptionType;
        int32_t rssi;
        uint8_t* bssid;
        int32_t channel;
        int quality;
        bool isHidden = false;
        bool duplicate = false;
    };

    void loop();

    void disconnect();
    bool connect(String ssid, String password, String login);
    bool autoConnect(String apName);
    bool startAP(String apName);
    void setFallbackToAp(bool enable);

    void setupHandlers(AsyncWebServer *server);
    void onFinished(void (*func)(bool)); // arg bool "is AP mode"
    void onNotFound(void (*func)(AsyncWebServerRequest*));
    void onCaptiveRedirect(bool (*func)(AsyncWebServerRequest*));

    void finishConnection(bool apMode);
    void scheduleScan(int timeout = 2000);
    bool scan();
    int size();
    std::vector<WifiResult> results();

};
