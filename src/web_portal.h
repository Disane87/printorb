/**
 * @file web_portal.h
 * Async web server: configuration UI + live status JSON API.
 */
#pragma once

#include <Arduino.h>
#include "printer.h"

namespace WebPortal {
    typedef void (*ConfigSavedCb)();
    typedef void (*DryHandler)(bool start);   // AMS HT drying toggle

    /** Start the HTTP server. `onSaved` is invoked after settings are stored. */
    void begin(ConfigSavedCb onSaved);

    /** Register the handler invoked by POST /api/dry (start/stop AMS HT drying). */
    void setDryHandler(DryHandler cb);

    /** Push the latest status so /api/status can serve it. */
    void updateStatus(const PrinterStatus& s, const String& label);

    /**
     * OTA upload progress for the on-screen update screen. Returns -1 when no
     * browser firmware upload is in flight, otherwise 0..100. Written from the
     * async server task, read from the main loop (single int, lock-free).
     */
    int otaProgress();
}
