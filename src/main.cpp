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
#include "logbuf.h"
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

// Routes on-screen control buttons to the active printer client.
static void onControl(UI::Ctrl c) {
    if (!printer) return;
    if      (c == UI::CTRL_PAUSE)  printer->pause();
    else if (c == UI::CTRL_RESUME) printer->resume();
    else                           printer->stop();
}

static String printerLabel() {
    String name = cfg.printerName.length() ? cfg.printerName : String("Printer");
    return name;
}

static void createPrinter() {
    if (printer) { delete printer; printer = nullptr; }

    // Accept an IP or a hostname (.local via mDNS) for the printer address.
    String host = WifiManager::resolveHost(cfg.printerIp);

    if (cfg.printerType == PrinterType::BAMBU) {
        printer = new BambuClient(host, cfg.bambuSerial, cfg.bambuAccessCode);
    } else {
        printer = new KlipperClient(host, cfg.moonrakerPort, cfg.moonrakerApiKey);
    }
    printer->begin();
}

// Fed to WifiManager during the blocking STA connect so the boot screen stays
// animated and shows the SSID with moving dots.
static void bootPump() {
    static uint32_t lastDot = 0;
    static uint8_t dots = 0;
    uint32_t now = millis();
    if (now - lastDot > 350) {
        lastDot = now;
        dots = (dots + 1) % 4;
        static const char* d[4] = {"", ".", "..", "..."};
        String detail = cfg.wifiSsid + " " + d[dots];
        UI::showBoot("Connecting to WiFi", 55, detail.c_str());
    }
    lv_timer_handler();
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Log::printf("\n[PrintOrb] boot\n");

    // --- Display + LVGL ---
    Display::begin();
    Config::load();   // before UI::begin so the carousel knows the printer type (AMS screen)
    UI::begin();      // boot screen at 0 %
    UI::setControlHandler(onControl);

    // 1 ms LVGL tick (needed for animations during the boot sequence)
    const esp_timer_create_args_t targs = {
        .callback = &lvTick, .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK, .name = "lv_tick"
    };
    esp_timer_create(&targs, &lvTickTimer);
    esp_timer_start_periodic(lvTickTimer, 1000);  // 1000 us

    UI::showBoot("Display ready", 12);
    Display::setBrightness(cfg.brightness);
    lv_timer_handler();

    UI::showBoot("Loading settings", 25);
    lv_timer_handler();

    // --- WiFi (STA with captive-AP fallback) ---
    UI::showBoot("Starting WiFi", 45, cfg.hasWifi() ? cfg.wifiSsid.c_str() : "");
    lv_timer_handler();
    WifiManager::begin(bootPump);

    // Web portal runs in both AP and STA mode.
    UI::showBoot("Web portal", 85);
    lv_timer_handler();
    WebPortal::begin(nullptr);

    if (WifiManager::mode() == WifiManager::Mode::AP) {
        UI::showSetup(WifiManager::apSsid(), WifiManager::ip());
    } else if (!cfg.isConfigured()) {
        UI::showSetup("connected", WifiManager::ip());
    } else {
        UI::showBoot("Connecting to printer", 95, printerLabel().c_str());
        lv_timer_handler();
        createPrinter();
    }

    Log::printf("[PrintOrb] mode=%d ip=%s\n",
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
            UI::update(s, printerLabel());
            WebPortal::updateStatus(s, printerLabel());
        }
    }

    delay(2);  // yield to WiFi / async stacks
}
