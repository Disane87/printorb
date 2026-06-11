# Design: Firmware Version Display + Update Check + Confirmed Auto-Update

Date: 2026-06-11
Status: Approved (pending spec review)

## Goal

Make the running firmware version visible on the touch UI and the web portal, let
the device discover newer GitHub releases on its own, and let the user apply an
update over WiFi with a single confirmation — without the existing manual `.bin`
upload going away.

The original request was "version display (touch + web) + version check +
auto-update (when enabled)". Resolved interpretation, confirmed with the user:

- **Auto-*check* when enabled** — a config toggle gates the periodic check + the
  "update available" notification.
- **Flashing is always user-confirmed** — never a silent/automatic reflash. The
  device shows that an update exists; the user taps (touch long-press) or clicks
  (web button) to apply.

## Decisions (locked)

| Topic | Decision |
|-------|----------|
| Update behaviour | Notify + confirm. No silent auto-flash. |
| Version source | GitHub Releases API (`/releases/latest` → `tag_name`). |
| Check cadence | Once ~15 s after WiFi+NTP is up, then every 24 h. STA only. |
| Manual "check now" button | **Omitted** (not in chosen cadence). |
| TLS | `WiFiClientSecure::setInsecure()`, consistent with Bambu MQTT and the documented trusted-LAN posture. |
| `autoUpdateCheck` default | **ON** (the check only notifies; nothing destructive happens unattended). |
| CI version ordering | Resolve the next version via a semantic-release dry-run **before** the firmware build, then inject it. |

## Non-goals

- No silent/automatic flashing.
- No manual "check now" button.
- No rollback / multi-channel (beta) support.
- No change to the existing manual `.bin` upload path (`/api/update`).
- No TLS certificate pinning.

## Architecture

### 1. Version provenance (build time)

The firmware currently has **no** compiled-in version; web sysinfo shows
`__DATE__ " " __TIME__`. We inject a real semver at build time.

- New `scripts/version.py`, wired as `extra_scripts = pre:scripts/version.py` in
  `platformio.ini`. It resolves the version in priority order:
  1. env var `PRINTORB_VERSION` (set by CI),
  2. `git describe --tags --abbrev=0` (local dev, stripped of a leading `v`),
  3. fallback `0.0.0-dev`.
  It appends `-DPRINTORB_VERSION='"<version>"'` to the build flags.
- New `src/version.h` exposing:
  ```cpp
  namespace Version {
      extern const char* STRING;     // e.g. "1.4.2" or "0.0.0-dev"
      extern const char* BUILD_DATE; // __DATE__ " " __TIME__
  }
  ```
  with a tiny `src/version.cpp` that pins `STRING` to `PRINTORB_VERSION` (with a
  `#ifndef` fallback so a build without the macro still compiles).

- CI (`.github/workflows/release-and-deploy.yml`): the firmware build runs
  **before** semantic-release, so the next tag isn't known yet. Add a
  "Resolve next version (dry-run)" step **before** "Build firmware" that runs
  `npx semantic-release --dry-run` (or reuses the existing tag) to compute the
  version, export it as `PRINTORB_VERSION`, and pass it into the `pio run` step's
  env. The real release step stays as-is. (`steps.ver` later still resolves the
  site/manifest version as today.)

### 2. Update module — `src/updater.{h,cpp}`

Single responsibility: know the latest available version and, on request, pull &
apply it. Public surface:

```cpp
namespace Updater {
    void begin();                 // called after WiFi STA + NTP are up
    void loop();                  // non-blocking scheduler; call from loop()
    bool updateAvailable();       // latest > running
    const String& latestVersion();
    void requestApply();          // set pending flag; main loop performs httpUpdate
    bool isApplying();            // true while a pull is in progress
}
```

- **Scheduling** (`loop()`): state machine with `nextCheckMs`. First check
  `+15 s` after `begin()`, then `+24 h`. Skips entirely when not in STA mode or
  WiFi down. `cfg.autoUpdateCheck == false` disables checks (and clears any
  cached "available" state).
- **Check**: `HTTPClient` over `WiFiClientSecure(setInsecure)` GET
  `https://api.github.com/repos/Disane87/printorb/releases/latest` with headers
  `User-Agent: printorb` and `Accept: application/vnd.github+json`. Parse with an
  ArduinoJson **filter** that keeps only `tag_name` (the response is large; this
  keeps RAM low). Strip a leading `v`, compare via a small semver comparator
  (numeric major/minor/patch; pre-release/`-dev` treated as "older/unknown" →
  never offered as an upgrade). On success set `_latest` / `_available`.
- **Apply**: `requestApply()` sets `_pendingApply`. The **main loop** (not the
  async web task) detects it and calls `httpUpdate.update(client, url)` (ESP32
  `<HTTPUpdate.h>`, global `httpUpdate`) against
  `https://github.com/Disane87/printorb/releases/latest/download/printorb-app.bin`
  (redirects followed). Progress is bridged to the existing
  `UI::showUpdate(pct)` screen via the httpUpdate progress callback +
  `WebPortal::updateStatus`-style reporting. On success the ESP reboots into the
  new image; on failure it logs, shows a brief error on the update screen, and
  returns to the carousel.
