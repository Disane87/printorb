#include "bambu_client.h"
#include "logbuf.h"
#include <WiFi.h>
#include <ArduinoJson.h>

// PubSubClient needs a generous buffer: the initial "pushall" report from a
// Bambu printer (especially with AMS) is large — ~32 KB observed on an X1/AMS
// setup. PubSubClient silently DROPS any packet larger than this buffer (no
// callback fires), so it must be big enough or the full report never arrives.
static const uint16_t MQTT_BUFFER = 49152;

// Single active instance pointer so the C-style MQTT callback can reach us.
static BambuClient* g_bambu = nullptr;

static void bambu_mqtt_cb(char* topic, uint8_t* payload, unsigned int len) {
    if (g_bambu) g_bambu->onMessage(topic, payload, len);
}

void BambuClient::begin() {
    g_bambu = this;
    _status.state = PrintState::OFFLINE;

    _net.setInsecure();                 // Bambu uses a self-signed cert on LAN
    _mqtt.setServer(_host.c_str(), 8883);
    bool bufOk = _mqtt.setBufferSize(MQTT_BUFFER);
    _mqtt.setKeepAlive(30);
    _mqtt.setSocketTimeout(5);
    _mqtt.setCallback(bambu_mqtt_cb);
    Log::printf("[Bambu] begin host=%s serial=%s codeLen=%d buf(%u)=%d\n",
                  _host.c_str(), _serial.c_str(), (int)_code.length(),
                  MQTT_BUFFER, bufOk);
}

void BambuClient::loop() {
    if (WiFi.status() != WL_CONNECTED) {
        _status.state = PrintState::OFFLINE;
        return;
    }

    if (!_mqtt.connected()) {
        uint32_t now = millis();
        if (now - _lastReconnectMs >= RECONNECT_INTERVAL_MS) {
            _lastReconnectMs = now;
            reconnect();
        }
        return;
    }

    _mqtt.loop();

    // Periodically ask for a full snapshot so we recover any missed fields.
    uint32_t now = millis();
    if (now - _lastPushAllMs >= PUSHALL_INTERVAL_MS) {
        _lastPushAllMs = now;
        requestPushAll();
    }
}

bool BambuClient::reconnect() {
    String clientId = "printorb-" + _serial.substring(0, 8);
    Log::printf("[Bambu] connecting %s:8883 ...\n", _host.c_str());
    // Bambu LAN MQTT: username "bblp", password = access code.
    if (!_mqtt.connect(clientId.c_str(), "bblp", _code.c_str())) {
        // PubSubClient state(): -4 timeout, -2 TLS/TCP connect failed,
        // 4 bad user/pass, 5 not authorized (wrong access code).
        Log::printf("[Bambu] connect FAILED, mqtt state=%d\n", _mqtt.state());
        _status.state = PrintState::OFFLINE;
        return false;
    }
    Log::printf("[Bambu] MQTT connected\n");

    String reportTopic = "device/" + _serial + "/report";
    bool sub = _mqtt.subscribe(reportTopic.c_str());
    Log::printf("[Bambu] subscribe %s -> %d\n", reportTopic.c_str(), sub);
    requestPushAll();
    _lastPushAllMs = millis();
    return true;
}

void BambuClient::requestPushAll() {
    if (!_mqtt.connected()) return;
    String reqTopic = "device/" + _serial + "/request";
    const char* msg = "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\"}}";
    _mqtt.publish(reqTopic.c_str(), msg);
}

void BambuClient::sendCommand(const char* cmd) {
    if (!_mqtt.connected()) { Log::printf("[Bambu] cmd %s ignored (offline)\n", cmd); return; }
    String topic = "device/" + _serial + "/request";
    String msg = String("{\"print\":{\"command\":\"") + cmd +
                 "\",\"param\":\"\",\"sequence_id\":\"1\"}}";
    bool ok = _mqtt.publish(topic.c_str(), msg.c_str());
    Log::printf("[Bambu] cmd %s -> %d\n", cmd, ok);
}

void BambuClient::pause()  { sendCommand("pause"); }
void BambuClient::resume() { sendCommand("resume"); }
void BambuClient::stop()   { sendCommand("stop"); }

