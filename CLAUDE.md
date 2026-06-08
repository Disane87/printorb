# CLAUDE.md — PrintOrb

Guidance for Claude Code (and other AI assistants) when working in this repo.

## What this is

**PrintOrb** is ESP32-S3 firmware for the **Waveshare ESP32-S3-Touch-LCD-1.28**
(1.28″ round 240×240 LCD, GC9A01 + CST816S touch). It shows live 3D-printer status
and connects to **either Klipper (Moonraker HTTP) or Bambu Lab (local MQTT/TLS)**.
The backend and all credentials are configured at runtime through an embedded web
portal — no recompile needed to switch printers.

## Tech stack

- **Framework:** Arduino on ESP32-S3 (PlatformIO, `espressif32@^6.9.0`)
- **Display/Touch:** LovyanGFX (`lovyan03/LovyanGFX`)
- **GUI:** LVGL 8 (`lvgl/lvgl@^8.4.0`)
- **JSON:** ArduinoJson 7 (`bblanchon/ArduinoJson`)
- **MQTT (Bambu):** PubSubClient + `WiFiClientSecure`
- **Web server:** `mathieucarbou/ESPAsyncWebServer` + `AsyncTCP`
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

## Architecture (one responsibility per file)

| File | Responsibility |
|------|----------------|
| `src/main.cpp` | `setup()`/`loop()`, LVGL tick timer, wires everything |
| `src/config.{h,cpp}` | `OrbConfig` struct + NVS load/save/reset; global `cfg` |
| `src/printer.h` | `PrinterStatus` model + abstract `PrinterClient` interface |
| `src/klipper_client.{h,cpp}` | Moonraker HTTP polling → fills `PrinterStatus` |
| `src/bambu_client.{h,cpp}` | Bambu MQTT/TLS subscribe → fills `PrinterStatus` |
| `src/display.{h,cpp}` | LovyanGFX + LVGL bring-up (flush + touch callbacks) |
| `src/ui.{h,cpp}` | LVGL screens: status / setup / boot message |
| `src/wifi_manager.{h,cpp}` | STA connect with AP fallback |
| `src/web_portal.{h,cpp}` | Async HTTP server: config + status JSON API |
| `src/web_index.h` | Embedded HTML/CSS/JS config UI (PROGMEM) |
| `include/lgfx_device.h` | **All hardware pins** + LovyanGFX panel/touch config |
| `include/lv_conf.h` | LVGL configuration |

**Data flow:** the active `PrinterClient` (Klipper or Bambu, chosen from
`cfg.printerType`) is polled in `loop()` and owns a `PrinterStatus`. Every
~500 ms `main.cpp` pushes that status to both the LVGL UI (`UI::showStatus`) and
the web portal (`WebPortal::updateStatus`). To add a backend, implement
`PrinterClient` and instantiate it in `createPrinter()`.

## Conventions

- **Comments, identifiers and docs in English.** User-facing strings on screen are
  English too.
- Settings live **only** in `OrbConfig`/NVS — don't hardcode IPs, SSIDs, or codes.
- Keep each printer backend self-contained; shared shape is `PrinterStatus`.
- ArduinoJson v7: never `auto x = doc["a"]["b"]` (MemberProxy is non-copyable).
  Use `JsonObject o = doc["a"]["b"].to<JsonObject>();` when building filters.
- Prefer streaming/filtered JSON parsing to keep RAM low (see both clients).

## Hardware gotchas (verify against your board revision)

- **Backlight = GPIO 40** is a best-guess for the common revision. If the screen
  stays dark, fix `ORB_PIN_LCD_BL` in `include/lgfx_device.h` (all pins live there).
- GC9A01 needs `invert = true` and **BGR** order (`rgb_order = false`).
- **Bambu** requires **LAN Mode** enabled on the printer; needs serial + access code.
  TLS cert is self-signed → `WiFiClientSecure::setInsecure()`.
- **Bambu MQTT buffer** is 16 KB (`MQTT_BUFFER` in `bambu_client.cpp`); the initial
  `pushall` report is large. Increase if reports get truncated (PSRAM is available).
- **Klipper layers** only appear if the slicer writes `print_stats.info`
  (`SET_PRINT_STATS_INFO CURRENT_LAYER=… TOTAL_LAYER=…`).

## Web API

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/` | config/status UI |
| GET | `/api/status` | current status JSON |
| GET | `/api/config` | current settings (WiFi password omitted) |
| POST | `/api/config` | save settings → reboot |
| POST | `/api/restart` | reboot |

## First-time setup flow

No WiFi saved → device opens AP `PrintOrb-Setup-xxxx` (open) → user connects and
opens `http://192.168.4.1` → fills WiFi + printer in **Settings** → save reboots
into STA mode. After that the UI is at `http://<device-ip>/`.

## Validation expectation

Any change touching `src/` or `include/` must still pass `pio run`. The current
baseline builds clean (≈42 % RAM, ≈42 % of the 3 MB app partition).

## History

See `docs/CONVERSATION.md` for the original request and the design decisions that
shaped this project.
