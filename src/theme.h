/**
 * @file theme.h
 * Central design tokens for the on-device UI.
 *
 * Every colour, font and metric the LVGL screens use lives here, so the look of
 * the whole device can be adjusted in one place instead of hunting for hex
 * literals across ui.cpp. Header-only: the helpers are trivial and inlining them
 * keeps them free.
 *
 * Layout note: the panel is a 240x240 *circle*. Anything further than ~118 px
 * from the centre is clipped by the bezel, so screen content is laid out inside
 * the metrics below rather than against the full square.
 */
#pragma once

#include <lvgl.h>
#include "printer.h"

namespace Theme {

// ---------------------------------------------------------------- palette ---
// Dark, low-glare palette: near-black background so the round bezel disappears,
// one cyan accent, and semantic colours reused for every state indication.
constexpr uint32_t BG         = 0x000000;  // screen background
constexpr uint32_t SURFACE    = 0x161d26;  // cards / tiles
constexpr uint32_t SURFACE_HI = 0x212b36;  // raised / pressed surface
constexpr uint32_t TRACK      = 0x1b2430;  // progress & arc tracks
constexpr uint32_t LINE       = 0x2b3644;  // hairlines, borders, inactive dots

constexpr uint32_t TEXT       = 0xf2f6fa;  // primary text
constexpr uint32_t TEXT_DIM   = 0x9aa7b4;  // secondary text
constexpr uint32_t TEXT_FAINT = 0x5d6875;  // tertiary / hints

constexpr uint32_t ACCENT     = 0x22d3ee;  // brand cyan
constexpr uint32_t OK         = 0x3ddc84;
constexpr uint32_t WARN       = 0xffb020;
constexpr uint32_t DANGER     = 0xff5a5a;
constexpr uint32_t NOZZLE     = 0xff8c42;
constexpr uint32_t BED        = 0xff6b6b;
constexpr uint32_t IDLE       = 0x78909c;

// ---------------------------------------------------------------- metrics ---
constexpr lv_coord_t CONTENT_W  = 176;  // widest content that clears the bezel
constexpr lv_coord_t ROW_W      = 168;  // list rows / buttons
constexpr lv_coord_t RADIUS     = 12;   // default corner radius

// Hold-to-confirm duration for destructive actions. LVGL 8 hardcodes its own
// 400 ms default in lv_hal_indev.h (it is not an lv_conf.h option), so UI::begin
// pushes this value onto the input driver; the on-button fill animation is timed
// against the same constant and completes exactly when the action fires.
constexpr uint32_t HOLD_MS = 700;

// ------------------------------------------------------------------ colour ---
inline lv_color_t c(uint32_t hex) { return lv_color_hex(hex); }

/** Semantic colour for a printer state — the single source of state colouring. */
inline lv_color_t state(PrintState s) {
    switch (s) {
        case PrintState::PRINTING: return c(ACCENT);
        case PrintState::PAUSED:   return c(WARN);
        case PrintState::COMPLETE: return c(OK);
        case PrintState::ERROR:    return c(DANGER);
        case PrintState::IDLE:     return c(IDLE);
        default:                   return c(TEXT_FAINT);
    }
}

/** Perceived brightness of a 0xRRGGBB colour (0..255), for contrast decisions. */
inline int luminance(uint32_t rgb) {
    return ((int)((rgb >> 16) & 0xff) * 299 +
            (int)((rgb >> 8)  & 0xff) * 587 +
            (int)( rgb        & 0xff) * 114) / 1000;
}

/** Black or white — whichever stays readable on top of `rgb`. */
inline lv_color_t textOn(uint32_t rgb) {
    return luminance(rgb) > 140 ? lv_color_black() : c(TEXT);
}

}  // namespace Theme
