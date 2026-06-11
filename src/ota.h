/**
 * @file ota.h
 * Over-the-air firmware update via ArduinoOTA (PlatformIO `espota`). Lets
 * `pio run -e ...-ota -t upload` push firmware over WiFi instead of USB.
 * Browser-based upload lives in the web portal (`/api/update`).
 */
#pragma once

namespace Ota {
    /** Start ArduinoOTA. Call once after WiFi STA is up. */
    void begin();

    /** Service pending OTA pushes. Call frequently from loop(). No-op until begun. */
    void loop();
}
