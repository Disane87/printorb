#include "updater.h"
#include "version.h"
#include "config.h"
#include "logbuf.h"
#include "ui.h"
#include <lvgl.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>

namespace Updater {

// GitHub repo that publishes releases (see package.json "repository").
static const char* GH_OWNER = "Disane87";
static const char* GH_REPO  = "printorb";

static const uint32_t FIRST_CHECK_DELAY_MS = 15000;                  // after begin()
static const uint32_t CHECK_INTERVAL_MS    = 24UL * 60 * 60 * 1000;  // 24 h

static bool     _began        = false;
static uint32_t _nextCheckMs  = 0;
static bool     _available    = false;
static String   _latest       = "";
// volatile: written by requestApply() from the async web-server task, read by
// loop() on the main thread — keep the optimizer from caching the load.
static volatile bool _pendingApply = false;
static volatile bool _applying     = false;

// Parse the leading MAJOR.MINOR.PATCH of a version string. Stops at the first
// non-digit/non-dot (so "1.4.2-rc1" -> {1,4,2}). Returns false if no digits.
static bool parseSemver(const String& v, long out[3]) {
    out[0] = out[1] = out[2] = 0;
    int part = 0; long val = 0; bool any = false;
    for (size_t i = 0; i < v.length() && part < 3; i++) {
        char c = v[i];
        if (c >= '0' && c <= '9') { val = val * 10 + (c - '0'); any = true; }
        else if (c == '.')        { out[part++] = val; val = 0; }
        else                      break;
    }
    if (part < 3) out[part] = val;
    return any;
}

// True if `latest` is strictly newer than `running`.
static bool isNewer(const String& latest, const String& running) {
    long a[3], b[3];
    if (!parseSemver(latest, a))  return false;  // unparsable remote -> ignore
    if (!parseSemver(running, b)) return true;   // dev/unknown local -> offer
    for (int i = 0; i < 3; i++) {
        if (a[i] > b[i]) return true;
        if (a[i] < b[i]) return false;
    }
    return false;
}

static void check() {
    if (WiFi.status() != WL_CONNECTED) return;

    WiFiClientSecure client;
    client.setInsecure();                       // trusted-LAN posture (see spec)
    HTTPClient http;
    String url = String("https://api.github.com/repos/") + GH_OWNER + "/" +
                 GH_REPO + "/releases/latest";
    if (!http.begin(client, url)) { Log::printf("[Updater] begin failed\n"); return; }
    http.addHeader("User-Agent", "printorb");   // GitHub rejects requests without one
    http.addHeader("Accept", "application/vnd.github+json");

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Log::printf("[Updater] check HTTP %d\n", code);
        http.end();
        return;
    }

    JsonDocument filter;
    filter["tag_name"] = true;                  // keep RAM low on a big response
    JsonDocument doc;
    DeserializationError err = deserializeJson(
        doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (err) { Log::printf("[Updater] json: %s\n", err.c_str()); return; }

    String tag = String((const char*)(doc["tag_name"] | ""));
    if (tag.startsWith("v") || tag.startsWith("V")) tag = tag.substring(1);
    if (!tag.length()) { Log::printf("[Updater] no tag_name\n"); return; }

    _latest = tag;
    _available = isNewer(tag, String(Version::STRING));
    Log::printf("[Updater] latest=%s running=%s -> %s\n",
                tag.c_str(), Version::STRING, _available ? "UPDATE" : "current");
}

// httpUpdate progress -> on-screen bar (we run in the main/LVGL thread here).
static void onProgress(int cur, int total) {
    int pct = total ? (int)((int64_t)cur * 100 / total) : 0;
    UI::showUpdate((uint8_t)pct);
    lv_timer_handler();
}

static void apply() {
    _applying = true;
    Log::printf("[Updater] applying update %s\n", _latest.c_str());
    UI::showUpdate(0);
    lv_timer_handler();

    WiFiClientSecure client;
    client.setInsecure();
    httpUpdate.rebootOnUpdate(true);
    httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);  // github -> CDN
    httpUpdate.onProgress(onProgress);

    // Pin to the exact version we detected (not "latest"): the flashed image then
    // matches what updateAvailable() advertised, even if a newer release lands
    // between the check and the apply. semantic-release tags are vX.Y.Z.
    String url = String("https://github.com/") + GH_OWNER + "/" + GH_REPO +
                 "/releases/download/v" + _latest + "/printorb-app.bin";
    t_httpUpdate_return ret = httpUpdate.update(client, url);

    // On success the device reboots inside update(); reaching here = failure.
    Log::printf("[Updater] update failed (%d): %s\n",
                (int)ret, httpUpdate.getLastErrorString().c_str());
    _applying = false;
    _pendingApply = false;
}

void begin() {
    _began = true;
    _nextCheckMs = millis() + FIRST_CHECK_DELAY_MS;
}

void loop() {
    if (!_began) return;
    if (_pendingApply && !_applying) { apply(); return; }
    if (!cfg.autoUpdateCheck) return;
    if (WiFi.status() != WL_CONNECTED) return;

    uint32_t now = millis();
    if ((int32_t)(now - _nextCheckMs) >= 0) {
        _nextCheckMs = now + CHECK_INTERVAL_MS;
        check();
    }
}

bool updateAvailable()        { return _available && cfg.autoUpdateCheck; }
const String& latestVersion() { return _latest; }
void requestApply()           { if (_available && cfg.autoUpdateCheck) _pendingApply = true; }
bool isApplying()             { return _applying || _pendingApply; }

}  // namespace Updater
