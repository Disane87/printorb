#include "config.h"
#include <Preferences.h>

OrbConfig cfg;

static Preferences prefs;
static const char* NS = "printorb";

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
    cfg.printerType     = static_cast<PrinterType>(prefs.getUChar("ptype", 0));
    cfg.printerName     = prefs.getString("pname", "My Printer");
    cfg.printerIp       = prefs.getString("pip", "");
    cfg.moonrakerPort   = prefs.getUShort("mport", 7125);
    cfg.moonrakerApiKey = prefs.getString("mkey", "");
    cfg.bambuSerial     = prefs.getString("bserial", "");
    cfg.bambuAccessCode = prefs.getString("bcode", "");
    cfg.brightness      = prefs.getUChar("bright", 100);
    prefs.end();
}

void save() {
    prefs.begin(NS, false);  // read-write
    prefs.putString("wifiSsid", cfg.wifiSsid);
    prefs.putString("wifiPass", cfg.wifiPass);
    prefs.putString("host", cfg.hostname);
    prefs.putUChar("ptype", static_cast<uint8_t>(cfg.printerType));
    prefs.putString("pname", cfg.printerName);
    prefs.putString("pip", cfg.printerIp);
    prefs.putUShort("mport", cfg.moonrakerPort);
    prefs.putString("mkey", cfg.moonrakerApiKey);
    prefs.putString("bserial", cfg.bambuSerial);
    prefs.putString("bcode", cfg.bambuAccessCode);
    prefs.putUChar("bright", cfg.brightness);
    prefs.end();
}

void reset() {
    prefs.begin(NS, false);
    prefs.clear();
    prefs.end();
}

}  // namespace Config
