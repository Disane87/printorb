#include "klipper_client.h"
#include "logbuf.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

void KlipperClient::begin() {
    _status.state = PrintState::OFFLINE;
    _lastPollMs = 0;
}

void KlipperClient::loop() {
    if (WiFi.status() != WL_CONNECTED) {
        _status.state = PrintState::OFFLINE;
        return;
    }
    uint32_t now = millis();
    if (now - _lastPollMs < POLL_INTERVAL_MS) return;
    _lastPollMs = now;
    poll();
}

bool KlipperClient::postCommand(const char* path) {
    if (WiFi.status() != WL_CONNECTED) return false;
    HTTPClient http;
    String url = "http://" + _host + ":" + String(_port) + path;
    http.setConnectTimeout(1500);
    http.setTimeout(2500);
    if (!http.begin(url)) return false;
    if (_apiKey.length()) http.addHeader("X-Api-Key", _apiKey);
    int code = http.POST("");
    http.end();
    Log::printf("[Klipper] POST %s -> %d\n", path, code);
    return code == HTTP_CODE_OK;
}

void KlipperClient::pause()  { postCommand("/printer/print/pause"); }
void KlipperClient::resume() { postCommand("/printer/print/resume"); }
void KlipperClient::stop()   { postCommand("/printer/print/cancel"); }

void KlipperClient::poll() {
    HTTPClient http;
    String url = "http://" + _host + ":" + String(_port) +
                 "/printer/objects/query?print_stats&display_status"
                 "&heater_bed&extruder&virtual_sdcard";

    http.setConnectTimeout(1500);
    http.setTimeout(2500);
    if (!http.begin(url)) {
        _status.state = PrintState::OFFLINE;
        return;
    }
    if (_apiKey.length()) http.addHeader("X-Api-Key", _apiKey);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        _status.state = PrintState::OFFLINE;
        return;
    }

    // Filter: only deserialize the fields we use, to keep RAM small.
    JsonDocument filter;
    JsonObject st = filter["result"]["status"].to<JsonObject>();
    st["print_stats"]["state"]            = true;
    st["print_stats"]["filename"]         = true;
    st["print_stats"]["print_duration"]   = true;
    st["print_stats"]["info"]             = true;   // current/total layer
    st["print_stats"]["message"]          = true;
    st["display_status"]["progress"]      = true;
    st["virtual_sdcard"]["progress"]      = true;
    st["heater_bed"]["temperature"]       = true;
    st["heater_bed"]["target"]            = true;
    st["extruder"]["temperature"]         = true;
    st["extruder"]["target"]              = true;

    JsonDocument doc;
    DeserializationError err =
        deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();

    if (err) {
        _status.state = PrintState::OFFLINE;
        return;
    }

    JsonObject s = doc["result"]["status"];
    if (s.isNull()) {
        _status.state = PrintState::OFFLINE;
        return;
    }

    // --- Temperatures ---
    _status.nozzleTemp   = s["extruder"]["temperature"]  | 0.0f;
    _status.nozzleTarget = s["extruder"]["target"]       | 0.0f;
    _status.bedTemp      = s["heater_bed"]["temperature"]| 0.0f;
    _status.bedTarget    = s["heater_bed"]["target"]     | 0.0f;

    // --- Progress (prefer virtual_sdcard, fall back to display_status) ---
    float prog = s["virtual_sdcard"]["progress"] | -1.0f;
    if (prog < 0) prog = s["display_status"]["progress"] | 0.0f;
    _status.progress = constrain(prog, 0.0f, 1.0f) * 100.0f;

    // --- State ---
    const char* kstate = s["print_stats"]["state"] | "standby";
    if      (!strcmp(kstate, "printing")) _status.state = PrintState::PRINTING;
    else if (!strcmp(kstate, "paused"))   _status.state = PrintState::PAUSED;
    else if (!strcmp(kstate, "complete")) _status.state = PrintState::COMPLETE;
    else if (!strcmp(kstate, "error")) {
        _status.state    = PrintState::ERROR;
        _status.errorMsg = String((const char*)(s["print_stats"]["message"] | ""));
    }
    else _status.state = PrintState::IDLE;

    _status.filename = String((const char*)(s["print_stats"]["filename"] | ""));

    // --- Layers (Klipper exposes these via print_stats.info when slicer sets them) ---
    _status.currentLayer = s["print_stats"]["info"]["current_layer"] | -1;
    _status.totalLayer   = s["print_stats"]["info"]["total_layer"]   | -1;

    // --- Remaining time estimate from elapsed duration & progress ---
    float duration = s["print_stats"]["print_duration"] | 0.0f;
    float p        = _status.progress / 100.0f;
    if (_status.state == PrintState::PRINTING && p > 0.01f && duration > 1.0f) {
        float total = duration / p;
        _status.remainingSec = (int32_t)max(0.0f, total - duration);
    } else {
        _status.remainingSec = -1;
    }

    _status.lastUpdateMs = millis();
}
