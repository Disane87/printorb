#include "web_portal.h"
#include "web_index.h"
#include "config.h"
#include "wifi_manager.h"
#include "logbuf.h"
#include "timekeeper.h"
#include "version.h"
#include "updater.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Update.h>

namespace {
    AsyncWebServer server(80);
    WebPortal::ConfigSavedCb g_onSaved = nullptr;
    WebPortal::DryHandler    g_onDry   = nullptr;

    // Latest status snapshot for /api/status.
    PrinterStatus g_status;
    String        g_label = "";

    // Browser-OTA progress (0..100), -1 = idle. Set by the async upload handler,
    // read by the main loop to drive the on-screen update screen.
    volatile int  g_otaPct = -1;

    // Firmware updates require the admin password (HTTP Basic, user "admin").
    // No password configured => always denied, so OTA is off until one is set.
    bool otaAuthed(AsyncWebServerRequest* req) {
        return cfg.adminPassword.length() &&
               req->authenticate("admin", cfg.adminPassword.c_str());
    }

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
        d["adminPwSet"]      = cfg.adminPassword.length() > 0;  // password write-only
        d["brightness"]      = cfg.brightness;
        d["screenTimeoutSec"] = cfg.screenTimeoutSec;
        d["screenSleepEnabled"] = cfg.screenSleepEnabled;
        d["autoUpdateCheck"] = cfg.autoUpdateCheck;
        d["timezone"]        = cfg.timezone;
        d["dimSchedEnabled"] = cfg.dimSchedEnabled;
        d["dimStartMin"]     = cfg.dimStartMin;
        d["dimEndMin"]       = cfg.dimEndMin;
        d["dimBrightness"]   = cfg.dimBrightness;
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

