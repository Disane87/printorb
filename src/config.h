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

    // --- UI ---
    uint8_t  brightness = 100;        // 0..100 (%)

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
