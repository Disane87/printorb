/**
 * @file display.h
 * Display + LVGL bring-up (LovyanGFX flush + CST816S touch input).
 */
#pragma once

#include <Arduino.h>

namespace Display {
    /** Initialise the panel, backlight, LVGL and the input device. */
    void begin();
    /** Set backlight brightness, 0..100 (%). */
    void setBrightness(uint8_t pct);
}
