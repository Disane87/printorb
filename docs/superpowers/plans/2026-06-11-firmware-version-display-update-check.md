# Firmware Version Display + Update Check Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show the running firmware version on the touch UI and web portal, let the device discover newer GitHub releases, and apply an update over WiFi after explicit user confirmation.

**Architecture:** A build-time `-DPRINTORB_VERSION` (resolved by `scripts/version.py`) feeds a tiny `Version` module. A new `Updater` module polls the GitHub Releases API (boot + every 24 h, STA only) and, on request, pulls `printorb-app.bin` via `httpUpdate`. The touch System screen and web Update tab surface the version and an "update available" affordance; flashing is always user-confirmed.

**Tech Stack:** PlatformIO/Arduino (ESP32-S3), LVGL 8, ArduinoJson 7, `HTTPClient` + `WiFiClientSecure` + `HTTPUpdate`, ESPAsyncWebServer, semantic-release CI.

**Note on testing:** this is embedded firmware with no host test framework. The verification gate for every code task is a clean `pio run` (both envs) plus the stated logic check; hardware behaviour is verified by the user after flashing. Keep an eye on the ~45 % RAM / ~47 % flash baseline.

---

### Task 1: Build-time version injection

**Files:**
- Create: `scripts/version.py`
- Create: `src/version.h`
- Create: `src/version.cpp`
- Modify: `platformio.ini` (base env build section)

- [ ] **Step 1: Create the version-resolver script**

Create `scripts/version.py`:

```python
# PlatformIO pre-build hook: inject -DPRINTORB_VERSION.
# Priority: env PRINTORB_VERSION (CI) -> git tag -> "0.0.0-dev".
Import("env")
import os, subprocess

def resolve_version():
    v = os.environ.get("PRINTORB_VERSION", "").strip()
    if v:
        return v
    try:
        v = subprocess.check_output(
            ["git", "describe", "--tags", "--abbrev=0"],
            stderr=subprocess.DEVNULL).decode().strip()
        if v[:1] in ("v", "V"):
            v = v[1:]
        if v:
            return v
    except Exception:
        pass
    return "0.0.0-dev"

version = resolve_version()
env.Append(CPPDEFINES=[("PRINTORB_VERSION", env.StringifyMacro(version))])
print("PrintOrb firmware version: %s" % version)
```

- [ ] **Step 2: Create the version header**

Create `src/version.h`:

```cpp
/**
 * @file version.h
 * Compile-time firmware version. STRING comes from -DPRINTORB_VERSION
 * (scripts/version.py); falls back to "0.0.0-dev" for ad-hoc builds.
 */
#pragma once

namespace Version {
    extern const char* STRING;      // e.g. "1.4.2"
    extern const char* BUILD_DATE;  // __DATE__ " " __TIME__
}
```

- [ ] **Step 3: Create the version source**

Create `src/version.cpp`:

```cpp
#include "version.h"

#ifndef PRINTORB_VERSION
#define PRINTORB_VERSION "0.0.0-dev"
#endif

namespace Version {
    const char* STRING     = PRINTORB_VERSION;
    const char* BUILD_DATE = __DATE__ " " __TIME__;
}
```

- [ ] **Step 4: Wire the script into platformio.ini**

In `platformio.ini`, in the `[env:esp32-s3-touch-lcd-128]` block, add an `extra_scripts` line in the "Build flags" area (the `-ota` env extends this env, so it inherits the script). Insert immediately after the `build_flags = ... -I include` block and before `; --- Libraries ---`:

```ini
; --- Build version injection -------------------------------------------------
extra_scripts = pre:scripts/version.py
```

- [ ] **Step 5: Build and verify the version is injected**

Run: `pio run -e esp32-s3-touch-lcd-128`
Expected: build SUCCESS, and the log line `PrintOrb firmware version: 0.0.0-dev` (or the local git tag) appears near the top.

- [ ] **Step 6: Commit**

```bash
git add scripts/version.py src/version.h src/version.cpp platformio.ini
git commit -m "feat: inject build-time firmware version"
```

---