- Guard: `requestApply()` refuses (logs + UI hint) while a print is active
  (`state == PRINTING || PAUSED`).

### 3. Config — `OrbConfig` / NVS

Add one field:

```cpp
bool autoUpdateCheck = true;   // periodic GitHub update check + notification
```

Loaded/saved/reset alongside the rest in `config.cpp` (NVS key e.g. `autoupd`).

### 4. Touch UI (System screen)

- A small version label `v1.4.2` near the IP row.
- When `Updater::updateAvailable()`: a line "Update v1.5.0 verfügbar" plus an
  **Update** button that requires a **long-press** to confirm (mirrors the
  existing Stop / Reboot long-press pattern). Long-press → `Updater::requestApply()`
  → the update screen shows download progress.
- `UI::update()` reads the updater state each refresh (same ~500 ms cadence as
  the rest of the status push) so the badge appears/disappears without a screen
  rebuild.

### 5. Web UI (`web_index.h` + `web_portal.cpp`)

- `/api/sysinfo` gains: `version`, `buildDate`, `latestVersion`, `updateAvailable`.
- Sysinfo display: "Firmware" shows `version`; build date shown as a separate row.
- Settings: a **"Auf Updates prüfen"** toggle bound to `autoUpdateCheck`
  (persisted via the existing `POST /api/config`).
- Update tab: when `updateAvailable`, a banner "Update verfügbar: v1.5.0" with a
  **"Jetzt aktualisieren"** button → `POST /api/update/github` (HTTP Basic, user
  `admin`, the admin password). The handler validates auth, then calls
  `Updater::requestApply()` and returns `202`. The existing manual `.bin` upload
  stays untouched below it.

### 6. New web endpoint

| Method | Path | Auth | Purpose |
|--------|------|------|---------|
| POST | `/api/update/github` | HTTP Basic (`admin`) | Trigger a GitHub release pull → confirmed OTA. Returns 202; rejects with 409 if already applying or a print is active. |

## Data flow

```
boot → WiFi STA up → NTP → Updater::begin()
  loop():
    Updater::loop()  → (t≥nextCheck && autoUpdateCheck && STA)
                       GET releases/latest → tag_name → compare → _available
    UI::update()     → reads updater state → shows/hides "update available" badge
    web sysinfo      → exposes version/latest/available

user confirms (touch long-press  OR  web "Jetzt aktualisieren" → POST)
  → Updater::requestApply() sets _pendingApply
  → main loop: httpUpdate.update(app.bin) → UI::showUpdate(pct) → reboot
```

## Error handling

- Network / HTTP / TLS failure on check → log only, retry next interval; no UI
  error spam.
- Missing/!200 release or `tag_name` absent → treat as "no update".
- Semver parse failure → "no update".
- `httpUpdate` failure → log error code, brief error on update screen, back to
  carousel, `_pendingApply` cleared.
- Not in STA / WiFi down / `autoUpdateCheck` off → no check performed.
- Apply requested during active print → refused with a log + UI hint.

## Security notes

Consistent with the existing posture (see memory: trusted-LAN, `/api/update`
is already unguarded RCE). `setInsecure()` means the GitHub fetch/download is not
MITM-protected; an attacker on-path could serve malicious firmware. Accepted
trade-off for this device class; certificate pinning is an explicit non-goal.
`POST /api/update/github` still requires the admin password (HTTP Basic), same as
the manual upload.

## Affected files

| File | Change |
|------|--------|
| `platformio.ini` | `extra_scripts = pre:scripts/version.py` |
| `scripts/version.py` | new — resolve version, inject `-DPRINTORB_VERSION` |
| `src/version.{h,cpp}` | new — `Version::STRING` / `BUILD_DATE` |
| `src/updater.{h,cpp}` | new — check + confirmed apply |
| `src/config.{h,cpp}` | add `autoUpdateCheck` + NVS load/save/reset |
| `src/main.cpp` | `Updater::begin()/loop()`; run `_pendingApply` |
| `src/ui.{h,cpp}` | version label + update badge/button (System screen) |
| `src/web_portal.cpp` | sysinfo fields + `POST /api/update/github` |
| `src/web_index.h` | version display, settings toggle, update banner/button |
| `.github/workflows/release-and-deploy.yml` | dry-run version → `PRINTORB_VERSION` before build |
| `CLAUDE.md` | document version source + new web API row + updater module |

## Validation

- `pio run` (both envs) must stay green; watch RAM/flash vs the ~45 %/47 %
  baseline.
- Hardware test by the user: flash, confirm the version shows on touch + web.
- Force an "update available" by setting `PRINTORB_VERSION` locally to a value
  below the latest release, then exercising the touch long-press and the web
  button.
