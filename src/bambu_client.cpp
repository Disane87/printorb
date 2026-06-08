#include "bambu_client.h"
#include <WiFi.h>
#include <ArduinoJson.h>

// PubSubClient needs a generous buffer: the initial "pushall" report from a
// Bambu printer can be several KB. Incremental reports afterwards are small.
static const uint16_t MQTT_BUFFER = 16384;

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
    _mqtt.setBufferSize(MQTT_BUFFER);
    _mqtt.setKeepAlive(30);
    _mqtt.setSocketTimeout(5);
    _mqtt.setCallback(bambu_mqtt_cb);
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
    // Bambu LAN MQTT: username "bblp", password = access code.
    if (!_mqtt.connect(clientId.c_str(), "bblp", _code.c_str())) {
        _status.state = PrintState::OFFLINE;
        return false;
    }

    String reportTopic = "device/" + _serial + "/report";
    _mqtt.subscribe(reportTopic.c_str());
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

    JsonDocument doc;
    DeserializationError err =
        deserializeJson(doc, payload, len, DeserializationOption::Filter(filter));
    if (err) return;

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

    _status.lastUpdateMs = millis();
}
