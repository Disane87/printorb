# Display Sleep + Dedicated Idle Screen — Design

**Date:** 2026-06-11
**Component:** PrintOrb firmware (ESP32-S3, LVGL UI)

## Goal

Two related features:

1. **Display sleep** (not true ESP32 deep sleep): turn the backlight off after a
   configurable inactivity timeout, but only while no print is active. WiFi,
   printer polling and the web portal keep running so the device still detects a
   print start and stays reachable. A touch or a starting print wakes the screen.
2. **Dedicated idle screen**: a full-screen "resting" view shown automatically
   whenever the printer is idle/offline/complete; switches back to the status
   screen automatically when a print starts.

## Active vs. Resting

The central classification driving both features:

| Group   | States                       | Behaviour                                  |
|---------|------------------------------|--------------------------------------------|
| Active  | `PRINTING`, `PAUSED`, `ERROR`| Status carousel visible; screen never sleeps |
| Resting | `IDLE`, `OFFLINE`, `COMPLETE`| Idle screen; screen may sleep after timeout |

`PAUSED` and `ERROR` are intentionally "active" so a paused job or an error never
disappears into a dark screen.

## Sleep mechanics

Use LVGL's built-in `lv_disp_get_inactive_time()` (time since last touch) instead
of hand-rolled touch tracking.

A small sleep manager in `main.cpp` (`serviceSleep()`), called every loop:

- **Sleep:** resting state AND `inactive_time > timeout` AND not already asleep
  → `Display::setBrightness(0)`, set `asleep = true`.
- **Wake on touch:** `inactive_time` small AND `asleep` → restore
  `cfg.brightness`. The waking tap is swallowed (does not trigger a button or
  gesture).
- **Wake on print start:** resting→active transition wakes the screen immediately
  even without a touch.

`timeout = cfg.screenTimeoutSec * 1000`. `screenTimeoutSec == 0` disables sleep.

Display helpers kept minimal; asleep/awake state lives in `main.cpp`.

## Configuration

New field in `OrbConfig`:

- `uint16_t screenTimeoutSec = 120;` — `0` = never sleep (feature off), default 2 min.

Handled like `brightness`: persisted in NVS via `Config::load/save`, exposed in the
web portal (`web_index.h` form + `web_portal.cpp` GET/POST `/api/config`) as a
numeric field "Display sleep after (s), 0 = never". No hardcoded values — all in
`OrbConfig`/NVS per CLAUDE.md.

## Idle screen (`scr_idle`)

A standalone full-screen view in the existing visual style (black background,
`addRingFrame`, centered flex column):

- Subtle orb at the top (boot-screen style brand mark).
- Printer name (`cfg.printerName`).
- Large state-dependent, colored word (`stateColor`); English per CLAUDE.md
  (on-screen strings are English):
  - `IDLE` → "Ready" (blue-grey)
  - `COMPLETE` → "Done" (green)
  - `OFFLINE` → "Offline" (grey)
- Nozzle/bed temperature row (so cooling is visible); hidden when `OFFLINE`.

Refresher `refreshIdle(s, label)` mirrors the other `refresh*` functions.

## Navigation & screen switching

`scr_idle` is managed like the boot/setup screens — outside the carousel array —
driven by `UI::update()`:

- **Resting** → load `scr_idle` (if not already shown); hide page dots.
- **Active** → if idle/boot currently shown, switch to `scr_status`, show dots,
  `carIdx = 0`. Carousel then behaves as before.
- **Swipe on the idle screen** (left/right) → enters the carousel (→ status
  screen), so the user can deliberately reach Details/System/AMS without the
  carousel being forced during idle.

An internal flag tracks whether the idle screen is currently shown. The sleep
manager queries the printer state directly (not the UI), staying decoupled.

## Files touched

- `src/config.{h,cpp}` — `screenTimeoutSec` field + NVS load/save.
- `src/display.{h,cpp}` — (minimal, if needed) brightness already exists.
- `src/ui.{h,cpp}` — `scr_idle`, `refreshIdle`, state-driven switching, idle-swipe.
- `src/main.cpp` — `serviceSleep()` sleep manager, wake-on-print.
- `src/web_portal.cpp` + `src/web_index.h` — config field plumbing.

## Validation

`pio run` must stay clean (baseline ≈44 % RAM / ≈44 % app partition).
