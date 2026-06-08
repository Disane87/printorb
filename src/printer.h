/**
 * @file printer.h
 * Common printer status model + abstract client interface.
 * Klipper (Moonraker) and Bambu Lab (MQTT) both produce a PrinterStatus.
 */
#pragma once

#include <Arduino.h>

enum class PrintState : uint8_t {
    OFFLINE,    // no connection to printer
    IDLE,       // connected, not printing
    PRINTING,
    PAUSED,
    COMPLETE,
    ERROR,
};

struct PrinterStatus {
    PrintState state = PrintState::OFFLINE;

    float progress     = 0.0f;   // 0..100 %
    float nozzleTemp   = 0.0f;
    float nozzleTarget = 0.0f;
    float bedTemp      = 0.0f;
    float bedTarget    = 0.0f;

    int32_t remainingSec = -1;   // -1 = unknown
    int32_t currentLayer = -1;
    int32_t totalLayer   = -1;

    String  filename;            // current job file
    String  errorMsg;            // optional human-readable error

    uint32_t lastUpdateMs = 0;   // millis() of last successful update

    static const char* stateLabel(PrintState s) {
        switch (s) {
            case PrintState::IDLE:     return "Idle";
            case PrintState::PRINTING: return "Printing";
            case PrintState::PAUSED:   return "Paused";
            case PrintState::COMPLETE: return "Complete";
            case PrintState::ERROR:    return "Error";
            default:                   return "Offline";
        }
    }
};

/** Abstract printer client. Implementations poll/subscribe and fill `status`. */
class PrinterClient {
public:
    virtual ~PrinterClient() {}

    /** Called once after construction. */
    virtual void begin() = 0;

    /** Service the connection. Call frequently from loop(). */
    virtual void loop() = 0;

    /** Latest known status. */
    const PrinterStatus& status() const { return _status; }

protected:
    PrinterStatus _status;
};
