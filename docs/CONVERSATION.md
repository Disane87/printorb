# Conversation history — How PrintOrb came to be

This document records the original request and the design decisions that led to
this project.

---

## 1. Original request (user)

> Please create a PlatformIO project for an **ESP32-S3-Touch-LCD-1.28** that can
> connect to either **Klipper** or **Bambu Lab** printers (configurable through a
> **web server**) and then displays the status.

## 2. Follow-up requests (user)

> Please also come up with a cool name for the project and put it where all my
> other projects live.

> Please also set the project up for Claude with a CLAUDE.md etc. and include the
> conversation so far.

---

## 3. Response / implementation (Claude)

### Naming
The user's projects live under `C:\Workspace` (lowercase). Already taken:
`filapulse`, `klipper-sphere`. A vendor-neutral name was chosen (the device
supports Klipper **and** Bambu), fitting the round 1.28″ display:

→ **PrintOrb** 🔮 — folder `C:\Workspace\printorb`.

### Design decisions (deliberate defaults instead of many follow-up questions)

| Topic | Decision | Rationale |
|-------|----------|-----------|
| Display/touch | LovyanGFX (GC9A01 + CST816S) | proven for exactly this board |
| GUI | LVGL 8 | ideal for a round display, many examples |
| Config storage | NVS (`Preferences`) | survives reboot & re-flash |
| Web UI | embedded in flash (PROGMEM) | one flash step, no LittleFS upload |
| WiFi | STA + AP fallback `PrintOrb-Setup-xxxx` | first-time setup without hardcoding |
| Klipper | Moonraker HTTP polling (2 s) | simple, robust, no WebSocket needed |
| Bambu Lab | local MQTT over TLS (port 8883, LAN mode) | the official local path |
| Web server | ESPAsyncWebServer | non-blocking alongside LVGL |

### Architecture
Shared status model `PrinterStatus` + abstract interface `PrinterClient`.
Depending on `cfg.printerType`, either `KlipperClient` **or** `BambuClient` is
created at runtime. `main.cpp` polls the active client and pushes the status every
~500 ms into both the LVGL UI and the web server.

### Files implemented
`platformio.ini`, `partitions.csv`, `.gitignore`, `include/lv_conf.h`,
`include/lgfx_device.h`, plus in `src/`: `main.cpp`, `config.{h,cpp}`,
`printer.h`, `klipper_client.{h,cpp}`, `bambu_client.{h,cpp}`, `display.{h,cpp}`,
`ui.{h,cpp}`, `wifi_manager.{h,cpp}`, `web_portal.{h,cpp}`, `web_index.h`.
Plus `README.md` and this `CLAUDE.md` / `docs/CONVERSATION.md`.

### Build verification
`pio run` compiles successfully. Issues fixed on the first run:
1. A PlatformIO env name must not contain a dot → `esp32-s3-touch-lcd-128`.
2. ArduinoJson v7: `auto p = filter["print"]` copies a non-copyable
   `MemberProxy` → replaced with `JsonObject p = filter["print"].to<JsonObject>()`
   (in `klipper_client.cpp` and `bambu_client.cpp`).
3. `send_P()` deprecated → `send(...)` (flash is memory-mapped on ESP32).
4. `LGFX_USE_V1` redefinition → guarded with `#ifndef`.

Result baseline: RAM ≈ 41.8 %, flash ≈ 41.8 % of the 3 MB app partition.

### Open to-dos / notes for the user
- **Backlight pin (GPIO 40)** — verify against your board revision
  (in `include/lgfx_device.h`, if the display stays dark).
- **Bambu:** enable LAN mode on the printer; serial number + access code required.
- Possible extensions: IMU (QMI8658) for display rotation, touch gestures to
  switch between multiple printers.
