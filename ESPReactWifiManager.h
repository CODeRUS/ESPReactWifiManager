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
    bool connect(String ssid, String password, String login);
    bool autoConnect();
    bool startAP(String apName);

    void setupHandlers(AsyncWebServer *server);
    void setFinishedCallback(void (*func)(bool)); // arg bool "is AP mode"
    void setNotFoundCallback(void (*func)(AsyncWebServerRequest*));

    void finishConnection(bool apMode);
    void scheduleScan(int timeout = 2000);
    bool scan();
    int size();
    std::vector<WifiResult> results();

};
