#include "wifi_manager.h"
#include "config.h"
#include <WiFi.h>

namespace {
    WifiManager::Mode g_mode = WifiManager::Mode::CONNECTING;
    String   g_apSsid;
    uint32_t g_lastCheckMs = 0;
    const uint32_t CHECK_INTERVAL_MS = 5000;

    void startAP() {
        WiFi.mode(WIFI_AP);
        uint64_t mac = ESP.getEfuseMac();
        g_apSsid = "PrintOrb-Setup-" + String((uint16_t)(mac >> 32), HEX);
        WiFi.softAP(g_apSsid.c_str());  // open network for easy onboarding
        g_mode = WifiManager::Mode::AP;
    }

    bool connectSTA() {
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        WiFi.setHostname("printorb");
        WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());

        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
            delay(250);
        }
        return WiFi.status() == WL_CONNECTED;
    }
}

namespace WifiManager {

void begin() {
    g_mode = Mode::CONNECTING;
    if (cfg.hasWifi() && connectSTA()) {
        g_mode = Mode::STA;
    } else {
        startAP();
    }
}

void loop() {
    if (g_mode != Mode::STA) return;

    uint32_t now = millis();
    if (now - g_lastCheckMs < CHECK_INTERVAL_MS) return;
    g_lastCheckMs = now;

    if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect();
        WiFi.reconnect();
    }
}

Mode   mode()        { return g_mode; }
String apSsid()      { return g_apSsid; }
bool   isConnected() { return g_mode == Mode::STA && WiFi.status() == WL_CONNECTED; }

String ip() {
    if (g_mode == Mode::AP) return WiFi.softAPIP().toString();
    return WiFi.localIP().toString();
}

}  // namespace WifiManager
