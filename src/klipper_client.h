/**
 * @file klipper_client.h
 * Klipper status via the Moonraker HTTP API (polling).
 */
#pragma once

#include "printer.h"

class KlipperClient : public PrinterClient {
public:
    KlipperClient(const String& host, uint16_t port, const String& apiKey = "")
        : _host(host), _port(port), _apiKey(apiKey) {}

    void begin() override;
    void loop() override;

private:
    void poll();

    String   _host;
    uint16_t _port;
    String   _apiKey;
    uint32_t _lastPollMs = 0;
    static const uint32_t POLL_INTERVAL_MS = 2000;
};
