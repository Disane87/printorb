#include "web_portal.h"
#include "web_index.h"
#include "config.h"
#include "wifi_manager.h"
#include "logbuf.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ESPmDNS.h>

namespace {
    AsyncWebServer server(80);
    WebPortal::ConfigSavedCb g_onSaved = nullptr;

    // Latest status snapshot for /api/status.
    PrinterStatus g_status;
    String        g_label = "";

    String buildConfigJson() {
        JsonDocument d;
        d["wifiSsid"]        = cfg.wifiSsid;
        // wifiPass intentionally omitted (write-only).
        d["hostname"]        = cfg.hostname;
        d["printerType"]     = Config::printerTypeStr(cfg.printerType);
        d["printerName"]     = cfg.printerName;
        d["printerIp"]       = cfg.printerIp;
        d["moonrakerPort"]   = cfg.moonrakerPort;
        d["moonrakerApiKey"] = cfg.moonrakerApiKey;
        d["bambuSerial"]     = cfg.bambuSerial;
        d["bambuAccessCode"] = cfg.bambuAccessCode;
        d["brightness"]      = cfg.brightness;
        String out; serializeJson(d, out); return out;
    }

    String buildStatusJson() {
        JsonDocument d;
        d["state"]        = PrinterStatus::stateLabel(g_status.state);
        d["printer"]      = g_label;
        d["progress"]     = g_status.progress;
        d["nozzle"]       = g_status.nozzleTemp;
        d["nozzleTarget"] = g_status.nozzleTarget;
        d["bed"]          = g_status.bedTemp;
        d["bedTarget"]    = g_status.bedTarget;
        d["remaining"]    = g_status.remainingSec;
        d["layer"]        = g_status.currentLayer;
        d["totalLayer"]   = g_status.totalLayer;
        d["file"]         = g_status.filename;
        String out; serializeJson(d, out); return out;
    }

    // Accumulates a POST body across chunks, then applies it once complete.
    void handleConfigBody(AsyncWebServerRequest* req, uint8_t* data,
                          size_t len, size_t index, size_t total) {
        static String body;
        if (index == 0) body = "";
        body.reserve(total);
        body += String((const char*)data, len);
        if (index + len != total) return;  // wait for more chunks

        JsonDocument doc;
        if (deserializeJson(doc, body)) {
            req->send(400, "application/json", "{\"ok\":false}");
            return;
        }

        if (doc["wifiSsid"].is<const char*>()) cfg.wifiSsid = String((const char*)doc["wifiSsid"]);
        if (doc["hostname"].is<const char*>())  cfg.hostname = String((const char*)doc["hostname"]);
        // Only overwrite the password when a non-empty value is supplied.
        if (doc["wifiPass"].is<const char*>()) {
            String p = String((const char*)doc["wifiPass"]);
            if (p.length()) cfg.wifiPass = p;
        }
        if (doc["printerType"].is<const char*>())
            cfg.printerType = Config::printerTypeFromStr(String((const char*)doc["printerType"]));
        if (doc["printerName"].is<const char*>())     cfg.printerName     = String((const char*)doc["printerName"]);
        if (doc["printerIp"].is<const char*>())       cfg.printerIp       = String((const char*)doc["printerIp"]);
        if (doc["moonrakerPort"].is<int>())           cfg.moonrakerPort   = doc["moonrakerPort"];
        if (doc["moonrakerApiKey"].is<const char*>()) cfg.moonrakerApiKey = String((const char*)doc["moonrakerApiKey"]);
        if (doc["bambuSerial"].is<const char*>())     cfg.bambuSerial     = String((const char*)doc["bambuSerial"]);
        if (doc["bambuAccessCode"].is<const char*>()) cfg.bambuAccessCode = String((const char*)doc["bambuAccessCode"]);
        if (doc["brightness"].is<int>())              cfg.brightness      = constrain((int)doc["brightness"], 10, 100);

        Config::save();
        if (g_onSaved) g_onSaved();

        req->send(200, "application/json", "{\"ok\":true}");

        // Reboot shortly after so the new WiFi/printer settings take effect.
        req->onDisconnect([]() {
            delay(300);
            ESP.restart();
        });
    }
}

namespace WebPortal {

void begin(ConfigSavedCb onSaved) {
    g_onSaved = onSaved;

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html", INDEX_HTML);
    });

    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", buildConfigJson());
    });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", buildStatusJson());
    });

    // Live serial/debug log as plain text (the device's own scrollback buffer).
    server.on("/api/log", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/plain", Log::dump());
    });

    // Asynchronous WiFi scan. Returns {"scanning":true} while a scan runs, then
    // {"networks":[{ssid,rssi,secure}]} once results are ready. Client polls.
    server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest* req) {
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) {
            req->send(200, "application/json", "{\"scanning\":true}");
            return;
        }
        if (n < 0) {                  // not started yet / previous failed
            WiFi.scanNetworks(true);  // kick off async scan
            req->send(200, "application/json", "{\"scanning\":true}");
            return;
        }
        JsonDocument d;
        d["scanning"] = false;
        JsonArray arr = d["networks"].to<JsonArray>();
        for (int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            if (!ssid.length()) continue;
            bool dup = false;
            for (JsonObject o : arr) {
                if (o["ssid"].as<String>() == ssid) {        // keep strongest dup
                    if (WiFi.RSSI(i) > o["rssi"].as<int>()) o["rssi"] = WiFi.RSSI(i);
                    dup = true; break;
                }
            }
            if (dup) continue;
            JsonObject o = arr.add<JsonObject>();
            o["ssid"]   = ssid;
            o["rssi"]   = WiFi.RSSI(i);
            o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        }
        String out; serializeJson(d, out);
        WiFi.scanDelete();
        req->send(200, "application/json", out);
    });

    // mDNS discovery of Klipper/Bambu printers (only meaningful in STA mode,
    // where the device is on the user's LAN). Blocks briefly per query.
    server.on("/api/discover", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument d;
        JsonArray arr = d["printers"].to<JsonArray>();
        if (WifiManager::mode() == WifiManager::Mode::STA) {
            struct { const char* svc; const char* type; } svcs[] = {
                {"moonraker", "klipper"}, {"bambulab", "bambu"}
            };
            for (auto& s : svcs) {
                int n = MDNS.queryService(s.svc, "tcp");
                for (int i = 0; i < n; i++) {
                    JsonObject o = arr.add<JsonObject>();
                    o["type"] = s.type;
                    o["name"] = MDNS.hostname(i);
                    o["ip"]   = MDNS.IP(i).toString();
                    o["port"] = MDNS.port(i);
                }
            }
        }
        String out; serializeJson(d, out);
        req->send(200, "application/json", out);
    });

    server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest* req) {},   // response sent from body handler
        nullptr,
        handleConfigBody);

    server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", "{\"ok\":true}");
        req->onDisconnect([]() { delay(300); ESP.restart(); });
    });

    // Captive portal: in AP mode, redirect every unknown request (including the
    // OS connectivity probes like /generate_204 and /hotspot-detect.html) to the
    // config page so the "sign in to network" sheet pops up. In STA mode just
    // serve the page for unknown routes.
    server.onNotFound([](AsyncWebServerRequest* req) {
        if (WifiManager::mode() == WifiManager::Mode::AP) {
            req->redirect("http://" + WifiManager::ip() + "/");
        } else {
            req->send(200, "text/html", INDEX_HTML);
        }
    });

    server.begin();
}

void updateStatus(const PrinterStatus& s, const String& label) {
    g_status = s;
    g_label  = label;
}

}  // namespace WebPortal
