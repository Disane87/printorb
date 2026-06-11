/**
 * @file config.h
 * Persistent device settings, stored in NVS (Preferences).
 */
#pragma once

#include <Arduino.h>

enum class PrinterType : uint8_t {
    KLIPPER = 0,
    BAMBU   = 1,
};

struct OrbConfig {
    // --- WiFi ---
    String wifiSsid;
    String wifiPass;

    // --- Network identity (DHCP hostname + mDNS name + AP SSID base) ---
    String hostname = "printorb";

    // --- Printer selection ---
    PrinterType printerType = PrinterType::KLIPPER;
    String      printerName;          // friendly label shown on screen
    String      printerIp;

    // --- Klipper / Moonraker ---
    uint16_t moonrakerPort = 7125;
    String   moonrakerApiKey;         // optional, usually empty on LAN

    // --- Bambu Lab (LAN mode) ---
    String   bambuSerial;             // device serial, e.g. 01P00A...
    String   bambuAccessCode;         // LAN access code from printer screen

    // --- Security ---
    // Gates firmware updates (web /api/update HTTP-Basic + ArduinoOTA password).
    // Empty = OTA disabled (secure default); set it in the web UI to enable.
    String   adminPassword;

    // --- UI ---
    uint8_t  brightness = 100;        // 0..100 (%)
    uint16_t screenTimeoutSec = 120;  // blank the display after N s of inactivity
                                      // while no print is active; 0 = never sleep
    bool     screenSleepEnabled = true; // explicit on/off for the inactivity auto-off

    // --- Time / scheduled dimming (needs NTP; only active in STA mode) ---
    String   timezone;                // POSIX TZ string ("" = UTC). DST handled by libc.
    bool     dimSchedEnabled = false; // reduce brightness during a nightly window
    uint16_t dimStartMin = 22 * 60;   // window start, minutes since local midnight (0..1439)
    uint16_t dimEndMin   =  7 * 60;   // window end (start>end = crosses midnight)
    uint8_t  dimBrightness = 20;      // brightness % inside the window (0..100; 0 = off)

    bool hasWifi() const { return wifiSsid.length() > 0; }
    bool isConfigured() const {
        if (!hasWifi() || printerIp.length() == 0) return false;
        if (printerType == PrinterType::BAMBU)
            return bambuSerial.length() > 0 && bambuAccessCode.length() > 0;
        return true;
    }
};

namespace Config {
    /** Load settings from NVS into the global `cfg`. */
    void load();
    /** Persist the global `cfg` to NVS. */
    void save();
    /** Wipe all stored settings. */
    void reset();

    const char* printerTypeStr(PrinterType t);
    PrinterType printerTypeFromStr(const String& s);
}

// Global singleton.
extern OrbConfig cfg;
