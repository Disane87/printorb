/**
 * @file wifi_manager.h
 * WiFi connection with a captive-portal AP fallback for first-time setup,
 * plus mDNS host resolution for printer addresses.
 */
#pragma once

#include <Arduino.h>

namespace WifiManager {
    enum class Mode { CONNECTING, STA, AP };

    /** Called repeatedly while blocking (e.g. to keep the UI animated). */
    typedef void (*WaitCb)();

    /** Try saved credentials; fall back to a captive AP if none/failed. */
    void begin(WaitCb onWait = nullptr);
    /** Service reconnects and the captive DNS; call from loop(). */
    void loop();

    Mode   mode();
    String ip();          // current IP (STA or AP)
    String apSsid();      // SSID of the setup AP
    bool   isConnected(); // true when in STA and associated

    /**
     * Resolve an address that may be an IP literal or a hostname. Hostnames
     * ending in ".local" are looked up via mDNS; others via regular DNS.
     * Returns the resolved dotted IP, or the original string on failure.
     */
    String resolveHost(const String& hostOrIp);
}
