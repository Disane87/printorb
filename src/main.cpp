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
#include "timekeeper.h"
#include "ota.h"
#include "updater.h"
#include "drying.h"

static PrinterClient* printer = nullptr;
static esp_timer_handle_t lvTickTimer = nullptr;

static const uint32_t UI_REFRESH_MS = 500;
static uint32_t lastUiMs = 0;

static bool screenAsleep = false;

// LVGL needs a millisecond tick fed independently of loop() timing.
static void lvTick(void*) { lv_tick_inc(1); }

// Start/stop AMS HT drying for the first HT unit, picking a safe temperature
// from the loaded filament. Shared by the touch UI and the web endpoint.
static void doDry(bool start) {
    if (!printer) return;
    const AmsInfo& a = printer->status().ams;
    for (uint8_t i = 0; i < a.units; i++) {
        const AmsUnit& U = a.unit[i];
        if (!U.isHT) continue;
        if (!start) { printer->stopDrying(U.rawId); return; }
        if (U.count == 0 || !U.slot[0].present) {     // nothing to dry
            Log::printf("[Dry] HT empty — start ignored\n");
            return;
        }
        Drying::Profile p = Drying::profileForType(U.slot[0].type);
        printer->startDrying(U.rawId, p.tempC, p.hours);
        return;
    }
    Log::printf("[Dry] no AMS HT present\n");
}

// Routes on-screen control buttons to the active printer client.
static void onControl(UI::Ctrl c) {
    if (!printer) return;
    if      (c == UI::CTRL_PAUSE)      printer->pause();
    else if (c == UI::CTRL_RESUME)     printer->resume();
    else if (c == UI::CTRL_STOP)       printer->stop();
    else if (c == UI::CTRL_DRY_START)  doDry(true);
    else if (c == UI::CTRL_DRY_STOP)   doDry(false);
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

// True while the local time falls inside the configured nightly dim window.
// Crosses midnight when start > end. Inactive (false) until NTP has synced.
static bool inDimWindow() {
    if (!cfg.dimSchedEnabled || cfg.dimStartMin == cfg.dimEndMin) return false;
    int now = Time::localMinutes();
    if (now < 0) return false;                 // no clock yet -> no dimming
    uint16_t a = cfg.dimStartMin, b = cfg.dimEndMin;
    return (a < b) ? (now >= a && now < b)     // same-day window
                   : (now >= a || now < b);    // overnight window
}

// The brightness the screen should show while awake: dimmed inside the schedule
// window, otherwise the user's normal brightness.
static uint8_t baseBrightness() {
    return inDimWindow() ? cfg.dimBrightness : cfg.brightness;
}

// Wake from sleep: clear the flag, reset the idle timer and swallow the tap.
static void wakeScreen() {
    screenAsleep = false;
    lv_disp_trig_activity(NULL);          // so it doesn't re-sleep at once
    Display::suppressTouchUntilRelease(); // the tap that woke us triggers nothing
}

// Single owner of the backlight. Combines three inputs into one effective level:
//   - manual brightness (cfg.brightness)
//   - scheduled dimming  (cfg.dim*) -> baseBrightness()
//   - inactivity auto-off (power-save) -> 0 while asleep
// Auto-off only runs while the printer is resting AND the toggle is on. WiFi and
// polling keep running, so a starting print or a touch wakes the screen.
static void serviceBrightness(const PrinterStatus& s) {
    static int lastApplied = -1;

    bool sleepEnabled = cfg.screenSleepEnabled && cfg.screenTimeoutSec > 0;
    if (sleepEnabled && UI::isResting(s.state)) {
        if (!screenAsleep &&
            lv_disp_get_inactive_time(NULL) >= (uint32_t)cfg.screenTimeoutSec * 1000) {
            screenAsleep = true;
            Log::printf("[Sleep] display off\n");
        } else if (screenAsleep && Display::touched()) {
            Log::printf("[Sleep] wake (touch)\n");
            wakeScreen();
        }
    } else if (screenAsleep) {
        wakeScreen();                     // feature off or print active -> wake
    }

    uint8_t eff = screenAsleep ? 0 : baseBrightness();
    if ((int)eff != lastApplied) {
        lastApplied = eff;
        Display::setBrightness(eff);
    }
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

    // Start NTP once we're on the LAN so the dim schedule has a clock to work
    // against. In AP/setup mode there's no upstream network, so we skip it.
    if (WifiManager::mode() == WifiManager::Mode::STA) {
        Time::begin(cfg.timezone);
        Ota::begin();   // enable `pio ... espota` flashing over WiFi
        Updater::begin();   // boot + daily GitHub release check (gated by config)
    }

    // Web portal runs in both AP and STA mode.
    UI::showBoot("Web portal", 85);
    lv_timer_handler();
    WebPortal::begin(nullptr);
    WebPortal::setDryHandler(doDry);

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
    WifiManager::loop();
    Ota::loop();
    Updater::loop();   // performs a queued GitHub OTA pull, then daily checks

    // A browser firmware upload runs in the async server task; it only writes a
    // progress percentage. Render the on-screen update screen here, in the LVGL
    // thread, while the upload streams in.
    int otaPct = WebPortal::otaProgress();
    if (otaPct >= 0) {
        UI::showUpdate((uint8_t)otaPct);
        lv_timer_handler();
        delay(2);
        return;
    }

    if (printer) {
        printer->loop();
        const PrinterStatus& s = printer->status();

        // Run before lv_timer_handler so a wake-up tap is swallowed before LVGL
        // processes it as a button press or swipe.
        serviceBrightness(s);

        uint32_t now = millis();
        if (now - lastUiMs >= UI_REFRESH_MS) {
            lastUiMs = now;
            UI::update(s, printerLabel());
            WebPortal::updateStatus(s, printerLabel());
        }
    }

    lv_timer_handler();
    delay(2);  // yield to WiFi / async stacks
}
