# Scheduled Display Dimming + Power-Save Toggle — Design

**Date:** 2026-06-11
**Status:** Approved (design), pending implementation plan
**Component:** PrintOrb firmware (ESP32-S3) + embedded web UI

## Goal

Let the user, from the **web UI**:

1. **Schedule nightly dimming** — reduce display brightness during one configurable
   time window (e.g. 22:00–07:00), so the orb isn't glaring in the evening.
2. **Toggle the power-save mode** (the existing inactivity display auto-off) on/off
   explicitly, instead of the implicit "timeout = 0 means never" convention.

Both require the device to know the local wall-clock time, which it currently does
not. So this feature also adds **NTP time sync** with a user-selected timezone.

## Decisions (from brainstorming)

- **Timezone:** chosen from a **dropdown of common zones**, each mapping to a POSIX
  TZ string so DST is handled automatically.
- **Schedule scope:** exactly **one nightly window** (start/end + dim brightness),
  supporting windows that cross midnight. Multiple windows are out of scope (YAGNI).
- **"Power-save":** means the existing **display auto-off on inactivity**; the new
  control is an explicit on/off toggle in the web UI. No CPU/WiFi throttling.
- **Apply:** saving config reboots the device (existing behavior) so NTP/TZ
  re-initialize cleanly. No live (reboot-less) apply.

## Current state

- `cfg.brightness` (0–100%) and `cfg.screenTimeoutSec` (inactivity auto-off, 0 =
  never) already exist and are plumbed through NVS + web UI.
- `serviceSleep()` in `main.cpp` blanks the backlight (`setBrightness(0)`) after
  `screenTimeoutSec` of inactivity while the printer is "resting", and restores
  `cfg.brightness` on touch/print-start (see display-sleep spec).
- **No time source exists** (no NTP/RTC) — confirmed by grep.

## 1. Time source — new module `src/timekeeper.{h,cpp}`

One responsibility: provide local time, per the project's one-file-per-concern
convention.

- `void begin(const String& posixTz);` — sets TZ and starts SNTP:
  `configTzTime(posixTz.c_str(), "pool.ntp.org", "time.nist.gov")`. Called once
  WiFi STA is up (from `main.cpp` after connect, or from `wifi_manager` on STA
  transition). If `posixTz` is empty, default to UTC.
- `bool synced();` — true once SNTP has delivered a plausible time
  (`getLocalTime()` succeeds / year > 2020).
- `int localMinutes();` — minutes since local midnight (0–1439), or `-1` if not
  yet synced.

Degrades gracefully: in AP/setup mode or before first sync, `localMinutes()`
returns `-1`, so the dim window is treated as inactive (normal brightness). SNTP
auto-resyncs periodically (ESP-IDF default ~1 h).

## 2. Config — new `OrbConfig` fields + NVS

```cpp
String   timezone;                  // POSIX TZ string (from dropdown), "" = UTC
bool     screenSleepEnabled = true; // explicit on/off for inactivity auto-off
bool     dimSchedEnabled    = false;
uint16_t dimStartMin = 22*60;       // window start, minutes since midnight (0..1439)
uint16_t dimEndMin   =  7*60;       // window end,   minutes since midnight (0..1439)
uint8_t  dimBrightness = 20;        // brightness % during the window (0..100; 0 = off)
```

NVS keys (≤15 chars) in `config.cpp` `load()`/`save()`: `tz`, `slpOn`, `dimOn`,
`dimStart`, `dimEnd`, `dimBri`. Defaults as above. No hardcoded behavior outside
`OrbConfig`/NVS, per CLAUDE.md.

Window semantics (`inDimWindow(now)` with `now = localMinutes()`):

- `start < end`  → `start <= now < end`.
- `start > end`  → overnight: `now >= start || now < end`.
- `start == end` → window disabled (always false).

## 3. Brightness arbitration — `src/main.cpp`

Replace the scattered brightness setters with a single per-loop computation so the
three inputs (manual brightness, scheduled dim, inactivity sleep) compose
predictably:

```
base      = (dimSchedEnabled && Time::synced() && inDimWindow(Time::localMinutes()))
                ? cfg.dimBrightness : cfg.brightness
effective = asleep ? 0 : base
```

- `serviceSleep()` is extended: auto-off only triggers when
  `screenSleepEnabled && screenTimeoutSec > 0`. Touch-wake restores **base** (so a
  wake inside the dim window comes back dimmed, not full).
- Call `Display::setBrightness(effective)` only when `effective` changes, to avoid
  redundant panel writes every loop.
- On-device brightness buttons (`ui.cpp`) keep setting `cfg.brightness` (the active
  brightness); the next loop's arbitration applies it (and dim window still wins
  during the window).

## 4. Web UI — extend the "Display" card (`src/web_index.h`)

- **Power-save:** checkbox "Display auto-off" bound to `screenSleepEnabled`; it
  shows/hides the existing "sleep after (s)" numeric field.
- **Timezone:** `<select>` dropdown whose option `value`s are POSIX TZ strings,
  labels are friendly zone names (e.g. Europe/Berlin →
  `CET-1CEST,M3.5.0,M10.5.0/3`, Europe/London, UTC, US Eastern, …).
- **Night dimming:** checkbox "Enable night dimming" + two `<input type="time">`
  fields (Start / End, HH:MM) + a "Night brightness" range slider (0–100).
- JS converts HH:MM ↔ minutes and sends `dimStartMin`/`dimEndMin` as integers;
  sends `screenSleepEnabled`, `dimSchedEnabled` as booleans, `timezone` as the
  POSIX string, `dimBrightness` as int. `loadConfig()` converts minutes back to
  HH:MM and reflects checkbox/dropdown state.

## 5. Backend plumbing — `src/web_portal.cpp`

- `buildConfigJson()`: add `timezone`, `screenSleepEnabled`, `dimSchedEnabled`,
  `dimStartMin`, `dimEndMin`, `dimBrightness`.
- `handleConfigBody()`: parse + `constrain` each new field
  (`dimStartMin`/`dimEndMin` 0–1439, `dimBrightness` 0–100). Save → reboot (existing
  behavior) so NTP/TZ re-init cleanly.

## Files touched

- `src/config.{h,cpp}` — new fields + NVS load/save.
- `src/timekeeper.{h,cpp}` — **new** NTP/local-time module.
- `src/main.cpp` — start time sync after STA connect; brightness arbitration +
  extended `serviceSleep()`.
- `src/web_portal.cpp` — config GET/POST plumbing.
- `src/web_index.h` — Display card UI + JS.
- `CLAUDE.md` — add `timekeeper` to the architecture table (and web API/config notes
  if appropriate).

## Validation

- `pio run` must stay clean (baseline ≈44 % RAM / ≈44 % app partition).
- Manual: set a window spanning "now", confirm the display dims to the night
  brightness and returns to full afterwards; toggle power-save off and confirm the
  display no longer auto-blanks on inactivity; with no WiFi/NTP, confirm no
  dimming occurs (normal brightness).

## Non-goals / future

- Multiple dim windows, weekday-specific schedules, sunrise/sunset (would need geo
  + solar calc), and CPU/WiFi power throttling — all out of scope.
