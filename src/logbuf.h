/**
 * @file logbuf.h
 * Tiny in-memory log ring buffer. Log::printf writes to the serial port AND to
 * a fixed circular buffer that the web portal can serve at /api/log, so the
 * device can be debugged from a browser without a serial cable.
 */
#pragma once

#include <Arduino.h>

namespace Log {
    /** printf-style: echoes to Serial and appends to the ring buffer. */
    void printf(const char* fmt, ...);
    /** Snapshot of the buffered log text (oldest first). */
    String dump();
}
