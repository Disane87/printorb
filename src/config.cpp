#include "config.h"
#include "logbuf.h"
#include <Preferences.h>

OrbConfig cfg;

static Preferences prefs;
static const char* NS = "printorb";

namespace {

// Per-printer NVS key builder: "p<idx><field>" (e.g. "p0ip"). NVS keys are
// limited to 15 characters, which these comfortably stay under.
const char* pkey(uint8_t idx, const char* field) {
    static char buf[16];
    snprintf(buf, sizeof(buf), "p%u%s", (unsigned)idx, field);
    return buf;
}

void loadPrinter(uint8_t i, PrinterCfg& p) {
    p.type            = static_cast<PrinterType>(prefs.getUChar(pkey(i, "t"), 0));
    p.name            = prefs.getString(pkey(i, "n"), "");
    p.ip              = prefs.getString(pkey(i, "a"), "");
    p.moonrakerPort   = prefs.getUShort(pkey(i, "mp"), 7125);
    p.moonrakerApiKey = prefs.getString(pkey(i, "mk"), "");
    p.bambuSerial     = prefs.getString(pkey(i, "bs"), "");
    p.bambuAccessCode = prefs.getString(pkey(i, "bc"), "");
}

void savePrinter(uint8_t i, const PrinterCfg& p) {
    prefs.putUChar (pkey(i, "t"),  static_cast<uint8_t>(p.type));
    prefs.putString(pkey(i, "n"),  p.name);
    prefs.putString(pkey(i, "a"),  p.ip);
    prefs.putUShort(pkey(i, "mp"), p.moonrakerPort);
    prefs.putString(pkey(i, "mk"), p.moonrakerApiKey);
    prefs.putString(pkey(i, "bs"), p.bambuSerial);
    prefs.putString(pkey(i, "bc"), p.bambuAccessCode);
}

// Firmware <= 1.2 stored a single printer under un-indexed keys. Pull those into
// slot 0 so an OTA update keeps working without a re-setup. Detected by the
// absence of the "pcnt" key (written by every multi-printer save).
void migrateLegacyPrinter() {
    PrinterCfg& p = cfg.printers[0];
    p.type            = static_cast<PrinterType>(prefs.getUChar("ptype", 0));
    p.name            = prefs.getString("pname", "");
    p.ip              = prefs.getString("pip", "");
    p.moonrakerPort   = prefs.getUShort("mport", 7125);
    p.moonrakerApiKey = prefs.getString("mkey", "");
    p.bambuSerial     = prefs.getString("bserial", "");
    p.bambuAccessCode = prefs.getString("bcode", "");
    cfg.printerCount  = p.ip.length() ? 1 : 0;
    cfg.activePrinter = 0;
}

}  // namespace

namespace Config {

const char* printerTypeStr(PrinterType t) {
    return t == PrinterType::BAMBU ? "bambu" : "klipper";
}

PrinterType printerTypeFromStr(const String& s) {
    return s == "bambu" ? PrinterType::BAMBU : PrinterType::KLIPPER;
}

void load() {
    prefs.begin(NS, true);  // read-only
    cfg.wifiSsid        = prefs.getString("wifiSsid", "");
    cfg.wifiPass        = prefs.getString("wifiPass", "");
    cfg.hostname        = prefs.getString("host", "printorb");
    cfg.adminPassword   = prefs.getString("adminPw", "");
    cfg.brightness      = prefs.getUChar("bright", 100);
    cfg.screenTimeoutSec = prefs.getUShort("sleep", 120);
    cfg.screenSleepEnabled = prefs.getBool("slpOn", true);
    cfg.autoUpdateCheck = prefs.getBool("autoupd", true);
    cfg.timezone        = prefs.getString("tz", "");
    cfg.dimSchedEnabled = prefs.getBool("dimOn", false);
    cfg.dimStartMin     = prefs.getUShort("dimStart", 22 * 60);
    cfg.dimEndMin       = prefs.getUShort("dimEnd", 7 * 60);
    cfg.dimBrightness   = prefs.getUChar("dimBri", 20);

    // 0xFF = key absent -> settings written by a pre-multi-printer firmware.
    uint8_t count = prefs.getUChar("pcnt", 0xFF);
    bool migrated = (count == 0xFF);
    if (migrated) {
        migrateLegacyPrinter();
    } else {
        cfg.printerCount = count > ORB_MAX_PRINTERS ? ORB_MAX_PRINTERS : count;
        for (uint8_t i = 0; i < cfg.printerCount; i++) loadPrinter(i, cfg.printers[i]);
        cfg.activePrinter = prefs.getUChar("pact", 0);
    }
    prefs.end();

    if (cfg.printerCount && cfg.activePrinter >= cfg.printerCount) cfg.activePrinter = 0;

    if (migrated) {
        Log::printf("[Config] migrated %u legacy printer(s) to slot layout\n",
                    (unsigned)cfg.printerCount);
        save();
    }
}

void save() {
    prefs.begin(NS, false);  // read-write
    prefs.putString("wifiSsid", cfg.wifiSsid);
    prefs.putString("wifiPass", cfg.wifiPass);
    prefs.putString("host", cfg.hostname);
    prefs.putString("adminPw", cfg.adminPassword);
    prefs.putUChar("bright", cfg.brightness);
    prefs.putUShort("sleep", cfg.screenTimeoutSec);
    prefs.putBool("slpOn", cfg.screenSleepEnabled);
    prefs.putBool("autoupd", cfg.autoUpdateCheck);
    prefs.putString("tz", cfg.timezone);
    prefs.putBool("dimOn", cfg.dimSchedEnabled);
    prefs.putUShort("dimStart", cfg.dimStartMin);
    prefs.putUShort("dimEnd", cfg.dimEndMin);
    prefs.putUChar("dimBri", cfg.dimBrightness);

    if (cfg.printerCount > ORB_MAX_PRINTERS) cfg.printerCount = ORB_MAX_PRINTERS;
    if (cfg.printerCount && cfg.activePrinter >= cfg.printerCount) cfg.activePrinter = 0;
    prefs.putUChar("pcnt", cfg.printerCount);
    prefs.putUChar("pact", cfg.activePrinter);
    for (uint8_t i = 0; i < cfg.printerCount; i++) savePrinter(i, cfg.printers[i]);
    // Blank out any slot the user removed so a later re-add starts clean.
    for (uint8_t i = cfg.printerCount; i < ORB_MAX_PRINTERS; i++) savePrinter(i, PrinterCfg());
    prefs.end();
}

bool setActivePrinter(uint8_t idx) {
    if (idx >= cfg.printerCount || idx == cfg.activePrinter) return false;
    cfg.activePrinter = idx;
    prefs.begin(NS, false);
    prefs.putUChar("pact", idx);
    prefs.end();
    return true;
}

void reset() {
    prefs.begin(NS, false);
    prefs.clear();
    prefs.end();
}

}  // namespace Config
