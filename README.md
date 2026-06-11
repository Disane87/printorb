[![Release & Deploy](https://github.com/Disane87/printorb/actions/workflows/release-and-deploy.yml/badge.svg)](https://github.com/Disane87/printorb/actions/workflows/release-and-deploy.yml)
![GitHub all releases](https://img.shields.io/github/downloads/Disane87/printorb/total)
![GitHub issues by-label](https://img.shields.io/github/issues/Disane87/printorb/bug?color=red)
![GitHub contributors](https://img.shields.io/github/contributors/Disane87/printorb)
![Platform](https://img.shields.io/badge/platform-ESP32--S3-E7352C?logo=espressif&logoColor=white)
[![semantic-release: conventionalcommits](https://img.shields.io/badge/semantic--release-conventionalcommits-e10079?logo=semantic-release)](https://github.com/semantic-release/semantic-release)


# 🔮 PrintOrb

Hey there! 👋 PrintOrb turns a tiny round **Waveshare ESP32-S3-Touch-LCD-1.28**
into a live 3D-printer status orb that sits on your desk. It talks to either
**Klipper (Moonraker)** or **Bambu Lab** and shows you progress, temps, remaining
time, layers and — for Bambu — the full **AMS** filament state. 🖨️✨

The best part? **No recompiling to switch printers.** Everything is configured at
runtime through a built-in **web portal**, and the round screen is a **swipeable,
touch-driven carousel**. Flash it once, set it up from your browser, done. 🎉

> [!TIP]
> 🧩 **No Home Assistant. No broker. No cloud. No companion server.** PrintOrb is a
> **fully standalone** device that talks **directly** to your printer — Bambu Lab
> over **local MQTT (LAN mode)** or Klipper via the **Moonraker API**. The only
> things between the orb and your printer are your WiFi and an access code. That's
> the whole point. ✨

> [!TIP]
> Don't want to install a toolchain? Jump straight to
> [Flash from the browser](#-flash-from-the-browser-no-toolchain) — plug in the
> board, click one button, and you're running. ⚡

# ✨ What Can This Thing Do?

Glad you asked! Here's the good stuff:

- 🧩 **Fully standalone — no Home Assistant required**: the orb connects **directly**
  to your printer over your LAN. No middleware hub, no MQTT broker, no cloud account,
  no companion server to keep running.
- 🔀 **Two backends, switchable at runtime**: pick **Klipper** (HTTP polling of the
  Moonraker API) or **Bambu Lab** (local MQTT over TLS, port 8883, LAN mode) — no
  reflash to swap.
- 👆 **Touch carousel** — swipe left/right between five screens:
  1. 📊 **Status** – progress ring, temps, time, layers (with crisp MDI icons)
  2. 📄 **Details** – state, file, ETA (minimal & airy)
  3. ⚙️ **System** – WiFi/RSSI, IP, brightness (just tap −/+)
  4. 🧵 **Filament (Bambu/AMS)** – colored slot tiles, active slot, humidity;
     **swipe up/down to switch AMS units** (with a vertical dot indicator)
  5. 🎛️ **Control** – Pause / Resume / **hold-to-Stop**
- 🌈 **Full Bambu AMS view**: per-slot filament type, color and remaining %, active
  tray highlight, humidity, and multiple AMS units
- 🌐 **Web portal** (baked into flash): live status, all settings, and a **live
  device log** for browser debugging
- 📶 **First-time setup made easy**: a **captive-portal** AP (`printorb-setup-xxxx`)
  with a **WiFi network scan** and printer **mDNS discovery**
- 🏷️ **mDNS**: reachable as `printorb.local`; the printer address accepts an IP
  **or** a hostname (`.local`), and the hostname is configurable
- 💾 **Persistence in NVS**: survives reboots *and* re-flashes
- 🌙 **Scheduled dimming & power-save**: time-aware backlight dimming and idle
  sleep (NTP-synced), so your orb isn't blazing at 3 AM
- 🚀 **OTA updates**: flash new firmware over WiFi (ArduinoOTA) or right from the
  browser Update tab — first flash is USB, after that you're wireless

> [!NOTE]
> The AMS screen only appears when the printer type is **Bambu** — the carousel
> adapts itself to your backend at boot.

# 🛠️ Hardware

**Waveshare ESP32-S3-Touch-LCD-1.28**

- 🧠 ESP32-S3R2 (2 MB QSPI PSRAM)
- 🟢 1.28″ round LCD, 240×240, **GC9A01** (SPI)
- 👆 **CST816S** capacitive touch (I²C)

## Pinout (in `include/lgfx_device.h`)

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

> [!WARNING]
> **Backlight is GPIO 2** on this board revision. Some other Waveshare revisions
> use GPIO 40 — if the screen stays dark, that's the very first thing to check
> (`ORB_PIN_LCD_BL` in `include/lgfx_device.h`, where all the pins live). 🔦

# 📦 Build & Flash

You'll need [PlatformIO](https://platformio.org/) (CLI or the VS Code extension).
Then it's three commands:

```bash
# in the project folder
pio run                 # compile
pio run -t upload       # flash (connect the board via USB-C)
pio device monitor      # serial output (115200 baud)
```

The web UI is embedded in flash — **no separate LittleFS upload needed**. 🎉

## ⚡ Flash from the browser (no toolchain)

Every push to `main` builds the firmware and publishes a **web flasher** to GitHub
Pages: **[disane87.github.io/printorb](https://disane87.github.io/printorb/)**.

Open it in **Chrome / Edge / Opera** on desktop, plug the board in via USB-C and
click **Connect & Install** — it flashes the merged image over Web Serial, zero
PlatformIO required. The same merged binary is attached to every
[GitHub Release](https://github.com/Disane87/printorb/releases). 🚀

> [!NOTE]
> **Serial routing:** the USB-C port goes through the on-board **CH343 UART
> bridge**, so `Serial` is routed to UART0 (`ARDUINO_USB_CDC_ON_BOOT=0` in
> `platformio.ini`). The serial monitor and the web log show the same output.

> [!IMPORTANT]
> **Flash size:** the partition table only uses the first 4 MB and works on both
> 4 MB and 16 MB units. On a flash-size mismatch during upload, set
> `board_upload.flash_size` in `platformio.ini` to match your unit.

# 🚀 First-Time Setup

Ready to roll? Here's the whole flow:

1. **Flash** the firmware and power up the board.
2. On first boot (no WiFi saved) PrintOrb opens a **captive-portal access point**:
   `printorb-setup-xxxx` (open network). Connecting with a phone should pop up the
   "sign in to network" page automatically — otherwise just open
   **`http://192.168.4.1`**.
3. In the **Settings** tab:
   - 📶 **Scan** for your WiFi, pick the network, enter the password
   - 🏷️ Optionally set a **hostname** (default `printorb` → `printorb.local`)
   - 🖨️ Choose your printer type:
     - **Klipper:** IP/hostname, Moonraker port (default `7125`), API key optional
     - **Bambu Lab:** IP/hostname, **serial number**, **LAN access code**
   - 🔍 Or use **Discover (mDNS)** to find a Klipper/Bambu printer on your LAN
   - 💾 Save → the device reboots and connects.
4. After that the UI lives at `http://<device-ip>/` or `http://printorb.local/`. 🎉

## Bambu Lab — prerequisites

- ✅ Enable **LAN mode** on the printer; the **serial number** and **access code**
  are shown right there.
- ✅ Works with P1/X1/A1/H2 over the local network for status + AMS. (Live camera
  is **not** supported on-device — see [Known limitations](#-known-limitations--notes).)

# 🗂️ Project Structure

One responsibility per file — easy to find your way around:

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
   ├─ timekeeper.{h,cpp}      SNTP local time for scheduled dimming
   ├─ ota.{h,cpp}             ArduinoOTA (espota) WiFi flashing
   ├─ web_portal.{h,cpp}      Async web server (API + scan/discover/log/update)
   ├─ web_index.h          Embedded config UI (HTML/CSS/JS)
   └─ logbuf.{h,cpp}       In-memory log ring buffer (served at /api/log)
```

> [!TIP]
> Adding a new printer backend? Implement the `PrinterClient` interface and
> instantiate it in `createPrinter()` — the shared shape is `PrinterStatus`, so
> the UI and web portal pick it up for free. 🔌

# 🌐 Web API

| Method | Path           | Purpose                                      |
|--------|----------------|----------------------------------------------|
| GET    | `/`            | Config/status UI                             |
| GET    | `/api/status`  | Current printer status (JSON)                |
| GET    | `/api/config`  | Current settings (without WiFi password)     |
| POST   | `/api/config`  | Save settings → reboot                       |
| POST   | `/api/restart` | Restart the device                           |
| GET    | `/api/scan`    | Async WiFi network scan (JSON)               |
| GET    | `/api/discover`| mDNS discovery of Klipper/Bambu printers     |
| GET    | `/api/sysinfo` | Diagnostics (IP, memory, flash, chip, uptime, NTP) |
| GET    | `/api/log`     | Live device log (plain text)                 |
| POST   | `/api/update`  | Firmware upload (raw `.bin`, HTTP Basic auth) → reboot |

# 📡 Flashing Over WiFi (OTA)

Once your orb is on the network, you don't need the USB cable anymore. 🎉

OTA requires an **Update / OTA password** set in the web UI (Settings → Security).
With no password, OTA is **disabled** as a secure default (`Ota::begin()` returns
early and `/api/update` always 401s). The first flash is still USB.

- 💻 **Dev push:** `pio run -e esp32-s3-touch-lcd-128-ota -t upload` (ArduinoOTA /
  `espota`; set `--auth` in `platformio.ini` to the device password). Uses a
  device→host reverse TCP connection — a host firewall or crossing subnets breaks
  it, so use the browser path instead.
- 🌐 **Browser:** the Update tab uploads the raw `.bin` to `POST /api/update` with
  HTTP Basic auth (user `admin`, the OTA password). The raw body (not multipart)
  keeps memory low, so big uploads don't reset the device while the Bambu MQTT
  buffer is live.

# ⚠️ Known Limitations & Notes

- 📷 **Camera:** not available on-device. The Bambu **H2 series streams H.264 over
  RTSPS (port 322)**, which the ESP32-S3 simply cannot decode. The simple JPEG
  chamber protocol (port 6000) only exists on X1/P1/A1. A live view would need an
  external transcoding proxy (e.g. go2rtc/ffmpeg → JPEG snapshots).
- 📦 **Bambu MQTT buffer:** the initial `pushall` report (with AMS) is large —
  ~32 KB observed — so the PubSubClient buffer is set to **48 KB** in
  `bambu_client.cpp`. Too small a buffer makes PubSubClient *silently* drop the
  report and the status stays stuck on "Offline".
- 🔒 **TLS unvalidated:** the Bambu LAN broker uses a self-signed certificate; the
  client connects with `setInsecure()` (common on the local network).
- 🧱 **Klipper layers:** `current/total layer` only appear if your slicer writes
  them to `print_stats.info` (e.g. via `SET_PRINT_STATS_INFO`).
- 🔍 **mDNS discovery** only works in STA mode (on your LAN). In first-time AP
  setup, enter the IP/hostname manually; resolution then works after reboot.
- 📶 **AP mode** is an open network purely for easy setup and is not active after
  the initial WiFi configuration.

> [!CAUTION]
> The web portal is intentionally **unauthenticated** on a trusted LAN, and
> `/api/update` accepts a firmware upload — keep your orb on a network you trust,
> and set the OTA password to guard the update endpoint.

# 🔁 Reset

Need a clean slate? Call `Config::reset()` (e.g. temporarily in `setup()`), flash,
boot once, then remove it again. The device starts back up in the setup AP, ready
for a fresh config. 🧹

# 🏷️ Releases & Versioning

Versioning is fully automated with
**[semantic-release](https://semantic-release.gitbook.io/)**, driven by
**[Conventional Commits](https://www.conventionalcommits.org/)**. On every push to
`main`, CI (`.github/workflows/release-and-deploy.yml`) builds the firmware, then
semantic-release figures out the next version from your commit messages, writes
`CHANGELOG.md`, tags the repo, and creates a GitHub Release with the firmware
attached — and the web flasher on GitHub Pages is redeployed at that version. 🎉

Commit prefixes that drive the version bump:

| Prefix | Example | Release |
|--------|---------|---------|
| `fix:` | `fix: correct AMS slot color mapping` | patch (`x.y.Z`) |
| `feat:` | `feat: add scheduled display dimming` | minor (`x.Y.0`) |
| `feat!:` / `BREAKING CHANGE:` footer | `feat!: drop legacy config keys` | major (`X.0.0`) |
| `chore:` / `docs:` / `refactor:` / `ci:` … | `docs: update wiring notes` | no release |

So: phrase commits as `type: summary` and the changelog + releases take care of
themselves. No conventional commits since the last tag → no new release (the Pages
site just redeploys at the current version).

---

## 🔗 Related Projects

Building your printer stack? Check out these other projects from the same workshop:

| Project | Description |
|---------|-------------|
| [🎯 Spoolman Home Assistant](https://github.com/Disane87/spoolman-homeassistant) | Bring your [Spoolman](https://github.com/Donkie/Spoolman/) filament inventory into Home Assistant — 25+ sensors per spool, run-out predictions and low-filament alerts. |
| [🧵 Spoolman MCP](https://github.com/Disane87/spoolman-mcp) | MCP Server for Spoolman — manage your filament inventory through AI assistants like Claude. Available on [npm](https://www.npmjs.com/package/@disane-dev/spoolman-mcp). |
| [🌐 Klipper Sphere](https://github.com/Disane87/klipper-sphere) | Sibling project exploring Klipper status on round displays. |
| [📊 FilaPulse / Open Maker Database](https://github.com/Disane87/open-maker-database) | Community database & tooling for the maker ecosystem. |

# 🤝 Contributing

Want to make PrintOrb even better? Awesome — PRs and issues are very welcome! 🎉

A few things worth knowing:
- 💾 Use [conventional commits](https://www.conventionalcommits.org/en/v1.0.0/) so
  semantic-release can do its thing (`feat:`, `fix:`, `feat!:`, `chore:`, …).
- ✅ Anything touching `src/` or `include/` must still pass `pio run` — the current
  baseline builds clean (≈44 % RAM, ≈44 % of the 3 MB app partition).
- 🇬🇧 Comments, identifiers, docs and on-screen strings are all in English.
- ⚙️ Keep settings in `OrbConfig`/NVS — no hardcoded IPs, SSIDs or codes.

Curious how the project came to be? The original request and design decisions live
in [`docs/CONVERSATION.md`](docs/CONVERSATION.md). 📖

# 🎉 Cheers!

Thanks for checking out PrintOrb! If your printer now has a glowing little orb
keeping an eye on it, give the repo a ⭐ on GitHub — it really helps! 🙌

Found a bug? Got an idea? [Open an issue](https://github.com/Disane87/printorb/issues)
and let's make it better together! 🚀
