/**
 * @file updater.h
 * GitHub-release update checker + confirmed self-update.
 * Checks the latest release tag (boot + every 24 h, STA only, gated by
 * cfg.autoUpdateCheck) and, on requestApply(), pulls printorb-app.bin over OTA.
 */
#pragma once

#include <Arduino.h>

namespace Updater {
    /** Start the scheduler. Call once after WiFi STA is up. */
    void begin();

    /** Non-blocking service. Call frequently from loop(). */
    void loop();

    /** True if the latest release is newer than the running build. */
    bool updateAvailable();

    /** Latest release version (no leading 'v'); "" until first successful check. */
    const String& latestVersion();

    /** Queue a GitHub OTA pull; performed from loop() in the main thread. */
    void requestApply();

    /** True while a pull is queued or in progress. */
    bool isApplying();
}