// AMS HT drying. `ams_filament_drying` is a print-class command; mode 1 starts,
// mode 0 stops. temp/cooling_temp must be >= 45 or firmware ignores it.
bool BambuClient::startDrying(int amsRawId, int tempC, int durationH) {
    if (!_mqtt.connected()) { Log::printf("[Bambu] dry-start ignored (offline)\n"); return false; }
    if (tempC < 45) tempC = 45;
    String topic = "device/" + _serial + "/request";
    String msg = String("{\"print\":{\"command\":\"ams_filament_drying\",\"ams_id\":") + amsRawId +
                 ",\"mode\":1,\"temp\":" + tempC + ",\"cooling_temp\":" + tempC +
                 ",\"duration\":" + durationH + ",\"humidity\":0,\"rotate_tray\":false,\"sequence_id\":\"1\"}}";
    bool ok = _mqtt.publish(topic.c_str(), msg.c_str());
    Log::printf("[Bambu] dry-start ams=%d %dC %dh -> %d\n", amsRawId, tempC, durationH, ok);
    return ok;
}

bool BambuClient::stopDrying(int amsRawId) {
    if (!_mqtt.connected()) { Log::printf("[Bambu] dry-stop ignored (offline)\n"); return false; }
    String topic = "device/" + _serial + "/request";
    String msg = String("{\"print\":{\"command\":\"ams_filament_drying\",\"ams_id\":") + amsRawId +
                 ",\"mode\":0,\"temp\":0,\"cooling_temp\":40,\"duration\":0,\"humidity\":0,"
                 "\"rotate_tray\":false,\"sequence_id\":\"1\"}}";
    bool ok = _mqtt.publish(topic.c_str(), msg.c_str());
    Log::printf("[Bambu] dry-stop ams=%d -> %d\n", amsRawId, ok);
    return ok;
}

