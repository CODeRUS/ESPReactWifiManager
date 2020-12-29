#pragma once

#include <Arduino.h>

class AsyncWebServer;
class AsyncWebServerRequest;
class ESPReactWifiManager
{
public:
    ESPReactWifiManager();

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
    void setHostname(String hostname);
    void setApOptions(String apName, String apPassword = String());
    void setStaOptions(String ssid, String password = String(), String login = String(), String bssid = String());
    bool connect();
    bool autoConnect();
    bool startAP();
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