### Task 2: Config flag `autoUpdateCheck`

**Files:**
- Modify: `src/config.h` (struct)
- Modify: `src/config.cpp` (load + save)

- [ ] **Step 1: Add the field to OrbConfig**

In `src/config.h`, after the `// --- UI ---` block (after the `screenSleepEnabled` line, before `// --- Time / scheduled dimming ...`), add:

```cpp
    // --- Updates ---
    bool     autoUpdateCheck = true;  // periodic GitHub release check + on-screen
                                      // / web notice. Flashing is always manual.
```

- [ ] **Step 2: Load it from NVS**

In `src/config.cpp`, in `load()`, after the `cfg.screenSleepEnabled = ...` line, add:

```cpp
    cfg.autoUpdateCheck = prefs.getBool("autoupd", true);
```

- [ ] **Step 3: Save it to NVS**

In `src/config.cpp`, in `save()`, after the `prefs.putBool("slpOn", ...)` line, add:

```cpp
    prefs.putBool("autoupd", cfg.autoUpdateCheck);
```

- [ ] **Step 4: Build**

Run: `pio run -e esp32-s3-touch-lcd-128`
Expected: SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add src/config.h src/config.cpp
git commit -m "feat: add autoUpdateCheck config flag"
```

---

### Task 3: Updater module (check + confirmed apply)

**Files:**
- Create: `src/updater.h`
- Create: `src/updater.cpp`

- [ ] **Step 1: Create the header**

Create `src/updater.h`:

```cpp
/**
 * @file updater.h
 * GitHub-release update checker + confirmed self-update.
 * Checks the latest release tag (boot + every 24 h, STA only, gated by
 * cfg.autoUpdateCheck) and, on requestApply(), pulls printorb-app.bin over OTA.
 */
#pragma once

#include <Arduino.h>

namespace Updater {
    /** Start the scheduler. Call once after WiFi STA is up. */
    void begin();

    /** Non-blocking service. Call frequently from loop(). */
    void loop();

    /** True if the latest release is newer than the running build. */
    bool updateAvailable();

    /** Latest release version (no leading 'v'); "" until first successful check. */
    const String& latestVersion();

    /** Queue a GitHub OTA pull; performed from loop() in the main thread. */
    void requestApply();

    /** True while a pull is queued or in progress. */
    bool isApplying();
}
```

- [ ] **Step 2: Create the implementation**

Create `src/updater.cpp`:

```cpp
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
static bool     _pendingApply = false;
static bool     _applying     = false;

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

    String url = String("https://github.com/") + GH_OWNER + "/" + GH_REPO +
                 "/releases/latest/download/printorb-app.bin";
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
void requestApply()           { if (_available) _pendingApply = true; }
bool isApplying()             { return _applying || _pendingApply; }

}  // namespace Updater
```

- [ ] **Step 3: Build (module compiles even though not yet wired)**

Run: `pio run -e esp32-s3-touch-lcd-128`
Expected: SUCCESS. (The new `.cpp` is compiled by PlatformIO automatically; it is unused until Task 4.)

- [ ] **Step 4: Commit**

```bash
git add src/updater.h src/updater.cpp
git commit -m "feat: add GitHub release update checker module"
```

---

### Task 4: Wire the updater into main.cpp

**Files:**
- Modify: `src/main.cpp` (include, `setup()`, `loop()`)

- [ ] **Step 1: Include the updater header**

In `src/main.cpp`, after `#include "ota.h"`, add:

```cpp
#include "updater.h"
```

- [ ] **Step 2: Start the updater in STA mode**

In `src/main.cpp`, in `setup()`, inside the `if (WifiManager::mode() == WifiManager::Mode::STA) { ... }` block, after `Ota::begin();`, add:

```cpp
        Updater::begin();   // boot + daily GitHub release check (gated by config)
```

- [ ] **Step 3: Service the updater from loop()**

In `src/main.cpp`, in `loop()`, immediately after `Ota::loop();`, add:

```cpp
    Updater::loop();   // performs a queued GitHub OTA pull, then daily checks
```

- [ ] **Step 4: Build**

