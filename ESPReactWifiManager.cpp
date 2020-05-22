#include <ESPReactWifiManager.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <FS.h>

extern "C" {
#include <user_interface.h>
#include <wpa2_enterprise.h>
}

typedef int wifi_ssid_count_t;
typedef uint8 wifi_cred_t;
typedef struct station_config sta_config_t;
#define esp_wifi_sta_wpa2_ent_set_identity wifi_station_set_enterprise_identity
#define esp_wifi_sta_wpa2_ent_set_username wifi_station_set_enterprise_username
#define esp_wifi_sta_wpa2_ent_set_password wifi_station_set_enterprise_password
#define ENCRYPTION_NONE ENC_TYPE_NONE
#define ENCRYPTION_ENT 255

#else
#include <WiFi.h>
#include <SPIFFS.h>
#include <esp_wifi.h>
#include <esp_wpa2.h>

typedef int16_t wifi_ssid_count_t;
typedef unsigned char wifi_cred_t;
typedef wifi_sta_config_t sta_config_t;
#define ENCRYPTION_NONE WIFI_AUTH_OPEN
#define ENCRYPTION_ENT WIFI_AUTH_WPA2_ENTERPRISE
#endif

#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <algorithm>
#include <memory>

#define ARDUINOJSON_ENABLE_PROGMEM 1
#include <ArduinoJson.h>
#include <AsyncJson.h>

namespace {

bool isConnecting = false;
bool fallbackToAp = true;

uint32_t connectingTimer = 0;
uint32_t connectedCheckTimeout = 1000;

uint32_t shouldScan = 0;

DNSServer* dnsServer = nullptr;
void (*finishedCallback)(bool) = nullptr;
void (*notFoundCallback)(AsyncWebServerRequest*) = nullptr;

String wifiHostname;

std::vector<ESPReactWifiManager::WifiResult> wifiResults;
int wifiIndex = 0;

bool signalLess(const ESPReactWifiManager::WifiResult& a,
                const ESPReactWifiManager::WifiResult& b)
{
    return a.rssi > b.rssi;
}

bool ssidEqual(const ESPReactWifiManager::WifiResult& a,
               const ESPReactWifiManager::WifiResult& b)
{
    return a.ssid == b.ssid;
}

bool ssidLess(const ESPReactWifiManager::WifiResult& a,
              const ESPReactWifiManager::WifiResult& b)
{
    return a.ssid == b.ssid ? signalLess(a, b) : a.ssid < b.ssid;
}

void notFoundHandler(AsyncWebServerRequest* request)
{
    if (request->url().endsWith(F(".map"))) {
        request->send(404);
        return;
    }

    bool isLocal = WiFi.localIP() == request->client()->localIP();
    
    if (!isLocal) {
        Serial.print(F("Request redirected to captive portal: "));
        Serial.println(request->url());

        request->redirect(String(F("http://"))
            + request->client()->localIP().toString()
            + String(F("/wifi.html")));
        return;
    }

    if (notFoundCallback) {
        notFoundCallback(request);
    }
}

}

ESPReactWifiManager::ESPReactWifiManager(const String &hostname)
{
    wifiHostname = hostname;
}

void ESPReactWifiManager::loop()
{
    if (dnsServer) {
        dnsServer->processNextRequest();
    }

    uint32_t now = millis();

    if (shouldScan > 0 && now > shouldScan) {
        shouldScan = 0;

        scan();
    }

    if (isConnecting && (now - connectingTimer) > connectedCheckTimeout) {
        connectingTimer = now;
        wl_status_t status = WiFi.status();
        switch (status) {
        case WL_CONNECTED:
            isConnecting = false;
            Serial.print(F("\nLocal ip: "));
            Serial.println(WiFi.localIP());
            finishConnection(false);
            break;
        case WL_CONNECT_FAILED:
        case WL_NO_SSID_AVAIL:
            if (fallbackToAp) {
#if defined(ESP32)
                WiFi.disconnect(false, true);
#else
                WiFi.disconnect();
#endif
            }
            isConnecting = false;
            Serial.println(F("Connection failed!"));
            ESP.restart();
            break;
        case WL_DISCONNECTED:
            Serial.print(".");
            break;
        case WL_IDLE_STATUS:
            // Serial.println(F("Connection idle!"));
            break;
        case WL_CONNECTION_LOST:
            Serial.println(F("Connection lost! Retrying."));
            break;
        case WL_NO_SHIELD:
            break;
        default:
            Serial.print(F("Unhandled status during connect: "));
            Serial.println(status);
            break;
        }
    }
}

void ESPReactWifiManager::disconnect()
{
#if defined(ESP8266)
    //trying to fix connection in progress hanging
    ETS_UART_INTR_DISABLE();
    wifi_station_disconnect();
    ETS_UART_INTR_ENABLE();
#else
    WiFi.disconnect(false);
#endif
}

