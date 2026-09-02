/**
 * @file config.h
 * Persistent device settings, stored in NVS (Preferences).
 *
 * Up to ORB_MAX_PRINTERS printers can be configured; exactly one of them is
 * "active" at a time and drives the display. See PrinterCfg / OrbConfig::printer().
 */
#pragma once

#include <Arduino.h>

enum class PrinterType : uint8_t {
    KLIPPER = 0,
    BAMBU   = 1,
};

/** How many printer slots the device stores. Bounded by NVS space and UI room. */
static const uint8_t ORB_MAX_PRINTERS = 4;

/** Connection settings for one printer. */
struct PrinterCfg {
    PrinterType type = PrinterType::KLIPPER;
    String      name;                 // friendly label shown on screen
    String      ip;                   // IP or hostname (".local" resolved via mDNS)

    // --- Klipper / Moonraker ---
    uint16_t moonrakerPort = 7125;
    String   moonrakerApiKey;         // optional, usually empty on LAN

    // --- Bambu Lab (LAN mode) ---
    String   bambuSerial;             // device serial, e.g. 01P00A...
    String   bambuAccessCode;         // LAN access code from printer screen

    /** True once this slot holds enough information to attempt a connection. */
    bool isConfigured() const {
        if (ip.length() == 0) return false;
        if (type == PrinterType::BAMBU)
            return bambuSerial.length() > 0 && bambuAccessCode.length() > 0;
        return true;
    }

    /** Display label: the user's name, falling back to the address. */
    String label() const {
        if (name.length()) return name;
        if (ip.length())   return ip;
        return String("Printer");
    }
};

struct OrbConfig {
    // --- WiFi ---
    String wifiSsid;
    String wifiPass;

    // --- Network identity (DHCP hostname + mDNS name + AP SSID base) ---
    String hostname = "printorb";

    // --- Printers ---
    PrinterCfg printers[ORB_MAX_PRINTERS];
    uint8_t    printerCount  = 0;     // populated slots, 0..ORB_MAX_PRINTERS
    uint8_t    activePrinter = 0;     // index into printers[]

    // --- Security ---
    // Gates firmware updates (web /api/update HTTP-Basic + ArduinoOTA password).
    // Empty = OTA disabled (secure default); set it in the web UI to enable.
    String   adminPassword;

    // --- UI ---
    uint8_t  brightness = 100;        // 0..100 (%)
    uint16_t screenTimeoutSec = 120;  // blank the display after N s of inactivity
                                      // while no print is active; 0 = never sleep
    bool     screenSleepEnabled = true; // explicit on/off for the inactivity auto-off

    // --- Updates ---
    bool     autoUpdateCheck = true;  // periodic GitHub release check + on-screen
                                      // / web notice. Flashing is always manual.

    // --- Time / scheduled dimming (needs NTP; only active in STA mode) ---
    String   timezone;                // POSIX TZ string ("" = UTC). DST handled by libc.
    bool     dimSchedEnabled = false; // reduce brightness during a nightly window
    uint16_t dimStartMin = 22 * 60;   // window start, minutes since local midnight (0..1439)
    uint16_t dimEndMin   =  7 * 60;   // window end (start>end = crosses midnight)
    uint8_t  dimBrightness = 20;      // brightness % inside the window (0..100; 0 = off)

    /** The printer currently shown on screen. Always a valid reference. */
    PrinterCfg& printer() {
        return printers[activePrinter < ORB_MAX_PRINTERS ? activePrinter : 0];
    }
    const PrinterCfg& printer() const {
        return printers[activePrinter < ORB_MAX_PRINTERS ? activePrinter : 0];
    }

    bool hasWifi() const { return wifiSsid.length() > 0; }

    /** True if at least one slot is usable (so the device can leave setup mode). */
    bool isConfigured() const {
        if (!hasWifi()) return false;
        for (uint8_t i = 0; i < printerCount; i++)
            if (printers[i].isConfigured()) return true;
        return false;
    }

    /** True if any configured printer is a Bambu — i.e. AMS screens are useful. */
    bool anyBambu() const {
        for (uint8_t i = 0; i < printerCount; i++)
            if (printers[i].type == PrinterType::BAMBU) return true;
        return false;
    }
};

namespace Config {
    /** Load settings from NVS into the global `cfg` (migrates v1 single-printer keys). */
    void load();
    /** Persist the global `cfg` to NVS. */
    void save();
    /** Wipe all stored settings. */
    void reset();

    /**
     * Switch the active printer and persist just that choice (cheap; avoids
     * rewriting every key on a screen tap). No-op for an out-of-range index or
     * when `idx` is already active. Returns true if the selection changed.
     */
    bool setActivePrinter(uint8_t idx);

    const char* printerTypeStr(PrinterType t);
    PrinterType printerTypeFromStr(const String& s);
}

// Global singleton.
extern OrbConfig cfg;