Run: `pio run -e esp32-s3-touch-lcd-128`
Expected: SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat: wire updater into boot + main loop"
```

---

### Task 5: Touch UI — version label + update button (System screen)

**Files:**
- Modify: `src/ui.cpp` (includes, globals, callback, `buildSystemScreen`, `refreshSystem`, `update`)

- [ ] **Step 1: Add includes**

In `src/ui.cpp`, with the other project includes near the top of the file, add:

```cpp
#include "updater.h"
#include "version.h"
```

- [ ] **Step 2: Add widget globals + last-state tracker**

In `src/ui.cpp`, on the line declaring the System widgets (`lv_obj_t* sy_wifi, *sy_ip, *sy_bright;`), extend it to:

```cpp
lv_obj_t* sy_wifi, *sy_ip, *sy_bright, *sy_ver, *sy_upd, *btn_upd;
```

Then, just below that line, add a state tracker used by the update button guard:

```cpp
PrintState g_lastState = PrintState::OFFLINE;  // for the update button print-guard
```

- [ ] **Step 3: Add the update button callback**

In `src/ui.cpp`, immediately after the `reboot_cb` function (the one that calls `ESP.restart()`), add:

```cpp
void update_cb(lv_event_t* /*e*/) {
    if (g_lastState == PrintState::PRINTING || g_lastState == PrintState::PAUSED) {
        Log::printf("[UI] update blocked (print active)\n");
        return;
    }
    Log::printf("[UI] firmware update confirmed\n");
    Updater::requestApply();
}
```

- [ ] **Step 4: Add the version label to the System screen**

In `src/ui.cpp`, in `buildSystemScreen()`, immediately after the `sy_ip` block (after `lv_label_set_text(sy_ip, "0.0.0.0");`), add:

```cpp
    sy_ver = lv_label_create(col);
    lv_obj_set_style_text_font(sy_ver, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sy_ver, lv_color_hex(0x6b7480), 0);
    lv_label_set_text(sy_ver, "v?");
```

- [ ] **Step 5: Add the (hidden) update button before the reboot button**

In `src/ui.cpp`, in `buildSystemScreen()`, immediately before the reboot button block (the comment `// Reboot: long-press to avoid accidental restarts ...`), add:

```cpp
    // Update: shown only when a newer release exists; long-press to confirm
    // (mirrors the reboot/stop long-press pattern). Hidden by default.
    btn_upd = lv_btn_create(col);
    lv_obj_set_size(btn_upd, 150, 36);
    lv_obj_add_flag(btn_upd, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_bg_color(btn_upd, lv_palette_darken(LV_PALETTE_GREEN, 1), 0);
    lv_obj_set_style_radius(btn_upd, 10, 0);
    lv_obj_add_event_cb(btn_upd, update_cb, LV_EVENT_LONG_PRESSED, NULL);
    sy_upd = lv_label_create(btn_upd);
    lv_label_set_text(sy_upd, LV_SYMBOL_DOWNLOAD "  Hold = Update");
    lv_obj_center(sy_upd);
    lv_obj_add_flag(btn_upd, LV_OBJ_FLAG_HIDDEN);
```

- [ ] **Step 6: Reflect version + availability in refreshSystem**

In `src/ui.cpp`, in `refreshSystem()`, after `lv_label_set_text_fmt(sy_bright, "%d%%", (int)cfg.brightness);`, add:

```cpp
    lv_label_set_text_fmt(sy_ver, "v%s", Version::STRING);
    if (Updater::updateAvailable()) {
        lv_label_set_text_fmt(sy_upd, LV_SYMBOL_DOWNLOAD "  v%s  (hold)",
                              Updater::latestVersion().c_str());
        lv_obj_clear_flag(btn_upd, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(btn_upd, LV_OBJ_FLAG_HIDDEN);
    }
```

- [ ] **Step 7: Track the print state for the button guard**

In `src/ui.cpp`, in `update(const PrinterStatus& s, const String& printerLabel)`, as the first statement of the function body (before `static bool started ...`), add:

```cpp
    g_lastState = s.state;
```