        // AMS (Bambu). Only meaningfully populated when present; the web UI's
        // AMS tab shows a placeholder otherwise. Max 4 units x 4 slots is tiny.
        JsonObject ams = d["ams"].to<JsonObject>();
        const AmsInfo& a = g_status.ams;
        ams["present"] = a.present;
        if (a.present) {
            ams["units"]      = a.units;
            ams["activeUnit"] = a.activeUnit;   // 0-based unit array index
            ams["activeSlot"] = a.activeSlot;
            JsonArray units = ams["unit"].to<JsonArray>();
            for (int u = 0; u < 4; u++) {
                const AmsUnit& U = a.unit[u];
                if (!U.present) continue;
                JsonObject uo = units.add<JsonObject>();
                uo["index"] = u;                // for active-slot matching in JS
                uo["count"] = U.count;
                uo["ht"]    = U.isHT;           // AMS HT (single high-temp slot)
                uo["model"] = U.isHT ? "AMS HT" : "AMS";
                if (U.humidity >= 0)    uo["humidity"]    = U.humidity;
                if (U.humidityPct >= 0) uo["humidityPct"] = U.humidityPct;
                if (U.tempC > -99.0f)   uo["temp"]        = U.tempC;
                if (U.drying)           uo["drying"]      = true;
                if (U.dryTargetC >= 0)  uo["dryTargetC"]  = U.dryTargetC;
                if (U.dryRemainMin >= 0)uo["dryRemainMin"]= U.dryRemainMin;
                JsonArray slots = uo["slots"].to<JsonArray>();
                int maxSlots = U.isHT ? 1 : 4;   // HT is physically single-slot
                for (int i = 0; i < maxSlots; i++) {
                    const AmsSlot& sl = U.slot[i];
                    JsonObject so = slots.add<JsonObject>();
                    so["used"] = sl.present;
                    if (sl.present) {
                        so["type"] = sl.type;
                        char col[8];
                        snprintf(col, sizeof(col), "#%06X",
                                 (unsigned)(sl.color & 0xFFFFFF));
                        so["color"] = col;
                        if (sl.remain >= 0) so["remain"] = sl.remain;
                    }
                }
            }
        }
        String out; serializeJson(d, out); return out;
    }

    const char* resetReasonStr() {
        switch (esp_reset_reason()) {
            case ESP_RST_POWERON:  return "power-on";
            case ESP_RST_SW:       return "software";
            case ESP_RST_PANIC:    return "panic";
            case ESP_RST_INT_WDT:  return "int-watchdog";
            case ESP_RST_TASK_WDT: return "task-watchdog";
            case ESP_RST_WDT:      return "watchdog";
            case ESP_RST_BROWNOUT: return "brownout";
            case ESP_RST_DEEPSLEEP:return "deep-sleep";
            case ESP_RST_EXT:      return "external";
            default:               return "unknown";
        }
    }

    // Diagnostics for the web "Info" tab: network, memory, flash, chip, build,
    // time-sync and the live printer connection. Everything useful for debugging.
    String buildSysinfoJson() {
        JsonDocument d;

        // Firmware / build
        d["firmware"]        = Version::STRING;       // semver (was build date)
        d["buildDate"]       = Version::BUILD_DATE;
        d["version"]         = Version::STRING;
        d["latestVersion"]   = Updater::latestVersion();
        d["updateAvailable"] = Updater::updateAvailable();
        d["sdk"]        = ESP.getSdkVersion();
        d["resetReason"]= resetReasonStr();
        d["uptimeSec"]  = (uint32_t)(millis() / 1000);

        // Network
        JsonObject net = d["net"].to<JsonObject>();
        net["mode"]     = (WifiManager::mode() == WifiManager::Mode::AP) ? "AP" : "STA";
        net["ip"]       = WifiManager::ip();
        net["hostname"] = cfg.hostname;
        net["mdns"]     = cfg.hostname + ".local";
        net["mac"]      = WiFi.macAddress();
        net["ssid"]     = WiFi.SSID();
        net["rssi"]     = WiFi.RSSI();
        net["channel"]  = WiFi.channel();

        // Memory (heap + PSRAM)
        JsonObject mem = d["mem"].to<JsonObject>();
        mem["heapFree"]   = ESP.getFreeHeap();
        mem["heapMin"]    = ESP.getMinFreeHeap();
        mem["heapSize"]   = ESP.getHeapSize();
        mem["heapMaxBlk"] = ESP.getMaxAllocHeap();
        mem["psramFree"]  = ESP.getFreePsram();
        mem["psramSize"]  = ESP.getPsramSize();

        // Flash / sketch
        JsonObject fl = d["flash"].to<JsonObject>();
        fl["flashSize"]  = ESP.getFlashChipSize();
        fl["sketchSize"] = ESP.getSketchSize();
        fl["sketchFree"] = ESP.getFreeSketchSpace();

        // Chip
        JsonObject chip = d["chip"].to<JsonObject>();
        chip["model"]   = ESP.getChipModel();
        chip["rev"]     = ESP.getChipRevision();
        chip["cores"]   = ESP.getChipCores();
        chip["cpuMhz"]  = ESP.getCpuFreqMHz();

        // Time sync (NTP)
        JsonObject tm = d["time"].to<JsonObject>();
        tm["synced"] = Time::synced();
        int lm = Time::localMinutes();
        if (lm >= 0) {
            char hhmm[6];
            snprintf(hhmm, sizeof(hhmm), "%02d:%02d", lm / 60, lm % 60);
            tm["local"] = hhmm;
        }

        // Printer link
        JsonObject pr = d["printer"].to<JsonObject>();
        pr["type"]  = Config::printerTypeStr(cfg.printerType);
        pr["state"] = PrinterStatus::stateLabel(g_status.state);

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
        // Only overwrite the admin password when a non-empty value is supplied.
        if (doc["adminPassword"].is<const char*>()) {
            String p = String((const char*)doc["adminPassword"]);
            if (p.length()) cfg.adminPassword = p;
        }
        if (doc["brightness"].is<int>())              cfg.brightness      = constrain((int)doc["brightness"], 10, 100);
        if (doc["screenTimeoutSec"].is<int>())        cfg.screenTimeoutSec = constrain((int)doc["screenTimeoutSec"], 0, 3600);
        if (doc["screenSleepEnabled"].is<bool>())     cfg.screenSleepEnabled = doc["screenSleepEnabled"];
        if (doc["autoUpdateCheck"].is<bool>())        cfg.autoUpdateCheck = doc["autoUpdateCheck"];
        if (doc["timezone"].is<const char*>())        cfg.timezone        = String((const char*)doc["timezone"]);
        if (doc["dimSchedEnabled"].is<bool>())        cfg.dimSchedEnabled = doc["dimSchedEnabled"];
        if (doc["dimStartMin"].is<int>())             cfg.dimStartMin     = constrain((int)doc["dimStartMin"], 0, 1439);
        if (doc["dimEndMin"].is<int>())               cfg.dimEndMin       = constrain((int)doc["dimEndMin"], 0, 1439);
        if (doc["dimBrightness"].is<int>())           cfg.dimBrightness   = constrain((int)doc["dimBrightness"], 0, 100);

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

    server.on("/api/sysinfo", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", buildSysinfoJson());
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

    // Start/stop AMS HT drying. ?action=start (default) | stop. Temperature and
    // duration are derived on-device from the loaded filament. Open like the
    // other printer controls (trusted-LAN assumption).
    server.on("/api/dry", HTTP_POST, [](AsyncWebServerRequest* req) {
        bool start = true;
        if (req->hasParam("action", true))
            start = req->getParam("action", true)->value() != "stop";
        else if (req->hasParam("action"))
            start = req->getParam("action")->value() != "stop";
        if (g_onDry) g_onDry(start);
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // Firmware update (OTA over HTTP). The .bin is POSTed as the RAW request body
    // (application/octet-stream), not multipart: that keeps memory low so a large
    // upload doesn't reset the connection while the Bambu MQTT TLS buffer is
    // allocated. Streamed straight into the OTA partition. Requires the admin
    // password (HTTP Basic, user "admin"); reboots on success.
    server.on("/api/update", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            if (!otaAuthed(req)) { g_otaPct = -1; return req->requestAuthentication(); }
            bool ok = !Update.hasError();
            if (!ok) g_otaPct = -1;
            AsyncWebServerResponse* res = req->beginResponse(
                ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
            res->addHeader("Connection", "close");
            req->send(res);
            if (ok) req->onDisconnect([]() { delay(300); ESP.restart(); });
        },
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len,
           size_t index, size_t total) {
            if (!otaAuthed(req)) return;            // unauthenticated -> ignore body
            if (index == 0) {
                Log::printf("[Update] start: %u bytes\n", (unsigned)total);
                g_otaPct = 0;
                if (!Update.begin(total ? total : UPDATE_SIZE_UNKNOWN))
                    Log::printf("[Update] begin failed: %s\n", Update.errorString());
            }
            if (Update.isRunning()) {
                if (Update.write(data, len) != len)
                    Log::printf("[Update] write failed: %s\n", Update.errorString());
                g_otaPct = total ? (int)((uint64_t)(index + len) * 100 / total) : 0;
                if (index + len >= total) {
                    g_otaPct = 100;
                    if (Update.end(true))
                        Log::printf("[Update] ok, %u bytes\n", (unsigned)(index + len));
                    else
                        Log::printf("[Update] end failed: %s\n", Update.errorString());
                }
            }
        });

    // Trigger a GitHub release pull (confirmed OTA). Requires the admin password.
    // Refuses while a print is active or when no update is known. The actual
    // download/flash happens in the main loop (Updater::loop -> apply).
    server.on("/api/update/github", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!otaAuthed(req)) return req->requestAuthentication();
        if (g_status.state == PrintState::PRINTING ||
            g_status.state == PrintState::PAUSED) {
            req->send(409, "application/json", "{\"ok\":false,\"error\":\"print active\"}");
            return;
        }
        if (!Updater::updateAvailable()) {
            req->send(409, "application/json", "{\"ok\":false,\"error\":\"no update\"}");
            return;
        }
        Updater::requestApply();
        req->send(202, "application/json", "{\"ok\":true}");
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

int otaProgress() { return g_otaPct; }

void setDryHandler(DryHandler cb) { g_onDry = cb; }

}  // namespace WebPortal
