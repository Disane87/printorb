/**
 * @file drying.h
 * Filament → safe drying profile lookup for the AMS HT.
 *
 * The AMS HT is a single-slot heated dryer (max ~85 °C). To avoid deforming a
 * sensitive spool we pick a conservative temperature/duration per filament type:
 * the value is chosen so the *most sensitive* (lowest-melting) material that
 * carries this type label survives. Temperatures are hard-clamped to 45..85 °C
 * (the HT's usable range; the start command is rejected by firmware below 45).
 */
#pragma once

#include <Arduino.h>

namespace Drying {

    // Usable AMS HT temperature window.
    static const int TEMP_MIN = 45;
    static const int TEMP_MAX = 85;

    struct Profile {
        int tempC;     // target / cooling temperature (°C), clamped to [45, 85]
        int hours;     // drying duration (whole hours)
    };

    /**
     * Map a Bambu `tray_type` string (e.g. "PLA", "PETG", "PA-CF") to a safe
     * drying profile. Matching is case-insensitive and substring-based; specific
     * tokens are checked before ambiguous ones (e.g. "PLA" before "PA"). Unknown
     * or empty types fall back to the safest profile (45 °C).
     */
    inline Profile profileForType(const String& typeIn) {
        String t = typeIn;
        t.toUpperCase();

        auto has = [&](const char* needle) { return t.indexOf(needle) >= 0; };

        Profile p;
        if      (has("PLA") || has("PVA") || has("TPU"))      p = { 45, 8 };
        else if (has("PETG") || has("PCTG") || has("PET"))    p = { 65, 6 };
        else if (has("ASA") || has("ABS"))                    p = { 80, 4 };
        else if (has("NYLON") || has("PAHT") || has("PA"))    p = { 80, 12 };
        else if (has("PC"))                                   p = { 85, 8 };
        else                                                  p = { 45, 6 };  // unknown

        if (p.tempC < TEMP_MIN) p.tempC = TEMP_MIN;
        if (p.tempC > TEMP_MAX) p.tempC = TEMP_MAX;
        return p;
    }
}
