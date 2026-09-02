/**
 * @file ui.h
 * LVGL screens for PrintOrb: a swipeable carousel of
 * Status / Job / AMS / Control / Printers / System, plus boot, setup, idle and
 * firmware-update screens.
 *
 * Navigation is gesture-driven: horizontal swipes move through the carousel
 * (wrapping at both ends), vertical swipes switch AMS units and scroll long
 * screens. Destructive actions are hold-to-confirm with an on-screen fill.
 */
#pragma once

#include "printer.h"
#include "config.h"

namespace UI {
    enum Ctrl { CTRL_PAUSE, CTRL_RESUME, CTRL_STOP, CTRL_DRY_START, CTRL_DRY_STOP };
    typedef void (*ControlCb)(Ctrl);

    /** Invoked when the user picks another printer on the Printers screen. */
    typedef void (*PrinterSwitchCb)(uint8_t index);

    /** Build all widgets. Call once after Display::begin() and Config::load(). */
    void begin();

    /** Register the handler invoked by the on-screen control buttons. */
    void setControlHandler(ControlCb cb);

    /** Register the handler invoked when another printer is selected. */
    void setPrinterSwitchHandler(PrinterSwitchCb cb);

    /** Refresh all live carousel screens from a PrinterStatus (no screen switch). */
    void update(const PrinterStatus& s);

    /**
     * Re-read the printer list from `cfg` (labels, count, active slot) and adapt
     * the carousel — the AMS page only exists while a Bambu is active, and the
     * Printers page only while more than one printer is configured.
     */
    void refreshPrinters();

    /** Brief self-clearing message overlay, e.g. after a control action. */
    void toast(const char* text);

    /**
     * "Resting" states show the idle screen and allow the display to sleep.
     * PAUSED and ERROR count as active so a paused job / error stays visible.
     */
    bool isResting(PrintState s);

    /** Show a fullscreen setup hint (AP mode / not configured). */
    void showSetup(const String& ssid, const String& ip);

    /**
     * Show the boot/splash screen: orb logo, a progress bar and the current
     * step. `pct` is 0..100; `detail` is an optional second line (e.g. SSID).
     */
    void showBoot(const char* step, uint8_t pct, const char* detail = "");

    /**
     * Fullscreen firmware-update screen (OTA, both ArduinoOTA and web upload).
     * `pct` is 0..100. Must be called from the main/LVGL thread only.
     */
    void showUpdate(uint8_t pct);
}
