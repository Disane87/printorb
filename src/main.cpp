/**
 *  ____       _       _    ___       _
 * |  _ \ _ __(_)_ __ | |_ / _ \ _ __| |__
 * | |_) | '__| | '_ \| __| | | | '__| '_ \
 * |  __/| |  | | | | | |_| |_| | |  | |_) |
 * |_|   |_|  |_|_| |_|\__|\___/|_|  |_.__/
 *
 *  PrintOrb — 3D printer status on an ESP32-S3-Touch-LCD-1.28 round display.
 *  Connects to either Klipper (Moonraker) or Bambu Lab; configurable via the
 *  built-in web portal.
 */
#include <Arduino.h>
#include <lvgl.h>
#include <esp_timer.h>

#include "config.h"
#include "display.h"
#include "ui.h"
#include "wifi_manager.h"
#include "web_portal.h"
#include "printer.h"
#include "klipper_client.h"
#include "bambu_client.h"

static PrinterClient* printer = nullptr;
static esp_timer_handle_t lvTickTimer = nullptr;

static const uint32_t UI_REFRESH_MS = 500;
static uint32_t lastUiMs = 0;

// LVGL needs a millisecond tick fed independently of loop() timing.
static void lvTick(void*) { lv_tick_inc(1); }

static String printerLabel() {
    String name = cfg.printerName.length() ? cfg.printerName : String("Printer");
    return name;
}

static void createPrinter() {
    if (printer) { delete printer; printer = nullptr; }

    if (cfg.printerType == PrinterType::BAMBU) {
        printer = new BambuClient(cfg.printerIp, cfg.bambuSerial, cfg.bambuAccessCode);
    } else {
        printer = new KlipperClient(cfg.printerIp, cfg.moonrakerPort, cfg.moonrakerApiKey);
    }
    printer->begin();
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[PrintOrb] boot");

    // --- Display + LVGL ---
    Display::begin();
    UI::begin();
    Config::load();
    Display::setBrightness(cfg.brightness);

    // 1 ms LVGL tick
    const esp_timer_create_args_t targs = {
        .callback = &lvTick, .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK, .name = "lv_tick"
    };
    esp_timer_create(&targs, &lvTickTimer);
    esp_timer_start_periodic(lvTickTimer, 1000);  // 1000 us

    UI::showMessage("PrintOrb", "connecting WiFi...");
    lv_timer_handler();

    // --- WiFi (STA with AP fallback) ---
    WifiManager::begin();

    // Web portal runs in both AP and STA mode.
    WebPortal::begin(nullptr);

    if (WifiManager::mode() == WifiManager::Mode::AP) {
        UI::showSetup(WifiManager::apSsid(), "192.168.4.1");
    } else if (!cfg.isConfigured()) {
        UI::showSetup("connected", WifiManager::ip());
    } else {
        createPrinter();
        UI::showMessage(printerLabel(), "connecting...");
    }

    Serial.printf("[PrintOrb] mode=%d ip=%s\n",
                  (int)WifiManager::mode(), WifiManager::ip().c_str());
}

void loop() {
    lv_timer_handler();
    WifiManager::loop();

    if (printer) {
        printer->loop();

        uint32_t now = millis();
        if (now - lastUiMs >= UI_REFRESH_MS) {
            lastUiMs = now;
            const PrinterStatus& s = printer->status();
            UI::showStatus(s, printerLabel());
            WebPortal::updateStatus(s, printerLabel());
        }
    }

    delay(2);  // yield to WiFi / async stacks
}
