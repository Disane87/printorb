/**
 * @file ui.h
 * LVGL screens for PrintOrb: a swipeable carousel of Status / Details / System
 * / Controls, plus boot and setup screens.
 */
#pragma once

#include "printer.h"
#include "config.h"

namespace UI {
    enum Ctrl { CTRL_PAUSE, CTRL_RESUME, CTRL_STOP };
    typedef void (*ControlCb)(Ctrl);

    /** Build all widgets. Call once after Display::begin(). */
    void begin();

    /** Register the handler invoked by the on-screen control buttons. */
    void setControlHandler(ControlCb cb);

    /** Refresh all live carousel screens from a PrinterStatus (no screen switch). */
    void update(const PrinterStatus& s, const String& printerLabel);

    /** Show a fullscreen setup hint (AP mode / not configured). */
    void showSetup(const String& ssid, const String& ip);

    /**
     * Show the boot/splash screen: orb logo, a progress bar and the current
     * step. `pct` is 0..100; `detail` is an optional second line (e.g. SSID).
     */
    void showBoot(const char* step, uint8_t pct, const char* detail = "");
}
