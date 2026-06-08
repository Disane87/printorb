# PrintOrb 🔮

3D-printer status display on a **Waveshare ESP32-S3-Touch-LCD-1.28** (round
240×240 LCD). Connects to either **Klipper (Moonraker)** or **Bambu Lab** and
shows progress, temperatures, remaining time and layers. Printer selection and
credentials are configured through a built-in **web server**.

```
   ╭───────────────╮
   │   My Printer  │
   │   model.gcode │
   │      63%      │     ← large progress ring (LVGL)
   │   Printing    │
   │  N 220°  B 60°│
   │  ⟳ 24m  84/132│
   ╰───────────────╯
```

---

## Features

- **Two backends, switchable at runtime:**
  - **Klipper** – HTTP polling of the Moonraker API (`/printer/objects/query`)
  - **Bambu Lab** – local MQTT over TLS (port 8883, LAN mode)
- **Web portal** (embedded in flash) to configure WiFi + printer
- **WiFi first-time setup** via access-point fallback (`PrintOrb-Setup-xxxx`)
- **Round LVGL UI**: progress ring, temperatures, remaining time, layers, file name
- **Persistence** in NVS – survives reboot and re-flash
- Live status also in the browser at `http://<ip>/`

---

## Hardware

**Waveshare ESP32-S3-Touch-LCD-1.28**
- ESP32-S3R2 (2 MB QSPI PSRAM)
- 1.28″ round LCD, 240×240, **GC9A01** (SPI)
- **CST816S** capacitive touch (I²C)

### Pinout (in `include/lgfx_device.h`)

| Function        | GPIO |
|-----------------|------|
| LCD SCLK        | 10   |
| LCD MOSI        | 11   |
| LCD MISO        | 12   |
| LCD DC          | 8    |
| LCD CS          | 9    |
| LCD RST         | 14   |
| LCD Backlight   | 40   |
| Touch SDA       | 6    |
| Touch SCL       | 7    |
| Touch INT       | 5    |
| Touch RST       | 13   |

> ⚠️ **Check the pins!** These values match the common board revision. If the
> display stays dark or the touch doesn't respond, verify the pins (especially
> **Backlight = 40**) against the Waveshare wiki for your board revision.
> Everything lives in one place: `include/lgfx_device.h`.

---

## Build & Flash

Requirement: [PlatformIO](https://platformio.org/) (CLI or VS Code extension).

```bash
# in the project folder
pio run                 # compile
pio run -t upload       # flash (connect the board via USB-C)
pio device monitor      # serial output (115200 baud)
```

The web UI is embedded in flash – **no separate LittleFS upload needed**.

> **Flash size:** The partition table only uses the first 4 MB and works on both
> 4 MB and 16 MB units. On a flash-size mismatch during upload, adjust
> `board_upload.flash_size` in `platformio.ini` to match your unit.

---

## First-time setup

1. **Flash** the firmware and power the board.
2. On first boot (no WiFi credentials) PrintOrb opens an **access point**:
   `PrintOrb-Setup-xxxx` (open). The display shows the SSID + IP.
3. Connect to the AP and open **`http://192.168.4.1`** in a browser.
4. In the **Settings** tab:
   - Enter WiFi SSID + password
   - Choose the printer type:
     - **Klipper:** printer IP, Moonraker port (default `7125`), API key optional
     - **Bambu Lab:** printer IP, **serial number**, **LAN access code**
   - Save → the device reboots and connects to your WiFi.
5. Afterwards the UI is reachable at `http://<device-ip>/` (the IP is shown in the
   serial log and briefly on the display).

### Bambu Lab – prerequisites

- Enable **LAN mode** on the printer: *Settings → WLAN → LAN mode*.
- The **serial number** and **access code** are shown there – enter both in the portal.
- Works with P1P/P1S/X1C/A1 on the local network. The printer must be reachable on
  the same network.

---

## Project structure

```
printorb/
├─ platformio.ini          Build configuration & libraries
├─ partitions.csv          Partition table (4/16 MB)
├─ include/
│  ├─ lv_conf.h            LVGL configuration
│  └─ lgfx_device.h        LovyanGFX panel/touch definition (PINS HERE)
└─ src/
   ├─ main.cpp             Setup/loop, wires everything together
   ├─ config.{h,cpp}       Settings + NVS persistence
   ├─ display.{h,cpp}      LVGL bring-up (flush + touch)
   ├─ ui.{h,cpp}           LVGL screens (status/setup/boot)
   ├─ printer.h            Shared status model + interface
   ├─ klipper_client.{h,cpp}  Moonraker HTTP client
   ├─ bambu_client.{h,cpp}    Bambu MQTT/TLS client
   ├─ wifi_manager.{h,cpp}    STA + AP fallback
   ├─ web_portal.{h,cpp}      Async web server (API)
   └─ web_index.h          Embedded config UI (HTML/CSS/JS)
```

---

## Web API

| Method | Path           | Purpose                                      |
|--------|----------------|----------------------------------------------|
| GET    | `/`            | Config/status UI                             |
| GET    | `/api/status`  | Current printer status (JSON)                |
| GET    | `/api/config`  | Current settings (without WiFi password)     |
| POST   | `/api/config`  | Save settings → reboot                       |
| POST   | `/api/restart` | Restart the device                           |

---

## Known limitations / notes

- **Bambu MQTT buffer:** the initial `pushall` report can be several KB; the buffer
  in `bambu_client.cpp` is set to 16 KB. Increase it for very large reports if
  needed (PSRAM is available).
- **TLS unvalidated:** the Bambu LAN broker uses a self-signed certificate; the
  client connects with `setInsecure()` (common on the local network).
- **Klipper layers:** `current/total layer` only appear if your slicer writes them
  to `print_stats.info` (e.g. via `SET_PRINT_STATS_INFO`).
- **AP mode** is an open network for easy first-time setup – it is no longer active
  after the initial WiFi setup.

---

## Reset

To clear the settings: call `Config::reset()` (e.g. temporarily in `setup()`),
flash, boot once, then remove it again. Afterwards the device starts back up in
the setup AP.
