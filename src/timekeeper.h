/**
 * @file timekeeper.h
 * Local wall-clock time via SNTP. Needed for the scheduled-dimming window.
 * Only meaningful in STA mode; before the first sync `localMinutes()` is -1 and
 * callers must treat the schedule as inactive.
 */
#pragma once

#include <Arduino.h>

namespace Time {
    /** Start SNTP with the given POSIX TZ string ("" = UTC). Call once WiFi is up. */
    void begin(const String& posixTz);

    /** True once SNTP has delivered a plausible time. */
    bool synced();

    /** Minutes since local midnight (0..1439), or -1 if not yet synced. */
    int localMinutes();
}
