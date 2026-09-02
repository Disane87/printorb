# CLAUDE.md — PrintOrb

Guidance for Claude Code (and other AI assistants) when working in this repo.

## What this is

**PrintOrb** is ESP32-S3 firmware for the **Waveshare ESP32-S3-Touch-LCD-1.28**
(1.28″ round 240×240 LCD, GC9A01 + CST816S touch). It shows live 3D-printer status
and connects to **either Klipper (Moonraker HTTP) or Bambu Lab (local MQTT/TLS)**.
Up to `ORB_MAX_PRINTERS` (4) printers can be stored; **one is active at a time** and
can be switched from the touch UI or the web portal without a reboot. The backends
and all credentials are configured at runtime through an embedded web portal — no
recompile needed to add or switch printers.

## Tech stack

- **Framework:** Arduino on ESP32-S3 (PlatformIO, `espressif32@^6.9.0`)
- **Display/Touch:** LovyanGFX (`lovyan03/LovyanGFX`)
- **GUI:** LVGL 8 (`lvgl/lvgl@^8.4.0`)
- **JSON:** ArduinoJson 7 (`bblanchon/ArduinoJson`)
- **MQTT (Bambu):** PubSubClient + `WiFiClientSecure`
- **Web server:** `mathieucarbou/ESPAsyncWebServer` + `AsyncTCP`
- **Networking:** `ESPmDNS` (`printorb.local`) + `DNSServer` (captive portal)
- **Icons:** embedded Material Design icon font (`src/orb_icons.c`, lv_font_conv)
- **Persistence:** NVS via `Preferences`

## Build / flash / monitor

```bash
pio run                 # compile
pio run -t upload       # flash over USB-C
pio device monitor      # serial @ 115200
```

> The PlatformIO env is `esp32-s3-touch-lcd-128` (no dots allowed in env names).
> The web UI is embedded in flash (`src/web_index.h`) — there is **no** LittleFS
> data upload step.
>
> **Serial goes to UART0** (the on-board CH343), not native USB:
> `ARDUINO_USB_CDC_ON_BOOT=0`. Use `Log::printf` (`src/logbuf.{h,cpp}`) instead of
> `Serial.printf` so output is also captured for the web log (`/api/log`).

## Architecture (one responsibility per file)

| File | Responsibility |
|------|----------------|
| `src/main.cpp` | `setup()`/`loop()`, boot sequence, LVGL tick, wiring |
| `src/config.{h,cpp}` | `OrbConfig` + `PrinterCfg[]` list + NVS load/save/reset (incl. v1 migration); global `cfg` |
| `src/printer.h` | `PrinterStatus` (+ `AmsInfo`) + abstract `PrinterClient` |
| `src/klipper_client.{h,cpp}` | Moonraker HTTP polling + pause/resume/stop |
| `src/bambu_client.{h,cpp}` | Bambu MQTT/TLS subscribe + AMS parsing + controls |
| `src/display.{h,cpp}` | LovyanGFX + LVGL bring-up (flush + touch callbacks) |
| `src/ui.{h,cpp}` | Boot/setup/idle screens + swipe carousel (status/job/ams/control/printers/system) |
| `src/theme.h` | Design tokens (colours, metrics, state colours) shared by every screen |
| `src/orb_icons.{h,c}` | Embedded MDI icon font + `ORB_ICON_*` UTF-8 macros |
| `src/wifi_manager.{h,cpp}` | STA + captive AP, DNS, mDNS, `resolveHost()` |
| `src/timekeeper.{h,cpp}` | SNTP local time (`Time::localMinutes()`) for scheduled dimming |
| `src/ota.{h,cpp}` | ArduinoOTA (`espota`) WiFi flashing; web upload lives in `web_portal` |
| `src/web_portal.{h,cpp}` | Async HTTP server: config/status/scan/discover/log/update |
| `src/web_index.h` | Embedded HTML/CSS/JS config UI (PROGMEM) |
| `src/logbuf.{h,cpp}` | Log ring buffer behind `Log::printf` (served at `/api/log`) |
| `include/lgfx_device.h` | **All hardware pins** + LovyanGFX panel/touch config |
| `include/lv_conf.h` | LVGL configuration |

**Data flow:** the active `PrinterClient` (Klipper or Bambu, chosen from
`cfg.printer().type`) is polled in `loop()` and owns a `PrinterStatus`. Every
~500 ms `main.cpp` pushes that status to both the LVGL UI (`UI::update`) and the
web portal (`WebPortal::updateStatus`). To add a backend, implement
`PrinterClient` and instantiate it in `createPrinter()`.

**Multi-printer:** `cfg.printers[]` holds up to 4 slots, `cfg.activePrinter` picks
one. Only that one is connected — a single Bambu client already reserves a 48 KB
MQTT buffer, so parallel polling does not fit in RAM. A switch requested from the
touch UI (inside `lv_timer_handler`) or from `POST /api/printer/select` (inside the
AsyncTCP task) only sets `pendingPrinter`; `servicePrinterSwitch()` applies it from
`loop()`, because the switch resolves mDNS (blocking ~2 s), allocates the MQTT
buffer and calls LVGL — none of which is safe in those contexts.

**UI:** `UI::update()` refreshes the widgets of all carousel screens without
switching screen; navigation is gesture-driven (`LV_EVENT_GESTURE`). Horizontal
swipes move through the carousel and wrap around; on the AMS screen vertical
swipes change the AMS unit, and on the System screen they scroll the page.
`Config::load()` runs **before** `UI::begin()` so the carousel knows which pages
exist. `rebuildCarousel()` re-derives the page list whenever the printer selection
changes (AMS page = active printer is Bambu; Printers page = more than one
configured).