bool ESPReactWifiManager::connect(String ssid, String password, String login)
{
    Serial.println();
    disconnect();

    isConnecting = true;
    WiFi.mode(WIFI_STA);
    if (!wifiHostname.isEmpty()) {
#if defined(ESP8266)
        WiFi.hostname(wifiHostname.c_str());
#else
        WiFi.setHostname(wifiHostname.c_str());
#endif
    }

    if (ssid.length() == 0) {
        sta_config_t sta_conf;
#if defined(ESP32)
        wifi_config_t current_conf;
        esp_wifi_get_config(WIFI_IF_STA, &current_conf);
        sta_conf = current_conf.sta;
#else
        wifi_station_get_config_default(&sta_conf);
#endif
        ssid = String(reinterpret_cast<const char*>(sta_conf.ssid));

        if (ssid.length() == 0) {
            return false;
        }

        String savedPassword = String(reinterpret_cast<const char*>(sta_conf.password));

        if (savedPassword.startsWith(F("x:"))) {
            int passwordIndex = savedPassword.indexOf(F(":"), 2);
            password = savedPassword.substring(passwordIndex + 1);
            login = savedPassword.substring(2, passwordIndex);
        } else {
            password = savedPassword;
        }

        if (ssid.length() > 0) {
            Serial.println(F("Connecting to last saved network"));
        } else {
            Serial.println(F("No last saved network"));
            return false;
        }
    }

    if (login.length() == 0) {
        Serial.print(F("Connecting to network: "));
        WiFi.begin(ssid.c_str(), password.c_str());
    } else {
        Serial.print(F("Connecting to secure network: "));

        String save_password = F("x:");
        save_password += login;
        save_password += F(":");
        save_password += password;
#if defined(ESP32)
        esp_wpa2_config_t config = WPA2_CONFIG_INIT_DEFAULT();
        esp_wifi_sta_wpa2_ent_enable(&config);
#else
        wifi_station_set_wpa2_enterprise_auth(1);
#endif
        esp_wifi_sta_wpa2_ent_set_identity((wifi_cred_t*)login.c_str(), login.length());
        esp_wifi_sta_wpa2_ent_set_username((wifi_cred_t*)login.c_str(), login.length());
        esp_wifi_sta_wpa2_ent_set_password((wifi_cred_t*)password.c_str(), password.length());
        WiFi.begin(ssid.c_str(), save_password.c_str());
    }
    Serial.println(ssid);

    return true;
}

bool ESPReactWifiManager::autoConnect(String apName)
{
    return connect(String(), String(), String()) || startAP(apName);
}

void ESPReactWifiManager::setFallbackToAp(bool enable)
{
    fallbackToAp = enable;
}

bool ESPReactWifiManager::startAP(String apName)
{
    Serial.println();
    disconnect();

    bool success = WiFi.mode(WIFI_AP_STA);
    if (!success) {
        Serial.println(F("Error changing mode to AP"));
        ESP.restart();
        return false;
    }
    success = WiFi.softAPConfig(IPAddress(8, 8, 8, 8), IPAddress(8, 8, 8, 8),
        IPAddress(255, 255, 255, 0));
    if (!success) {
        Serial.println(F("Error setting static IP for AP mode"));
        ESP.restart();
        return false;
    }
    success = WiFi.softAP(apName.c_str());
    if (!success) {
        Serial.println(F("Error starting AP"));
        ESP.restart();
        return false;
    }

    delay(500); // Without delay I've seen the IP address blank
    Serial.println(F("AP IP address: "));
    Serial.println(WiFi.softAPIP());

    finishConnection(true);

    return true;
}