- [ ] **Step 8: Build**

Run: `pio run -e esp32-s3-touch-lcd-128`
Expected: SUCCESS.

- [ ] **Step 9: Commit**

```bash
git add src/ui.cpp
git commit -m "feat: show version + update button on touch System screen"
```

---

### Task 6: Web API — sysinfo fields, config flag, GitHub-update endpoint

**Files:**
- Modify: `src/web_portal.cpp` (includes, sysinfo, config json, config body, new route)

- [ ] **Step 1: Add includes**

In `src/web_portal.cpp`, after `#include "timekeeper.h"`, add:

```cpp
#include "version.h"
#include "updater.h"
```

- [ ] **Step 2: Expose version + update info in sysinfo**

In `src/web_portal.cpp`, in `buildSysinfoJson()`, replace the line:

```cpp
        d["firmware"]   = __DATE__ " " __TIME__;
```

with:

```cpp
        d["firmware"]        = Version::STRING;       // semver (was build date)
        d["buildDate"]       = Version::BUILD_DATE;
        d["version"]         = Version::STRING;
        d["latestVersion"]   = Updater::latestVersion();
        d["updateAvailable"] = Updater::updateAvailable();
```

- [ ] **Step 3: Expose the config flag**

In `src/web_portal.cpp`, in `buildConfigJson()`, after `d["screenSleepEnabled"] = cfg.screenSleepEnabled;`, add:

```cpp
        d["autoUpdateCheck"] = cfg.autoUpdateCheck;
```

- [ ] **Step 4: Accept the config flag on save**

In `src/web_portal.cpp`, in `handleConfigBody()`, after the `if (doc["screenSleepEnabled"].is<bool>()) ...` line, add:

```cpp
        if (doc["autoUpdateCheck"].is<bool>())        cfg.autoUpdateCheck = doc["autoUpdateCheck"];
```

- [ ] **Step 5: Add the GitHub-update endpoint**

In `src/web_portal.cpp`, in `begin()`, immediately after the existing `server.on("/api/update", HTTP_POST, ...)` registration (after its closing `});`), add:

```cpp
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
```

- [ ] **Step 6: Build**

Run: `pio run -e esp32-s3-touch-lcd-128`
Expected: SUCCESS.

- [ ] **Step 7: Commit**

```bash
git add src/web_portal.cpp
git commit -m "feat: web sysinfo version fields + GitHub update endpoint"
```

---

### Task 7: Web UI — version display, settings toggle, update banner

**Files:**
- Modify: `src/web_index.h` (Settings card, Update tab, JS)

- [ ] **Step 1: Add the "Updates" settings card**

In `src/web_index.h`, immediately before the `<div class="card">` that contains `<b>Security</b>`, add:

```html
      <div class="card">
        <b>Updates</b>
        <label class="ck"><input type="checkbox" id="autoUpdateCheck"> Check for firmware updates</label>
        <div class="hint">Periodically checks GitHub for new releases and shows a notice.
          Flashing is always manual.</div>
      </div>
```

- [ ] **Step 2: Add the update banner to the Update tab**

In `src/web_index.h`, in the `<!-- UPDATE -->` section, immediately after `<div id="upd" class="hide">` and before its first `<div class="card">`, add:

```html
    <div id="updBanner" class="hide"></div>
```

- [ ] **Step 3: Populate Firmware/Build rows from the new fields**

In `src/web_index.h`, in `loadInfo()`, replace the line:

```javascript
    h+=kv('Firmware',d.firmware||'—');
```

with:

```javascript
    h+=kv('Firmware','v'+(d.version||d.firmware||'—'));
    h+=kv('Build',d.buildDate||'—');
    if(d.updateAvailable)h+=kv('Update','v'+d.latestVersion+' available');
```

- [ ] **Step 4: Load + save the toggle**

In `src/web_index.h`, in `loadConfig()`, after `document.getElementById('screenSleepEnabled').checked=(d.screenSleepEnabled!==false);`, add:

```javascript
    document.getElementById('autoUpdateCheck').checked=(d.autoUpdateCheck!==false);
```

