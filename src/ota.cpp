#include "ota.h"
#include "config.h"
#include "logbuf.h"
#include "ui.h"
#include <ArduinoOTA.h>
#include <lvgl.h>

namespace Ota {

static bool started = false;

void begin() {
    // No password => OTA stays off (secure default). Set one in the web UI to
    // enable; espota's `--auth` in platformio.ini must match that password.
    if (cfg.adminPassword.isEmpty()) {
        Log::printf("[OTA] disabled (no update password set)\n");
        return;
    }

    ArduinoOTA.setHostname(cfg.hostname.length() ? cfg.hostname.c_str() : "printorb");
    ArduinoOTA.setPassword(cfg.adminPassword.c_str());

    ArduinoOTA.onStart([]() {
        Log::printf("[OTA] start\n");
        UI::showUpdate(0);
        lv_timer_handler();
    });
    ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
        int pct = t ? (int)((uint64_t)p * 100 / t) : 0;
        UI::showUpdate((uint8_t)pct);
        lv_timer_handler();
    });
    ArduinoOTA.onEnd([]() { Log::printf("[OTA] done, rebooting\n"); });
    ArduinoOTA.onError([](ota_error_t e) { Log::printf("[OTA] error %u\n", (unsigned)e); });

    ArduinoOTA.begin();
    started = true;
    Log::printf("[OTA] ready at %s.local\n",
                cfg.hostname.length() ? cfg.hostname.c_str() : "printorb");
}

void loop() {
    if (started) ArduinoOTA.handle();
}

}  // namespace Ota
