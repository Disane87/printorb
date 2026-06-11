# PrintOrb 🔮

3D-printer status display on a **Waveshare ESP32-S3-Touch-LCD-1.28** (round
240×240 LCD). Connects to either **Klipper (Moonraker)** or **Bambu Lab** and
shows progress, temperatures, remaining time, layers and — for Bambu — the
**AMS** filament state. Everything is configured through a built-in **web
portal**; the round screen is a **swipeable, touch-driven carousel**.

```
   ╭───────────────╮
   │   My Printer  │
   │   model.gcode │
   │      63%      │     ← large progress ring (LVGL)
   │   Printing    │
   │  🌡220°  ▦60° │
   │  ⟳ 24m  84/132│
   ╰───────────────╯
     ●  ○  ○  ○  ○      ← swipe left/right between screens
```

---

## Features

- **Two backends, switchable at runtime:**
  - **Klipper** – HTTP polling of the Moonraker API
  - **Bambu Lab** – local MQTT over TLS (port 8883, LAN mode)
- **Touch carousel** – swipe left/right between screens:
  1. **Status** – progress ring, temps, time, layers (with MDI icons)
  2. **Details** – state, file, ETA (minimal & airy)
  3. **System** – WiFi/RSSI, IP, brightness (−/+ by touch)
  4. **Filament (Bambu/AMS)** – colored slot tiles, active slot, humidity;
     **swipe up/down to switch AMS units** (vertical dot indicator)
  5. **Control** – Pause / Resume / **hold-to-Stop**
- **Bambu AMS** – per-slot filament type, color and remaining %, active tray
  highlight, humidity, multiple AMS units
- **Web portal** (embedded in flash): live status, settings, and a **live device
  log** for browser debugging
- **First-time setup** via a **captive-portal** AP (`printorb-setup-xxxx`) with a
  **WiFi network scan** and printer **mDNS discovery**
- **mDNS** – reachable as `printorb.local`; printer address accepts an IP **or**
  a hostname (`.local`). Hostname is configurable.
- **Persistence** in NVS – survives reboot and re-flash

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
| LCD Backlight   | **2**|
| Touch SDA       | 6    |
| Touch SCL       | 7    |
| Touch INT       | 5    |
| Touch RST       | 13   |

> ⚠️ **Backlight is GPIO 2** on this board revision. Some other Waveshare
> revisions use GPIO 40 — if the screen stays dark, that's the first thing to
> check (`ORB_PIN_LCD_BL` in `include/lgfx_device.h`).

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

### Flash from the browser (no toolchain) ⚡