In `src/web_index.h`, in the form `submit` handler, after `o.screenSleepEnabled=document.getElementById('screenSleepEnabled').checked;`, add:

```javascript
  o.autoUpdateCheck=document.getElementById('autoUpdateCheck').checked;
```

- [ ] **Step 5: Add the banner + apply functions**

In `src/web_index.h`, immediately before the `function uploadFw(){` definition, add:

```javascript
async function checkUpd(){
  try{
    var d=await (await fetch('/api/sysinfo')).json();
    var b=document.getElementById('updBanner');
    if(d.updateAvailable){
      b.className='card';
      b.innerHTML='<b>Update available: v'+d.latestVersion+'</b>'+
        '<div class="hint">Running: v'+(d.version||'?')+'</div>'+
        '<button type="button" class="primary" onclick="applyGithub()">Update now</button>';
    }else{b.className='hide';b.innerHTML='';}
  }catch(e){}
}
async function applyGithub(){
  var pw=prompt('Update password (the OTA password set in Settings):');
  if(pw===null)return;
  var m=document.getElementById('updMsg');m.className='hint';m.textContent='Starting update…';
  try{
    var r=await fetch('/api/update/github',{method:'POST',headers:{'Authorization':'Basic '+btoa('admin:'+pw)}});
    if(r.status==202){m.className='hint ok';m.textContent='Update started. Watch the device screen; it reboots when done.';}
    else if(r.status==401){m.className='hint err';m.textContent='Wrong password.';}
    else{var j=await r.json().catch(function(){return{};});m.className='hint err';m.textContent='Could not start ('+r.status+(j.error?': '+j.error:'')+').';}
  }catch(e){m.className='hint err';m.textContent='Error: '+e;}
}
```

- [ ] **Step 6: Refresh the banner when the Update tab opens**

In `src/web_index.h`, in `function tab(t){ ... }`, immediately before its closing `}`, add:

```javascript
  if(t=='upd')checkUpd();
```

- [ ] **Step 7: Build (verifies the PROGMEM string still compiles)**

Run: `pio run -e esp32-s3-touch-lcd-128`
Expected: SUCCESS.

- [ ] **Step 8: Commit**

```bash
git add src/web_index.h
git commit -m "feat: web version display, update toggle + update banner"
```

---

### Task 8: CI — resolve version before the build

**Files:**
- Modify: `.github/workflows/release-and-deploy.yml`

- [ ] **Step 1: Move Node setup earlier + add a dry-run version step**

In `.github/workflows/release-and-deploy.yml`, in the `release` job, **remove** the existing `- name: Set up Node` step (the one between "Merge flashable image" and "Semantic Release"). Then, immediately **before** the `- name: Build firmware` step, insert:

```yaml
      - name: Set up Node
        uses: actions/setup-node@v4
        with:
          node-version: '20'

      - name: Resolve next version (dry-run)
        id: nextver
        run: |
          set -euo pipefail
          npx -y -p semantic-release@24 \
                 -p @semantic-release/changelog@6 \
                 -p @semantic-release/git@10 \
                 semantic-release --dry-run > sr.log 2>&1 || true
          V="$(grep -oiP 'next release version is \K[0-9]+\.[0-9]+\.[0-9]+' sr.log | head -n1 || true)"
          if [ -z "$V" ]; then
            V="$(git describe --tags --abbrev=0 2>/dev/null | sed 's/^v//' || true)"
          fi
          if [ -z "$V" ]; then V="0.0.0-${GITHUB_SHA::7}"; fi
          echo "version=$V" >> "$GITHUB_OUTPUT"
          echo "Resolved firmware version: $V"
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
```

- [ ] **Step 2: Pass the version into the firmware build**

In `.github/workflows/release-and-deploy.yml`, change the `- name: Build firmware` step from:

```yaml
      - name: Build firmware
        run: pio run -e esp32-s3-touch-lcd-128
```

to:

```yaml
      - name: Build firmware
        run: pio run -e esp32-s3-touch-lcd-128
        env:
          PRINTORB_VERSION: ${{ steps.nextver.outputs.version }}
```

