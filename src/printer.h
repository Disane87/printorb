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

// One AMS filament slot.
struct AmsSlot {
    bool     present = false;
    String   type;                 // "PLA", "PETG", ... ("" = empty slot)
    uint32_t color   = 0x808080;   // 0xRRGGBB
    int8_t   remain  = -1;         // remaining %, -1 = unknown
};

// One AMS unit (up to 4 trays).
struct AmsUnit {
    bool    present  = false;
    uint8_t count    = 0;          // populated trays (usually 4)
    int8_t  humidity = -1;         // dryness level 1..5, -1 = unknown
    AmsSlot slot[4];
};

// AMS snapshot across all units. present=false for Klipper / no AMS.
struct AmsInfo {
    bool    present    = false;
    uint8_t units      = 0;        // number of AMS units detected (1..4)
    int8_t  activeUnit = -1;       // unit index of the active tray, -1 = none
    int8_t  activeSlot = -1;       // slot within the active unit, -1 = none
    AmsUnit unit[4];
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

    AmsInfo ams;                 // Bambu AMS state (present=false if none)

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

    /** Print job control (no-op by default; overridden per backend). */
    virtual void pause()  {}
    virtual void resume() {}
    virtual void stop()   {}

    /** Latest known status. */
    const PrinterStatus& status() const { return _status; }

protected:
    PrinterStatus _status;
};
