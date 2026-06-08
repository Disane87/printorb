/**
 * @file wifi_manager.h
 * WiFi connection with captive AP fallback for first-time setup.
 */
#pragma once

#include <Arduino.h>

namespace WifiManager {
    enum class Mode { CONNECTING, STA, AP };

    /** Try saved credentials; fall back to an AP if none/failed. */
    void begin();
    /** Service reconnects; call from loop(). */
    void loop();

    Mode   mode();
    String ip();          // current IP (STA or AP)
    String apSsid();      // SSID of the setup AP
    bool   isConnected(); // true when in STA and associated
}
