#include "wifi_manager.h"
#include "config.h"
#include "logbuf.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>

namespace {
    WifiManager::Mode g_mode = WifiManager::Mode::CONNECTING;
    String   g_apSsid;
    uint32_t g_lastCheckMs = 0;
    const uint32_t CHECK_INTERVAL_MS = 5000;

    DNSServer g_dns;
    bool g_dnsActive  = false;
    bool g_mdnsActive = false;

    // Make a string safe to use as a DHCP/mDNS hostname (lowercase, [a-z0-9-]).
    String sanitizeHost(const String& in) {
        String s;
        for (size_t i = 0; i < in.length(); i++) {
            char c = in[i];
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') s += c;
            else if (c >= 'A' && c <= 'Z') s += (char)(c - 'A' + 'a');
            else if (c == ' ' || c == '_')  s += '-';
        }
        if (!s.length()) s = "printorb";
        return s;
    }

    void startMDNS() {
        String h = sanitizeHost(cfg.hostname);
        g_mdnsActive = MDNS.begin(h.c_str());
        Log::printf("[WiFi] mDNS %s -> %s.local\n", g_mdnsActive ? "up" : "failed", h.c_str());
    }

    void startAP() {
        // AP_STA so the config portal can still scan for nearby networks.
        WiFi.mode(WIFI_AP_STA);
        String h = sanitizeHost(cfg.hostname);
        uint64_t mac = ESP.getEfuseMac();
        g_apSsid = h + "-setup-" + String((uint16_t)(mac >> 32), HEX);
        WiFi.softAP(g_apSsid.c_str());  // open network for easy onboarding

        // Captive portal: answer every DNS query with our own AP IP so the
        // phone's "sign in to network" page pops up automatically.
        g_dns.setErrorReplyCode(DNSReplyCode::NoError);
        g_dns.start(53, "*", WiFi.softAPIP());
        g_dnsActive = true;
        g_mode = WifiManager::Mode::AP;
        Log::printf("[WiFi] AP %s ip=%s (captive DNS on)\n",
                      g_apSsid.c_str(), WiFi.softAPIP().toString().c_str());
    }

    bool connectSTA(WifiManager::WaitCb onWait) {
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        WiFi.setHostname(sanitizeHost(cfg.hostname).c_str());
        WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());

        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
            if (onWait) onWait();   // keep the UI alive while we wait
            delay(60);
        }
        return WiFi.status() == WL_CONNECTED;
    }
}

namespace WifiManager {

void begin(WaitCb onWait) {
    g_mode = Mode::CONNECTING;
    if (cfg.hasWifi() && connectSTA(onWait)) {
        g_mode = Mode::STA;
        startMDNS();
    } else {
        startAP();
    }
}

void loop() {
    if (g_mode == Mode::AP) {
        if (g_dnsActive) g_dns.processNextRequest();
        return;
    }
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

String resolveHost(const String& hostOrIp) {
    if (!hostOrIp.length()) return hostOrIp;

    IPAddress literal;
    if (literal.fromString(hostOrIp)) return hostOrIp;  // already an IP

    String name = hostOrIp;
    name.toLowerCase();
    if (name.endsWith(".local")) name = name.substring(0, name.length() - 6);

    if (g_mdnsActive) {
        IPAddress mip = MDNS.queryHost(name.c_str(), 2000);
        if ((uint32_t)mip != 0) {
            Log::printf("[WiFi] mDNS %s.local -> %s\n", name.c_str(), mip.toString().c_str());
            return mip.toString();
        }
    }

    IPAddress dip;
    if (WiFi.hostByName(hostOrIp.c_str(), dip) == 1 && (uint32_t)dip != 0) {
        Log::printf("[WiFi] DNS %s -> %s\n", hostOrIp.c_str(), dip.toString().c_str());
        return dip.toString();
    }

    Log::printf("[WiFi] could not resolve %s\n", hostOrIp.c_str());
    return hostOrIp;
}

}  // namespace WifiManager