**UI gotchas worth knowing:**
- LVGL's pointer path does **not** suppress events on `LV_STATE_DISABLED` widgets,
  and it still emits `CLICKED`/`LONG_PRESSED` on the widget a *swipe* started on.
  Both are handled by `actionable()` in `ui.cpp` — use it in every button callback.
- `LV_INDEV_DEF_LONG_PRESS_TIME` is hardcoded in LVGL 8's `lv_hal_indev.h`, not an
  `lv_conf.h` option, so `UI::begin()` pushes `Theme::HOLD_MS` onto the input
  driver at runtime. The hold-to-confirm fill animation is timed to the same value.

## Conventions

- **Comments, identifiers and docs in English.** User-facing strings on screen are
  English too.
- Settings live **only** in `OrbConfig`/NVS — don't hardcode IPs, SSIDs, or codes.
- Per-printer settings belong in `PrinterCfg`, not `OrbConfig`. NVS keys for them
  are built by `pkey(idx, field)` ("p0a", "p1bs", …) and must stay ≤ 15 chars.
- Colours, fonts and metrics for the device UI come from `src/theme.h` — no new
  hex literals in `ui.cpp`.
- **Commits follow [Conventional Commits](https://www.conventionalcommits.org/)**
  (`feat:`, `fix:`, `feat!:`/`BREAKING CHANGE:`, plus `chore:`/`docs:`/`ci:`/`refactor:`
  for no-release changes). semantic-release derives the version, `CHANGELOG.md` and
  GitHub Release from them on every push to `main` — see "Releases & versioning" in
  the README and `.github/workflows/release-and-deploy.yml`.
- Keep each printer backend self-contained; shared shape is `PrinterStatus`.
- ArduinoJson v7: never `auto x = doc["a"]["b"]` (MemberProxy is non-copyable).
  Use `JsonObject o = doc["a"]["b"].to<JsonObject>();` when building filters.
- Prefer streaming/filtered JSON parsing to keep RAM low (see both clients).

## Hardware gotchas (verify against your board revision)

- **Backlight = GPIO 2** on this board revision (some other Waveshare revisions
  use GPIO 40). If the screen stays dark, check `ORB_PIN_LCD_BL` in
  `include/lgfx_device.h` (all pins live there).
- GC9A01 needs `invert = true` and **BGR** order (`rgb_order = false`).
- **Bambu** requires **LAN Mode** enabled on the printer; needs serial + access code.
  TLS cert is self-signed → `WiFiClientSecure::setInsecure()`.
- **Bambu MQTT buffer** is **48 KB** (`MQTT_BUFFER` in `bambu_client.cpp`). The
  initial `pushall` report with AMS is ~32 KB; PubSubClient **silently drops** any
  packet larger than its buffer (no callback), so too small a buffer leaves the
  status stuck on "Offline".
- **Klipper layers** only appear if the slicer writes `print_stats.info`
  (`SET_PRINT_STATS_INFO CURRENT_LAYER=… TOTAL_LAYER=…`).
- **Camera:** not feasible on-device. Bambu **H2 series uses RTSPS/H.264 (port
  322)** which the ESP32-S3 can't decode; the JPEG chamber protocol (port 6000)
  is X1/P1/A1 only. Would need an external transcoding proxy.

## Web API

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/` | config/status UI |
| GET | `/api/status` | current status JSON |
| GET | `/api/config` | current settings (WiFi password + Bambu access codes omitted) |
| POST | `/api/config` | save settings → reboot; `printers` is sent as a whole array |
| POST | `/api/restart` | reboot |
| GET | `/api/scan` | async WiFi scan (poll until `networks` returned) |
| GET | `/api/discover` | mDNS discovery of Klipper/Bambu printers (STA only) |
| GET | `/api/sysinfo` | diagnostics JSON (IP, memory, flash, chip, uptime, NTP) |
| GET | `/api/log` | live device log (plain text, from `Log::dump()`) |
| POST | `/api/update` | firmware upload (raw `.bin` body, HTTP Basic auth) → reboot |
| POST | `/api/printer/select` | switch the active printer (`?index=N`); **no reboot** |

## Flashing over WiFi (OTA)

OTA requires an **Update / OTA password** set in the web UI (Settings → Security).
With no password, OTA is **disabled** (secure default): `Ota::begin()` returns
early and `/api/update` always 401s. The first flash is still USB.
- **Dev push:** `pio run -e esp32-s3-touch-lcd-128-ota -t upload` (ArduinoOTA /
  `espota`; set `--auth` in `platformio.ini` to the device password). Uses a
  device→host reverse TCP connection — a host firewall or crossing subnets breaks
  it; use the browser path instead.
- **Browser:** Update tab → uploads the raw `.bin` to `POST /api/update` with
  HTTP Basic auth (user `admin`, the OTA password). Raw body (not multipart) keeps
  memory low so large uploads don't reset while the Bambu MQTT buffer is live.

## First-time setup flow

No WiFi saved → device opens a **captive-portal** AP `printorb-setup-xxxx` (open,
`WIFI_AP_STA` so it can still scan) → DNS catch-all + `onNotFound` 302 redirect
pop up the config page → user scans WiFi, picks a network, sets printer (manual or
mDNS discover) → save reboots into STA mode (`MDNS.begin` → `printorb.local`).
After that the UI is at `http://<device-ip>/` or `http://printorb.local/`.

## Validation expectation

Any change touching `src/` or `include/` must still pass `pio run`. The current
baseline builds clean (≈46 % RAM, ≈49 % of the 3 MB app partition). On this board
serial is only visible after flashing because the monitor must own COM port; close
the monitor before `pio run -t upload`.

## History

See `docs/CONVERSATION.md` for the original request and the design decisions that
shaped this project.
