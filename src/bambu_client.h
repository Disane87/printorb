/**
 * @file bambu_client.h
 * Bambu Lab status via local MQTT over TLS (LAN mode required).
 *
 *   Broker : mqtts://<printer-ip>:8883
 *   User   : bblp
 *   Pass   : <LAN access code>   (Settings > WLAN on the printer)
 *   Topic  : device/<serial>/report   (subscribe)
 *            device/<serial>/request  (publish pushall on connect)
 *
 * Requires LAN-only / "LAN Mode" liveview to be enabled on the printer.
 */
#pragma once

#include "printer.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

class BambuClient : public PrinterClient {
public:
    BambuClient(const String& host, const String& serial, const String& accessCode)
        : _host(host), _serial(serial), _code(accessCode), _mqtt(_net) {}

    void begin() override;
    void loop() override;

    /** Static trampoline target. */
    void onMessage(char* topic, uint8_t* payload, unsigned int len);

private:
    bool reconnect();
    void requestPushAll();

    String              _host;
    String              _serial;
    String              _code;
    WiFiClientSecure    _net;
    PubSubClient        _mqtt;

    uint32_t _lastReconnectMs = 0;
    uint32_t _lastPushAllMs   = 0;
    static const uint32_t RECONNECT_INTERVAL_MS = 5000;
    static const uint32_t PUSHALL_INTERVAL_MS   = 30000;
};