Every push to `main` builds the firmware and publishes a **web flasher** to GitHub
Pages: **[disane87.github.io/printorb](https://disane87.github.io/printorb/)**.
Open it in **Chrome / Edge / Opera** on desktop, plug the board in via USB-C and
click **Connect & Install** — it flashes the merged image over Web Serial, no
PlatformIO needed. The same merged binary is attached to every
[GitHub Release](https://github.com/Disane87/printorb/releases).

> **Serial:** the USB-C port is wired through the on-board **CH343 UART bridge**,
> so `Serial` is routed to UART0 (`ARDUINO_USB_CDC_ON_BOOT=0` in
> `platformio.ini`). The serial monitor and the web log both show the same
> output.

> **Flash size:** the partition table only uses the first 4 MB and works on both
> 4 MB and 16 MB units. On a flash-size mismatch during upload, adjust
> `board_upload.flash_size` in `platformio.ini` to match your unit.

---

## First-time setup

1. **Flash** the firmware and power the board.
2. On first boot (no WiFi credentials) PrintOrb opens a **captive-portal access
   point**: `printorb-setup-xxxx` (open). Connecting with a phone should pop up
   the "sign in to network" page automatically; otherwise open
   **`http://192.168.4.1`**.
3. In the **Settings** tab:
   - **Scan** for your WiFi and pick the network, enter the password
   - Optionally set a **hostname** (default `printorb` → `printorb.local`)
   - Choose the printer type:
     - **Klipper:** IP/hostname, Moonraker port (default `7125`), API key optional
     - **Bambu Lab:** IP/hostname, **serial number**, **LAN access code**
   - Or use **Discover (mDNS)** to find a Klipper/Bambu printer on the LAN
   - Save → the device reboots and connects.
4. Afterwards the UI is reachable at `http://<device-ip>/` or
   `http://printorb.local/`.

### Bambu Lab – prerequisites

- Enable **LAN mode** on the printer; the **serial number** and **access code**
  are shown there.
- Works with P1/X1/A1/H2 over the local network for status + AMS. (Live camera is
  **not** supported on-device — see Known limitations.)

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
   ├─ main.cpp             Setup/loop, boot sequence, wiring
   ├─ config.{h,cpp}       Settings + NVS persistence
   ├─ display.{h,cpp}      LVGL bring-up (flush + touch)
   ├─ ui.{h,cpp}           LVGL screens: boot, setup, and the touch carousel
   ├─ orb_icons.{h,c}      Embedded Material Design icon font (nozzle/bed/…)
   ├─ printer.h            Shared status model (+ AMS) + client interface
   ├─ klipper_client.{h,cpp}  Moonraker HTTP client (+ pause/resume/stop)
   ├─ bambu_client.{h,cpp}    Bambu MQTT/TLS client (+ AMS, controls)
   ├─ wifi_manager.{h,cpp}    STA + captive AP, DNS, mDNS, host resolution
   ├─ web_portal.{h,cpp}      Async web server (API + scan/discover/log)
   ├─ web_index.h          Embedded config UI (HTML/CSS/JS)
   └─ logbuf.{h,cpp}       In-memory log ring buffer (served at /api/log)
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
| GET    | `/api/scan`    | Async WiFi network scan (JSON)               |
| GET    | `/api/discover`| mDNS discovery of Klipper/Bambu printers     |
| GET    | `/api/log`     | Live device log (plain text)                 |

---

## Known limitations / notes

- **Camera:** not available on-device. The Bambu **H2 series streams H.264 over
  RTSPS (port 322)**, which the ESP32-S3 cannot decode. The simple JPEG chamber
  protocol (port 6000) only exists on X1/P1/A1. A live view would require an
  external transcoding proxy (e.g. go2rtc/ffmpeg → JPEG snapshots).
- **Bambu MQTT buffer:** the initial `pushall` report (with AMS) is large —
  ~32 KB observed — so the PubSubClient buffer is set to **48 KB** in
  `bambu_client.cpp`. Too small a buffer makes PubSubClient silently drop the
  report and the status stays "Offline".
- **TLS unvalidated:** the Bambu LAN broker uses a self-signed certificate; the
  client connects with `setInsecure()` (common on the local network).
- **Klipper layers:** `current/total layer` only appear if your slicer writes
  them to `print_stats.info` (e.g. via `SET_PRINT_STATS_INFO`).
- **mDNS discovery** only works in STA mode (on your LAN). In first-time AP setup
  enter the IP/hostname manually; resolution then works after reboot.
- **AP mode** is an open network for easy setup and is not active after the
  initial WiFi setup.

---

## Reset

To clear the settings: call `Config::reset()` (e.g. temporarily in `setup()`),
flash, boot once, then remove it again. Afterwards the device starts back up in
the setup AP.

---

## Releases & versioning

Versioning is automated with **[semantic-release](https://semantic-release.gitbook.io/)**
driven by **[Conventional Commits](https://www.conventionalcommits.org/)**. On every
push to `main`, CI (`.github/workflows/release-and-deploy.yml`) builds the firmware,
then semantic-release decides the next version from the commit messages, writes
`CHANGELOG.md`, tags the repo and creates a GitHub Release with the firmware
attached — and the web flasher on GitHub Pages is redeployed with that version.

Commit message prefixes that drive the version bump:

| Prefix | Example | Release |
|--------|---------|---------|
| `fix:` | `fix: correct AMS slot color mapping` | patch (`x.y.Z`) |
| `feat:` | `feat: add scheduled display dimming` | minor (`x.Y.0`) |
| `feat!:` / `BREAKING CHANGE:` footer | `feat!: drop legacy config keys` | major (`X.0.0`) |
| `chore:` / `docs:` / `refactor:` / `ci:` … | `docs: update wiring notes` | no release |

So: phrase commits as `type: summary` and the changelog + releases take care of
themselves. No conventional commits since the last tag → no new release (the Pages
site just redeploys at the current version).