void BambuClient::onMessage(char* /*topic*/, uint8_t* payload, unsigned int len) {
    // We only care about messages containing a "print" object.
    JsonDocument filter;
    JsonObject p = filter["print"].to<JsonObject>();
    p["gcode_state"]          = true;
    p["mc_percent"]           = true;
    p["mc_remaining_time"]    = true;
    p["nozzle_temper"]        = true;
    p["nozzle_target_temper"] = true;
    p["bed_temper"]           = true;
    p["bed_target_temper"]    = true;
    p["layer_num"]            = true;
    p["total_layer_num"]      = true;
    p["subtask_name"]         = true;
    p["gcode_file"]           = true;
    p["print_error"]          = true;
    p["ams"]                  = true;   // whole AMS subtree (units + trays)
    p["device"]["extruder"]   = true;   // per-nozzle loaded slot (snow) — H2 series

    JsonDocument doc;
    DeserializationError err =
        deserializeJson(doc, payload, len, DeserializationOption::Filter(filter));
    if (err) { Log::printf("[Bambu] json err: %s\n", err.c_str()); return; }

    JsonObject pr = doc["print"];
    if (pr.isNull()) return;  // status message of a type we don't track

    // --- Temperatures (only update if present in this incremental report) ---
    if (pr["nozzle_temper"].is<float>())        _status.nozzleTemp   = pr["nozzle_temper"];
    if (pr["nozzle_target_temper"].is<float>()) _status.nozzleTarget = pr["nozzle_target_temper"];
    if (pr["bed_temper"].is<float>())           _status.bedTemp      = pr["bed_temper"];
    if (pr["bed_target_temper"].is<float>())    _status.bedTarget    = pr["bed_target_temper"];

    // --- Progress ---
    if (pr["mc_percent"].is<int>())
        _status.progress = constrain((float)(int)pr["mc_percent"], 0.0f, 100.0f);

    // --- Remaining time (Bambu reports minutes) ---
    if (pr["mc_remaining_time"].is<int>()) {
        int mins = pr["mc_remaining_time"];
        _status.remainingSec = mins >= 0 ? mins * 60 : -1;
    }

    // --- Layers ---
    if (pr["layer_num"].is<int>())       _status.currentLayer = pr["layer_num"];
    if (pr["total_layer_num"].is<int>()) _status.totalLayer   = pr["total_layer_num"];

    // --- Filename ---
    if (pr["subtask_name"].is<const char*>()) {
        _status.filename = String((const char*)pr["subtask_name"]);
    } else if (pr["gcode_file"].is<const char*>()) {
        _status.filename = String((const char*)pr["gcode_file"]);
    }

    // --- State ---
    if (pr["gcode_state"].is<const char*>()) {
        const char* gs = pr["gcode_state"];
        if      (!strcmp(gs, "RUNNING"))  _status.state = PrintState::PRINTING;
        else if (!strcmp(gs, "PAUSE"))    _status.state = PrintState::PAUSED;
        else if (!strcmp(gs, "FINISH"))   _status.state = PrintState::COMPLETE;
        else if (!strcmp(gs, "FAILED"))   _status.state = PrintState::ERROR;
        else if (!strcmp(gs, "PREPARE"))  _status.state = PrintState::PRINTING;
        else if (!strcmp(gs, "SLICING"))  _status.state = PrintState::PRINTING;
        else                              _status.state = PrintState::IDLE;
    }

    int perr = pr["print_error"] | 0;
    if (perr != 0 && _status.state != PrintState::ERROR) {
        // Non-fatal hms/print_error code present — surface it but don't override
        // an explicit RUNNING/PAUSE state.
        _status.errorMsg = "err " + String(perr);
    }

    // --- AMS (filament system), all units ---
    JsonObject ams = pr["ams"];
    if (!ams.isNull()) {
        JsonArray units = ams["ams"];
        if (!units.isNull()) {
            uint8_t un = 0;
            for (JsonObject u : units) {
                if (un >= 4) break;
                AmsUnit& U = _status.ams.unit[un];
                // The print payload carries no model name; AMS HT reports its id
                // in the 128..135 range, which is how we tell it apart from the
                // AMS / AMS 2 Pro / AMS Lite units (ids 0..3).
                int id   = atoi((const char*)(u["id"] | "-1"));
                U.rawId  = (int16_t)id;
                U.isHT   = (id >= 128);
                U.present  = true;
                U.humidity    = atoi((const char*)(u["humidity"]     | "-1"));
                U.humidityPct = atoi((const char*)(u["humidity_raw"] | "-1"));
                U.tempC       = atof((const char*)(u["temp"]         | "-100"));
                // Drying state: dry_time counts down (minutes) while a cycle runs;
                // dry_setting.dry_temperature holds the configured target. Both read
                // -1 / 0 when idle.
                int dryTime    = u["dry_time"] | 0;
                int drySetTemp = u["dry_setting"]["dry_temperature"] | -1;
                U.drying       = dryTime > 0;
                U.dryRemainMin = dryTime > 0 ? dryTime : -1;
                U.dryTargetC   = drySetTemp > 0 ? drySetTemp : -1;
                uint8_t maxSlots = U.isHT ? 1 : 4;   // HT is physically single-slot
                uint8_t n = 0;
                for (JsonObject t : u["tray"].as<JsonArray>()) {
                    if (n >= maxSlots) break;
                    AmsSlot& sl = U.slot[n];
                    sl.type    = String((const char*)(t["tray_type"] | ""));
                    sl.present = sl.type.length() > 0;
                    const char* col = t["tray_color"] | "";
                    if (strlen(col) >= 6)
                        sl.color = (uint32_t)strtoul(String(col).substring(0, 6).c_str(), nullptr, 16);
                    sl.remain = (int8_t)(t["remain"] | -1);
                    n++;
                }
                U.count = n;
                un++;
            }
            _status.ams.units   = un;
            _status.ams.present = un > 0;
        }
        // Which tray is actually loaded in the nozzle. H2-series firmware reports
        // it per-nozzle in device.extruder.info[].snow as a packed global address
        // (high byte = AMS id, low byte = tray index); its ams.tray_now is stale.
        // Older printers (X1/P1/A1) only send ams.tray_now = (id<<2)|slot, where
        // >=128 is the active AMS HT. Prefer snow, fall back to tray_now.
        _status.ams.activeUnit = -1;
        _status.ams.activeSlot = -1;
        int wantId = -1, wantSlot = -1;

        JsonArray ext = pr["device"]["extruder"]["info"].as<JsonArray>();
        if (!ext.isNull()) {
            for (JsonObject e : ext) {
                int snow = e["snow"] | -1;
                if (snow < 0 || snow == 0xFFFF) continue;   // no/unknown filament
                int id = snow >> 8, slot = snow & 0xFF;
                if (slot < 4) { wantId = id; wantSlot = slot; break; }
            }
        }
        if (wantId < 0) {   // legacy fallback (no per-nozzle snow)
            int idx = atoi((const char*)(ams["tray_now"] | "255"));
            if (idx >= 0 && idx != 254 && idx != 255) {
                wantId   = (idx >= 128) ? idx : (idx >> 2);
                wantSlot = (idx >= 128) ? 0   : (idx & 3);
            }
        }
        if (wantId >= 0 && wantSlot >= 0 && wantSlot < 4) {
            for (uint8_t i = 0; i < _status.ams.units; i++) {
                if (_status.ams.unit[i].rawId == wantId) {
                    _status.ams.activeUnit = (int8_t)i;
                    _status.ams.activeSlot = (int8_t)wantSlot;
                    break;
                }
            }
        }
    }

    _status.lastUpdateMs = millis();

    // Log only on state transitions to keep the serial output quiet.
    static PrintState lastLogged = (PrintState)0xFF;
    if (_status.state != lastLogged) {
        lastLogged = _status.state;
        Log::printf("[Bambu] status: %s %d%%\n",
                      PrinterStatus::stateLabel(_status.state),
                      (int)(_status.progress + 0.5f));
    }
}
