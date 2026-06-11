/**
 * @file version.h
 * Compile-time firmware version. STRING comes from -DPRINTORB_VERSION
 * (scripts/version.py); falls back to "0.0.0-dev" for ad-hoc builds.
 */
#pragma once

namespace Version {
    extern const char* STRING;      // e.g. "1.4.2"
    extern const char* BUILD_DATE;  // __DATE__ " " __TIME__
}
