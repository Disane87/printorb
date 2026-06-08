/**
 * @file ui.h
 * LVGL screens for PrintOrb. One status screen + a setup-hint screen.
 */
#pragma once

#include "printer.h"
#include "config.h"

namespace UI {
    /** Build all widgets. Call once after Display::begin(). */
    void begin();

    /** Refresh the status screen from a PrinterStatus. */
    void showStatus(const PrinterStatus& s, const String& printerLabel);

    /** Show a fullscreen setup hint (AP mode / not configured). */
    void showSetup(const String& ssid, const String& ip);

    /** Show a transient connecting/boot message. */
    void showMessage(const String& title, const String& subtitle);
}