- [ ] **Step 3: Validate the workflow YAML locally**

Run: `python -c "import yaml,sys; yaml.safe_load(open('.github/workflows/release-and-deploy.yml')); print('yaml ok')"`
Expected: `yaml ok`.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/release-and-deploy.yml
git commit -m "ci: resolve semver before build and inject PRINTORB_VERSION"
```

---

### Task 9: Documentation

**Files:**
- Modify: `CLAUDE.md`

- [ ] **Step 1: Add the updater + version to the architecture table**

In `CLAUDE.md`, in the "Architecture (one responsibility per file)" table, add these two rows after the `src/ota.{h,cpp}` row:

```markdown
| `src/version.{h,cpp}` | Compile-time firmware version (`-DPRINTORB_VERSION` via `scripts/version.py`) |
| `src/updater.{h,cpp}` | GitHub-release update check + confirmed self-update (`httpUpdate`) |
```

- [ ] **Step 2: Document the new web API row**

In `CLAUDE.md`, in the "Web API" table, after the `POST | /api/update | ...` row, add:

```markdown
| POST | `/api/update/github` | trigger confirmed GitHub-release OTA (HTTP Basic; 409 if printing / no update) |
```

- [ ] **Step 3: Add a versioning note**

In `CLAUDE.md`, immediately after the "Flashing over WiFi (OTA)" section, add a new section:

```markdown
## Versioning & update check

The firmware version is injected at build time as `-DPRINTORB_VERSION` by
`scripts/version.py` (env `PRINTORB_VERSION` from CI → git tag → `0.0.0-dev`) and
exposed via `Version::STRING`. `src/updater.{h,cpp}` checks the GitHub Releases API
(`Disane87/printorb`, boot + every 24 h, STA only, gated by `cfg.autoUpdateCheck`)
and, on user confirmation (touch long-press or web "Update now"), pulls
`printorb-app.bin` via `httpUpdate`. Flashing is never automatic. TLS uses
`setInsecure()` (trusted-LAN posture). The CI resolves the next version via a
semantic-release dry-run **before** the firmware build so the binary is stamped
with the version it will be released as.
```

- [ ] **Step 4: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: document version injection + update checker"
```

---

## Self-Review

**Spec coverage:**
- Version provenance (build-time inject) → Task 1. ✔
- `Version` module → Task 1. ✔
- Updater module (check + apply, semver, scheduling, STA-only, TLS insecure, app.bin URL) → Task 3. ✔
- Boot + daily cadence, first check +15 s → Task 3 (`FIRST_CHECK_DELAY_MS`, `CHECK_INTERVAL_MS`) + Task 4 wiring. ✔
- Config `autoUpdateCheck` (default ON) + NVS → Task 2. ✔
- Touch version label + update badge/button (long-press, print-guard) → Task 5. ✔
- Web sysinfo fields + settings toggle + update banner/button + endpoint → Tasks 6 & 7. ✔
- Manual `.bin` upload untouched → confirmed (Task 6 adds a sibling route; existing route unchanged). ✔
- CI dry-run reorder → Task 8. ✔
- Docs (architecture, API row, versioning note) → Task 9. ✔
- No "check now" button → confirmed absent. ✔

**Placeholder scan:** No TBD/TODO/"handle errors" placeholders; all steps contain literal code/commands.

**Type consistency:**
- `Version::STRING` / `Version::BUILD_DATE` — declared Task 1, used Tasks 5/6.
- `Updater::updateAvailable()` / `latestVersion()` / `requestApply()` / `isApplying()` / `begin()` / `loop()` — declared Task 3, used Tasks 4/5/6 with matching signatures.
- `cfg.autoUpdateCheck` — added Task 2, read in Tasks 3/6/7.
- Widget globals `sy_ver` / `sy_upd` / `btn_upd` and `g_lastState` / `update_cb` — all defined and used within Task 5.
- Endpoint `/api/update/github` — defined Task 6, called by `applyGithub()` Task 7.
- `checkUpd` / `applyGithub` — defined Task 7, referenced in the same task (tab hook + banner button).