void ESPReactWifiManager::setupHandlers(AsyncWebServer *server)
{
    if (!server) {
        Serial.println(F("WebServer is null!"));
        return;
    }

    server->on(PSTR("/wifiSave"), HTTP_POST, [this](AsyncWebServerRequest* request) {
        Serial.println("wifiSave request");

        String login;
        String password;
        String ssid;

        String message;

        for (uint8_t i = 0; i < request->args(); i++) {
            if (request->argName(i) == F("login")) {
                login = request->arg(i);
            }
            if (request->argName(i) == F("password")) {
                password = request->arg(i);
            }
            if (request->argName(i) == F("ssid")) {
                ssid = request->arg(i);
            }
        }
        if (ssid.length() > 0) {
            message = F("Connect to: ");
            message += ssid;
            message += F(" after module reboot");

            connect(ssid, password, login);
        } else {
            message = F("Wrong request. No ssid");
        }
        request->send(200, F("text/html"), message);
    });

    server->on(PSTR("/wifiList"), HTTP_GET, [](AsyncWebServerRequest* request) {
        wifiIndex = 0;
        Serial.printf_P(PSTR("wifiList count: %zu\n"), wifiResults.size());
        AsyncWebServerResponse* response = request->beginChunkedResponse(
            F("application/json"),
            [](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
                if (index == 0) {
                    buffer[0] = '[';
                    return 1;
                } else if (wifiIndex >= wifiResults.size()) {
                    return 0;
                } else {
                    String security;
                    if (wifiResults[wifiIndex].encryptionType == ENCRYPTION_NONE) {
                        security = F("none");
                    } else if (wifiResults[wifiIndex].encryptionType == ENCRYPTION_ENT) {
                        security = F("WPA2");
                    } else {
                        security = F("WEP");
                    }
                    const size_t capacity = JSON_OBJECT_SIZE(3) + 31 // fields length
                                            + security.length()
                                            + wifiResults[wifiIndex].ssid.length();
                    DynamicJsonDocument doc(capacity);
                    JsonObject obj = doc.to<JsonObject>();
                    obj[F("ssid")] = wifiResults[wifiIndex].ssid;
                    obj[F("signalStrength")] = wifiResults[wifiIndex].quality;
                    obj[F("security")] = security;
                    size_t len = serializeJson(doc, (char*)buffer, maxLen);
                    if ((wifiIndex + 1) == wifiResults.size()) {
                        buffer[len] = ']';
                    } else {
                        buffer[len] = ',';
                    }
                    ++len;
                    ++wifiIndex;
                    return len;
                }
            });
        request->send(response);
    });

    server->onNotFound(notFoundHandler);
}

void ESPReactWifiManager::onFinished(void (*func)(bool))
{
    finishedCallback = func;
}

void ESPReactWifiManager::onNotFound(void (*func)(AsyncWebServerRequest*))
{
    notFoundCallback = func;
}

void ESPReactWifiManager::finishConnection(bool apMode)
{
    if (!dnsServer && apMode) {
        dnsServer = new DNSServer();
        dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
        bool dnsOk = dnsServer->start(53, F("*"), WiFi.softAPIP());
        Serial.printf_P(PSTR("Starting DNS server: %s\n"), dnsOk ? PSTR("success") : PSTR("fail"));
    } else if (dnsServer && !apMode) {
        Serial.println(F("Stopping DNS server"));
        dnsServer->stop();
        delete dnsServer;
        dnsServer = nullptr;
    } 

    scheduleScan();

    if (finishedCallback) {
        finishedCallback(apMode);
    }
}

void ESPReactWifiManager::scheduleScan(int timeout)
{
    shouldScan = millis() + timeout;
}

bool ESPReactWifiManager::scan()
{
    wifi_ssid_count_t n = WiFi.scanNetworks();
    Serial.println(F("Scan done"));
    if (n == WIFI_SCAN_FAILED) {
        Serial.println(F("scanNetworks returned: WIFI_SCAN_FAILED!"));
        return false;
    } else if (n == WIFI_SCAN_RUNNING) {
        Serial.println(F("scanNetworks returned: WIFI_SCAN_RUNNING!"));
        return false;
    } else if (n < 0) {
        Serial.print(F("scanNetworks failed with unknown error code: "));
        Serial.println(n);
        return false;
    } else if (n == 0) {
        Serial.println(F("No networks found"));
        return false;
    } else {
        Serial.print(F("Found networks: "));
        Serial.println(n);
        wifiResults.clear();
        for (wifi_ssid_count_t i = 0; i < n; i++) {
            WifiResult result;
            bool res = WiFi.getNetworkInfo(i, result.ssid, result.encryptionType,
                result.rssi, result.bssid, result.channel
#if defined(ESP8266)
                ,
                result.isHidden
#endif
            );

            if (!res) {
                Serial.printf_P(PSTR("Error getNetworkInfo for %d\n"), i);
            } else {
                if (result.ssid.length() == 0) {
                    continue;
                }

                result.quality = 0;

                if (result.rssi <= -100) {
                    result.quality = 0;
                } else if (result.rssi >= -50) {
                    result.quality = 100;
                } else {
                    result.quality = 2 * (result.rssi + 100);
                }

                wifiResults.push_back(result);
            }
        }

        sort(wifiResults.begin(), wifiResults.end(), ssidLess);
        wifiResults.erase(unique(wifiResults.begin(), wifiResults.end(), ssidEqual), wifiResults.end());
        sort(wifiResults.begin(), wifiResults.end(), signalLess);

        return true;
    }
}

int ESPReactWifiManager::size()
{
    return wifiResults.size();
}

std::vector<ESPReactWifiManager::WifiResult> ESPReactWifiManager::results()
{
    return wifiResults;
}
