#include "ui.h"
#include "orb_icons.h"
#include "logbuf.h"
#include "display.h"
#include "wifi_manager.h"
#include <lvgl.h>
#include <WiFi.h>

namespace {

// --- Screens ---
lv_obj_t* scr_status   = nullptr;
lv_obj_t* scr_details  = nullptr;
lv_obj_t* scr_system   = nullptr;
lv_obj_t* scr_ams      = nullptr;
lv_obj_t* scr_controls = nullptr;
lv_obj_t* scr_idle     = nullptr;
lv_obj_t* scr_setup    = nullptr;
lv_obj_t* scr_boot     = nullptr;
lv_obj_t* scr_update   = nullptr;

lv_obj_t* carousel[5] = {};   // status, details, system, [ams], controls
int  carCount  = 4;
int  carIdx    = 0;
bool carActive = false;

UI::ControlCb g_ctrl = nullptr;

// --- Status widgets ---
lv_obj_t* arc_progress, *lbl_percent, *lbl_state, *lbl_printer, *lbl_file;
lv_obj_t* ic_nozzle, *lbl_nozzle, *ic_bed, *lbl_bed, *ic_eta, *lbl_eta, *ic_layers, *lbl_layers;

// --- Details widgets (minimal & airy) ---
lv_obj_t* dt_state, *dt_file, *dt_swatch, *dt_type, *dt_slot;

// --- System widgets (minimal & airy) ---
lv_obj_t* sy_wifi, *sy_ip, *sy_bright;

// --- AMS widgets ---
lv_obj_t* ams_tile[4], *ams_type[4], *ams_remain[4];
lv_obj_t* ams_title, *ams_humid, *ams_none, *ams_updown;
lv_obj_t* amsDots = nullptr;   // vertical unit indicator (right edge)
lv_obj_t* amsDot[4] = {};
AmsInfo   g_ams;          // last AMS snapshot (for re-render on vertical swipe)
int       amsUnitIdx = 0; // currently shown AMS unit

// --- Controls widgets ---
lv_obj_t* btn_pause, *btn_resume, *btn_stop;

// --- Idle screen widgets ---
lv_obj_t* id_printer, *id_state, *id_temps;

// --- Boot / setup widgets ---
lv_obj_t* boot_bar, *boot_step, *boot_detail;
lv_obj_t* setup_title, *setup_body;

// --- Update (OTA) widgets ---
lv_obj_t* upd_bar, *upd_pct;

// --- Page indicator dots (on the top layer, above all screens) ---
lv_obj_t* dots = nullptr;
lv_obj_t* dot[5] = {};

lv_color_t stateColor(PrintState s) {
    switch (s) {
        case PrintState::PRINTING: return lv_palette_main(LV_PALETTE_CYAN);
        case PrintState::PAUSED:   return lv_palette_main(LV_PALETTE_AMBER);
        case PrintState::COMPLETE: return lv_palette_main(LV_PALETTE_GREEN);
        case PrintState::ERROR:    return lv_palette_main(LV_PALETTE_RED);
        case PrintState::IDLE:     return lv_palette_main(LV_PALETTE_BLUE_GREY);
        default:                   return lv_palette_main(LV_PALETTE_GREY);
    }
}

String fmtRemaining(int32_t sec) {
    if (sec < 0) return String("--:--");
    int h = sec / 3600;
    int m = (sec % 3600) / 60;
    char buf[16];
    if (h > 0) snprintf(buf, sizeof(buf), "%dh%02dm", h, m);
    else       snprintf(buf, sizeof(buf), "%dm", m);
    return String(buf);
}

// ---- Carousel navigation ----
void updateDots() {
    for (int i = 0; i < carCount; i++)
        lv_obj_set_style_bg_color(
            dot[i], i == carIdx ? lv_palette_main(LV_PALETTE_CYAN) : lv_color_hex(0x3a424c), 0);
}

void gotoScreen(int idx, lv_scr_load_anim_t anim) {
    if (idx < 0 || idx >= carCount || idx == carIdx) return;
    carIdx = idx;
    lv_scr_load_anim(carousel[idx], anim, 220, 0, false);
    updateDots();
}

void renderAms();      // forward decl (vertical swipe re-renders the AMS unit)
void enterCarousel();  // forward decl (idle screen swipes into the carousel)

void gesture_cb(lv_event_t* /*e*/) {
    lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());

    // From the idle screen, any horizontal swipe enters the status carousel.
    if (scr_idle && lv_scr_act() == scr_idle) {
        if (d == LV_DIR_LEFT || d == LV_DIR_RIGHT) enterCarousel();
        return;
    }

    // On the AMS screen, vertical swipes cycle through AMS units.
    if (scr_ams && lv_scr_act() == scr_ams && (d == LV_DIR_TOP || d == LV_DIR_BOTTOM)) {
        if (g_ams.units > 1) {
            if (d == LV_DIR_TOP) amsUnitIdx = (amsUnitIdx + 1) % g_ams.units;
            else                 amsUnitIdx = (amsUnitIdx - 1 + g_ams.units) % g_ams.units;
            renderAms();
        }
        return;
    }

    if      (d == LV_DIR_LEFT)  gotoScreen(carIdx + 1, LV_SCR_LOAD_ANIM_MOVE_LEFT);
    else if (d == LV_DIR_RIGHT) gotoScreen(carIdx - 1, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
}

void ctrl_cb(lv_event_t* e) {
    UI::Ctrl c = (UI::Ctrl)(intptr_t)lv_event_get_user_data(e);
    Log::printf("[UI] control %d\n", (int)c);
    if (g_ctrl) g_ctrl(c);
}

void reboot_cb(lv_event_t* /*e*/) {
    Log::printf("[UI] reboot requested\n");
    ESP.restart();
}

void bright_cb(lv_event_t* e) {
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    int v = (int)cfg.brightness + delta;
    if (v < 10) v = 10;
    if (v > 100) v = 100;
    cfg.brightness = (uint8_t)v;
    Display::setBrightness(cfg.brightness);
    Config::save();
    lv_label_set_text_fmt(sy_bright, "%d%%", v);
}

// Activate the swipeable carousel at the status screen (from boot / idle).
void enterCarousel() {
    carActive = true;
    carIdx = 0;
    if (lv_scr_act() != scr_status) lv_scr_load(scr_status);
    lv_obj_clear_flag(dots, LV_OBJ_FLAG_HIDDEN);
    updateDots();
}

// Show the standalone idle screen (managed like boot/setup, outside the carousel).
void showIdle() {
    carActive = false;
    if (dots) lv_obj_add_flag(dots, LV_OBJ_FLAG_HIDDEN);
    if (scr_idle && lv_scr_act() != scr_idle) lv_scr_load(scr_idle);
}

// ---- Builders ----
lv_obj_t* makeScreen(lv_color_t bg) {
    lv_obj_t* s = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s, bg, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s, gesture_cb, LV_EVENT_GESTURE, NULL);
    return s;
}

// A thin decorative circle near the bezel — makes each screen feel "round".
void addRingFrame(lv_obj_t* scr) {
    lv_obj_t* ring = lv_obj_create(scr);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 236, 236);
    lv_obj_center(ring);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(0x1c2630), 0);
}

// A centered, transparent, airy flex column. No card (round display friendly).
// Not clickable/scrollable so swipe gestures reach the screen.
lv_obj_t* makeColumn(lv_obj_t* scr, const char* title) {
    lv_obj_t* col = lv_obj_create(scr);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, 200, 220);
    lv_obj_center(col);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 16, 0);

    if (title && title[0]) {
        lv_obj_t* t = lv_label_create(col);
        lv_obj_set_style_text_color(t, lv_palette_main(LV_PALETTE_CYAN), 0);
        lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
        lv_label_set_text(t, title);
    }
    return col;
}

// A centered "icon + value" row used on the airy detail screens.
lv_obj_t* addIconRow(lv_obj_t* col, const char* icon, lv_color_t iconColor, const lv_font_t* iconFont) {
    lv_obj_t* row = lv_obj_create(col);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 180, 30);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);

    lv_obj_t* ic = lv_label_create(row);
    lv_obj_set_style_text_font(ic, iconFont, 0);
    lv_obj_set_style_text_color(ic, iconColor, 0);
    lv_label_set_text(ic, icon);

    lv_obj_t* val = lv_label_create(row);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(val, lv_color_white(), 0);
    lv_label_set_text(val, "--");
    return val;
}

void buildStatusScreen() {
    scr_status = makeScreen(lv_color_black());

    arc_progress = lv_arc_create(scr_status);
    lv_obj_set_size(arc_progress, 226, 226);
    lv_obj_center(arc_progress);
    lv_arc_set_rotation(arc_progress, 135);
    lv_arc_set_bg_angles(arc_progress, 0, 270);
    lv_arc_set_range(arc_progress, 0, 100);
    lv_arc_set_value(arc_progress, 0);
    lv_obj_remove_style(arc_progress, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc_progress, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc_progress, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_progress, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_progress, lv_color_hex(0x202830), LV_PART_MAIN);

    lbl_printer = lv_label_create(scr_status);
    lv_obj_set_style_text_color(lbl_printer, lv_color_hex(0x9aa4ad), 0);
    lv_obj_set_style_text_font(lbl_printer, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl_printer, "Printer");
    lv_obj_align(lbl_printer, LV_ALIGN_TOP_MID, 0, 36);

    lbl_file = lv_label_create(scr_status);
    lv_obj_set_style_text_color(lbl_file, lv_color_hex(0x6b7480), 0);
    lv_obj_set_style_text_font(lbl_file, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(lbl_file, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_file, 150);
    lv_obj_set_style_text_align(lbl_file, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(lbl_file, "");
    lv_obj_align(lbl_file, LV_ALIGN_TOP_MID, 0, 54);

    lbl_percent = lv_label_create(scr_status);
    lv_obj_set_style_text_color(lbl_percent, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_percent, &lv_font_montserrat_40, 0);
    lv_label_set_text(lbl_percent, "--");
    lv_obj_align(lbl_percent, LV_ALIGN_CENTER, 0, -6);

    lbl_state = lv_label_create(scr_status);
    lv_obj_set_style_text_font(lbl_state, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_state, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_label_set_text(lbl_state, "Offline");
    lv_obj_align(lbl_state, LV_ALIGN_CENTER, 0, 28);

    ic_nozzle = lv_label_create(scr_status);
    lv_obj_set_style_text_font(ic_nozzle, &orb_icons, 0);
    lv_obj_set_style_text_color(ic_nozzle, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_label_set_text(ic_nozzle, ORB_ICON_NOZZLE);
    lv_obj_align(ic_nozzle, LV_ALIGN_CENTER, -58, 56);

    lbl_nozzle = lv_label_create(scr_status);
    lv_obj_set_style_text_color(lbl_nozzle, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_obj_set_style_text_font(lbl_nozzle, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl_nozzle, "--");
    lv_obj_align(lbl_nozzle, LV_ALIGN_CENTER, -34, 58);

    ic_bed = lv_label_create(scr_status);
    lv_obj_set_style_text_font(ic_bed, &orb_icons, 0);
    lv_obj_set_style_text_color(ic_bed, lv_palette_main(LV_PALETTE_RED), 0);
    lv_label_set_text(ic_bed, ORB_ICON_BED);
    lv_obj_align(ic_bed, LV_ALIGN_CENTER, 22, 56);

    lbl_bed = lv_label_create(scr_status);
    lv_obj_set_style_text_color(lbl_bed, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_text_font(lbl_bed, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl_bed, "--");
    lv_obj_align(lbl_bed, LV_ALIGN_CENTER, 46, 58);

    ic_eta = lv_label_create(scr_status);
    lv_obj_set_style_text_font(ic_eta, &orb_icons, 0);
    lv_obj_set_style_text_color(ic_eta, lv_color_hex(0x9aa4ad), 0);
    lv_label_set_text(ic_eta, ORB_ICON_CLOCK);
    lv_obj_align(ic_eta, LV_ALIGN_BOTTOM_MID, -52, -34);

    lbl_eta = lv_label_create(scr_status);
    lv_obj_set_style_text_color(lbl_eta, lv_color_hex(0x9aa4ad), 0);
    lv_obj_set_style_text_font(lbl_eta, &lv_font_montserrat_12, 0);
    lv_label_set_text(lbl_eta, "--");
    lv_obj_align(lbl_eta, LV_ALIGN_BOTTOM_MID, -28, -33);

    ic_layers = lv_label_create(scr_status);
    lv_obj_set_style_text_font(ic_layers, &orb_icons, 0);
    lv_obj_set_style_text_color(ic_layers, lv_color_hex(0x9aa4ad), 0);
    lv_label_set_text(ic_layers, ORB_ICON_LAYERS);
    lv_obj_align(ic_layers, LV_ALIGN_BOTTOM_MID, 14, -34);

    lbl_layers = lv_label_create(scr_status);
    lv_obj_set_style_text_color(lbl_layers, lv_color_hex(0x9aa4ad), 0);
    lv_obj_set_style_text_font(lbl_layers, &lv_font_montserrat_12, 0);
    lv_label_set_text(lbl_layers, "--");
    lv_obj_align(lbl_layers, LV_ALIGN_BOTTOM_MID, 38, -33);
}

// "Now printing" screen: the active filament shown as a big colour swatch
// (Bambu AMS), with type / slot / remaining. Klipper has no colour data, so it
// falls back to a neutral swatch + hint.
void buildDetailsScreen() {
    scr_details = makeScreen(lv_color_black());
    addRingFrame(scr_details);
    lv_obj_t* col = makeColumn(scr_details, "");
    lv_obj_set_style_pad_row(col, 10, 0);

    dt_state = lv_label_create(col);
    lv_obj_set_style_text_font(dt_state, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(dt_state, lv_color_white(), 0);
    lv_label_set_text(dt_state, "Idle");

    // Large circular filament-colour swatch.
    dt_swatch = lv_obj_create(col);
    lv_obj_remove_style_all(dt_swatch);
    lv_obj_set_size(dt_swatch, 78, 78);
    lv_obj_clear_flag(dt_swatch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(dt_swatch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(dt_swatch, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dt_swatch, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dt_swatch, lv_color_hex(0x2b3340), 0);
    lv_obj_set_style_border_width(dt_swatch, 3, 0);
    lv_obj_set_style_border_color(dt_swatch, lv_color_hex(0x3a424c), 0);

    dt_type = lv_label_create(col);
    lv_obj_set_style_text_font(dt_type, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(dt_type, lv_color_white(), 0);
    lv_label_set_text(dt_type, "—");

    dt_slot = lv_label_create(col);
    lv_obj_set_style_text_font(dt_slot, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(dt_slot, lv_color_hex(0x9aa4ad), 0);
    lv_label_set_text(dt_slot, "");

    dt_file = lv_label_create(col);
    lv_obj_set_style_text_font(dt_file, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(dt_file, lv_color_hex(0x6b7480), 0);
    lv_label_set_long_mode(dt_file, LV_LABEL_LONG_DOT);
    lv_obj_set_width(dt_file, 168);
    lv_obj_set_style_text_align(dt_file, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(dt_file, "");
}

void buildSystemScreen() {
    scr_system = makeScreen(lv_color_black());
    addRingFrame(scr_system);
    lv_obj_t* col = makeColumn(scr_system, "System");

    sy_wifi = lv_label_create(col);
    lv_obj_set_style_text_font(sy_wifi, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sy_wifi, lv_color_white(), 0);
    lv_label_set_long_mode(sy_wifi, LV_LABEL_LONG_DOT);
    lv_obj_set_width(sy_wifi, 176);
    lv_obj_set_style_text_align(sy_wifi, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(sy_wifi, LV_SYMBOL_WIFI);

    sy_ip = lv_label_create(col);
    lv_obj_set_style_text_font(sy_ip, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sy_ip, lv_color_hex(0x9aa4ad), 0);
    lv_label_set_text(sy_ip, "0.0.0.0");

    // Brightness row: [-]  value  [+]
    lv_obj_t* brow = lv_obj_create(col);
    lv_obj_remove_style_all(brow);
    lv_obj_set_size(brow, 150, 36);
    lv_obj_clear_flag(brow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(brow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(brow, 12, 0);

    lv_obj_t* bm = lv_btn_create(brow);
    lv_obj_set_size(bm, 40, 32);
    lv_obj_add_flag(bm, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(bm, bright_cb, LV_EVENT_CLICKED, (void*)(intptr_t)-10);
    lv_obj_t* bml = lv_label_create(bm); lv_label_set_text(bml, LV_SYMBOL_MINUS); lv_obj_center(bml);

    sy_bright = lv_label_create(brow);
    lv_obj_set_style_text_color(sy_bright, lv_color_white(), 0);
    lv_obj_set_style_text_font(sy_bright, &lv_font_montserrat_14, 0);
    lv_label_set_text(sy_bright, "100%");

    lv_obj_t* bp = lv_btn_create(brow);
    lv_obj_set_size(bp, 40, 32);
    lv_obj_add_flag(bp, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(bp, bright_cb, LV_EVENT_CLICKED, (void*)(intptr_t)10);
    lv_obj_t* bpl = lv_label_create(bp); lv_label_set_text(bpl, LV_SYMBOL_PLUS); lv_obj_center(bpl);

    // Reboot: long-press to avoid accidental restarts (mirrors "Hold = Stop").
    lv_obj_t* rb = lv_btn_create(col);
    lv_obj_set_size(rb, 150, 36);
    lv_obj_add_flag(rb, LV_OBJ_FLAG_EVENT_BUBBLE);          // let swipes pass through
    lv_obj_set_style_bg_color(rb, lv_palette_darken(LV_PALETTE_RED, 2), 0);
    lv_obj_set_style_radius(rb, 10, 0);
    lv_obj_add_event_cb(rb, reboot_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_t* rbl = lv_label_create(rb);
    lv_label_set_text(rbl, LV_SYMBOL_POWER "  Hold = Reboot");
    lv_obj_center(rbl);
}

void buildAmsScreen() {
    scr_ams = makeScreen(lv_color_black());
    addRingFrame(scr_ams);
    lv_obj_t* col = makeColumn(scr_ams, "");

    ams_title = lv_label_create(col);
    lv_obj_set_style_text_color(ams_title, lv_palette_main(LV_PALETTE_CYAN), 0);
    lv_obj_set_style_text_font(ams_title, &lv_font_montserrat_16, 0);
    lv_label_set_text(ams_title, "Filament");

    lv_obj_t* grid = lv_obj_create(col);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, 176, 132);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(grid, 8, 0);
    lv_obj_set_style_pad_column(grid, 8, 0);

    for (int i = 0; i < 4; i++) {
        lv_obj_t* tile = lv_obj_create(grid);
        lv_obj_remove_style_all(tile);
        lv_obj_set_size(tile, 80, 58);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(tile, 10, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(tile, lv_color_hex(0x2b3340), 0);
        lv_obj_set_style_border_width(tile, 2, 0);
        lv_obj_set_style_border_color(tile, lv_color_hex(0x2b3340), 0);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(tile, 2, 0);

        ams_type[i] = lv_label_create(tile);
        lv_obj_set_style_text_font(ams_type[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ams_type[i], lv_color_white(), 0);
        lv_label_set_text(ams_type[i], "-");

        ams_remain[i] = lv_label_create(tile);
        lv_obj_set_style_text_font(ams_remain[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(ams_remain[i], lv_color_white(), 0);
        lv_label_set_text(ams_remain[i], "");

        ams_tile[i] = tile;
    }

    ams_humid = lv_label_create(col);
    lv_obj_set_style_text_color(ams_humid, lv_color_hex(0x9aa4ad), 0);
    lv_obj_set_style_text_font(ams_humid, &lv_font_montserrat_12, 0);
    lv_label_set_text(ams_humid, "");

    // Hint that vertical swipes switch AMS units (shown only with >1 unit).
    ams_updown = lv_label_create(col);
    lv_obj_set_style_text_color(ams_updown, lv_color_hex(0x4a5560), 0);
    lv_obj_set_style_text_font(ams_updown, &lv_font_montserrat_12, 0);
    lv_label_set_text(ams_updown, LV_SYMBOL_UP "  " LV_SYMBOL_DOWN);
    lv_obj_add_flag(ams_updown, LV_OBJ_FLAG_HIDDEN);

    ams_none = lv_label_create(col);
    lv_obj_set_style_text_color(ams_none, lv_color_hex(0x6b7480), 0);
    lv_obj_set_style_text_font(ams_none, &lv_font_montserrat_14, 0);
    lv_label_set_text(ams_none, "No AMS detected");
    lv_obj_add_flag(ams_none, LV_OBJ_FLAG_HIDDEN);

    // Vertical unit indicator on the right edge (one dot per AMS unit).
    amsDots = lv_obj_create(scr_ams);
    lv_obj_remove_style_all(amsDots);
    lv_obj_set_size(amsDots, 12, 4 * 13);
    lv_obj_align(amsDots, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_clear_flag(amsDots, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(amsDots, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(amsDots, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(amsDots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(amsDots, 6, 0);
    for (int i = 0; i < 4; i++) {
        amsDot[i] = lv_obj_create(amsDots);
        lv_obj_remove_style_all(amsDot[i]);
        lv_obj_set_size(amsDot[i], 7, 7);
        lv_obj_set_style_radius(amsDot[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(amsDot[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(amsDot[i], lv_color_hex(0x3a424c), 0);
    }
    lv_obj_add_flag(amsDots, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t* makeCtrlButton(lv_obj_t* col, const char* text, lv_color_t color,
                         lv_event_code_t trigger, UI::Ctrl action) {
    lv_obj_t* b = lv_btn_create(col);
    lv_obj_set_size(b, 150, 40);
    lv_obj_add_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);          // let swipes pass through
    lv_obj_set_style_bg_color(b, color, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_add_event_cb(b, ctrl_cb, trigger, (void*)(intptr_t)action);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

void buildControlsScreen() {
    scr_controls = makeScreen(lv_color_black());
    addRingFrame(scr_controls);
    lv_obj_t* col = makeColumn(scr_controls, "Control");
    lv_obj_set_style_pad_row(col, 14, 0);
    btn_pause  = makeCtrlButton(col, LV_SYMBOL_PAUSE " Pause",   lv_palette_darken(LV_PALETTE_BLUE_GREY, 1), LV_EVENT_CLICKED,      UI::CTRL_PAUSE);
    btn_resume = makeCtrlButton(col, LV_SYMBOL_PLAY  " Resume",  lv_palette_darken(LV_PALETTE_GREEN, 2),     LV_EVENT_CLICKED,      UI::CTRL_RESUME);
    btn_stop   = makeCtrlButton(col, LV_SYMBOL_STOP  " Hold = Stop", lv_palette_darken(LV_PALETTE_RED, 2),  LV_EVENT_LONG_PRESSED, UI::CTRL_STOP);
}

void buildIdleScreen() {
    scr_idle = makeScreen(lv_color_black());
    addRingFrame(scr_idle);
    lv_obj_t* col = makeColumn(scr_idle, "");
    lv_obj_set_style_pad_row(col, 14, 0);

    // Subtle orb (brand mark, same look as the boot screen).
    lv_obj_t* orb = lv_obj_create(col);
    lv_obj_set_size(orb, 44, 44);
    lv_obj_clear_flag(orb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(orb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(orb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(orb, 0, 0);
    lv_obj_set_style_bg_color(orb, lv_palette_main(LV_PALETTE_CYAN), 0);
    lv_obj_set_style_bg_grad_color(orb, lv_palette_darken(LV_PALETTE_BLUE, 3), 0);
    lv_obj_set_style_bg_grad_dir(orb, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_color(orb, lv_palette_main(LV_PALETTE_CYAN), 0);
    lv_obj_set_style_shadow_width(orb, 18, 0);
    lv_obj_set_style_shadow_spread(orb, 1, 0);

    id_printer = lv_label_create(col);
    lv_obj_set_style_text_font(id_printer, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(id_printer, lv_color_hex(0x9aa4ad), 0);
    lv_label_set_long_mode(id_printer, LV_LABEL_LONG_DOT);
    lv_obj_set_width(id_printer, 180);
    lv_obj_set_style_text_align(id_printer, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(id_printer, "Printer");

    id_state = lv_label_create(col);
    lv_obj_set_style_text_font(id_state, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(id_state, lv_palette_main(LV_PALETTE_BLUE_GREY), 0);
    lv_label_set_text(id_state, "Ready");

    id_temps = addIconRow(col, ORB_ICON_NOZZLE, lv_palette_main(LV_PALETTE_ORANGE), &orb_icons);
}

void buildSetupScreen() {
    scr_setup = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_setup, lv_color_hex(0x101418), 0);
    lv_obj_clear_flag(scr_setup, LV_OBJ_FLAG_SCROLLABLE);

    setup_title = lv_label_create(scr_setup);
    lv_obj_set_style_text_color(setup_title, lv_palette_main(LV_PALETTE_CYAN), 0);
    lv_obj_set_style_text_font(setup_title, &lv_font_montserrat_20, 0);
    lv_label_set_text(setup_title, "Setup");
    lv_obj_align(setup_title, LV_ALIGN_CENTER, 0, -50);

    setup_body = lv_label_create(scr_setup);
    lv_obj_set_style_text_color(setup_body, lv_color_white(), 0);
    lv_obj_set_style_text_font(setup_body, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(setup_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(setup_body, 200);
    lv_label_set_text(setup_body, "");
    lv_obj_align(setup_body, LV_ALIGN_CENTER, 0, 10);
}

void buildBootScreen() {
    scr_boot = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_boot, lv_color_black(), 0);
    lv_obj_clear_flag(scr_boot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* orb = lv_obj_create(scr_boot);
    lv_obj_set_size(orb, 56, 56);
    lv_obj_clear_flag(orb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(orb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(orb, 0, 0);
    lv_obj_set_style_bg_color(orb, lv_palette_main(LV_PALETTE_CYAN), 0);
    lv_obj_set_style_bg_grad_color(orb, lv_palette_darken(LV_PALETTE_BLUE, 3), 0);
    lv_obj_set_style_bg_grad_dir(orb, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_color(orb, lv_palette_main(LV_PALETTE_CYAN), 0);
    lv_obj_set_style_shadow_width(orb, 22, 0);
    lv_obj_set_style_shadow_spread(orb, 1, 0);
    lv_obj_align(orb, LV_ALIGN_TOP_MID, 0, 44);

    lv_obj_t* title = lv_label_create(scr_boot);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_label_set_text(title, "PrintOrb");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 108);

    boot_bar = lv_bar_create(scr_boot);
    lv_obj_set_size(boot_bar, 150, 8);
    lv_obj_align(boot_bar, LV_ALIGN_CENTER, 0, 26);
    lv_bar_set_range(boot_bar, 0, 100);
    lv_bar_set_value(boot_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(boot_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(boot_bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(boot_bar, lv_color_hex(0x202830), LV_PART_MAIN);
    lv_obj_set_style_bg_color(boot_bar, lv_palette_main(LV_PALETTE_CYAN), LV_PART_INDICATOR);

    boot_step = lv_label_create(scr_boot);
    lv_obj_set_style_text_color(boot_step, lv_color_white(), 0);
    lv_obj_set_style_text_font(boot_step, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(boot_step, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(boot_step, "Starting");
    lv_obj_align(boot_step, LV_ALIGN_CENTER, 0, 48);

    boot_detail = lv_label_create(scr_boot);
    lv_obj_set_style_text_color(boot_detail, lv_color_hex(0x6b7480), 0);
    lv_obj_set_style_text_font(boot_detail, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(boot_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(boot_detail, LV_LABEL_LONG_DOT);
    lv_obj_set_width(boot_detail, 180);
    lv_label_set_text(boot_detail, "");
    lv_obj_align(boot_detail, LV_ALIGN_CENTER, 0, 68);
}

void buildUpdateScreen() {
    scr_update = makeScreen(lv_color_black());
    addRingFrame(scr_update);

    // Glowing orb mark, matching the boot screen's brand feel (amber = "busy").
    lv_obj_t* orb = lv_obj_create(scr_update);
    lv_obj_set_size(orb, 56, 56);
    lv_obj_clear_flag(orb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(orb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(orb, 0, 0);
    lv_obj_set_style_bg_color(orb, lv_palette_main(LV_PALETTE_AMBER), 0);
    lv_obj_set_style_bg_grad_color(orb, lv_palette_darken(LV_PALETTE_ORANGE, 3), 0);
    lv_obj_set_style_bg_grad_dir(orb, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_color(orb, lv_palette_main(LV_PALETTE_AMBER), 0);
    lv_obj_set_style_shadow_width(orb, 22, 0);
    lv_obj_set_style_shadow_spread(orb, 1, 0);
    lv_obj_align(orb, LV_ALIGN_TOP_MID, 0, 44);

    lv_obj_t* title = lv_label_create(scr_update);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_label_set_text(title, "Updating");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 108);

    upd_pct = lv_label_create(scr_update);
    lv_obj_set_style_text_color(upd_pct, lv_palette_main(LV_PALETTE_AMBER), 0);
    lv_obj_set_style_text_font(upd_pct, &lv_font_montserrat_28, 0);
    lv_label_set_text(upd_pct, "0%");
    lv_obj_align(upd_pct, LV_ALIGN_CENTER, 0, 4);

    upd_bar = lv_bar_create(scr_update);
    lv_obj_set_size(upd_bar, 150, 8);
    lv_obj_align(upd_bar, LV_ALIGN_CENTER, 0, 40);
    lv_bar_set_range(upd_bar, 0, 100);
    lv_bar_set_value(upd_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(upd_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(upd_bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(upd_bar, lv_color_hex(0x2a2218), LV_PART_MAIN);
    lv_obj_set_style_bg_color(upd_bar, lv_palette_main(LV_PALETTE_AMBER), LV_PART_INDICATOR);

    lv_obj_t* warn = lv_label_create(scr_update);
    lv_obj_set_style_text_color(warn, lv_color_hex(0x6b7480), 0);
    lv_obj_set_style_text_font(warn, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(warn, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(warn, "Do not power off");
    lv_obj_align(warn, LV_ALIGN_CENTER, 0, 64);
}

void buildDots(int count) {
    dots = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(dots);
    lv_obj_set_size(dots, count * 14, 12);
    lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_clear_flag(dots, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(dots, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots, 6, 0);
    for (int i = 0; i < count; i++) {
        dot[i] = lv_obj_create(dots);
        lv_obj_remove_style_all(dot[i]);
        lv_obj_set_size(dot[i], 7, 7);
        lv_obj_set_style_radius(dot[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot[i], lv_color_hex(0x3a424c), 0);
    }
    lv_obj_add_flag(dots, LV_OBJ_FLAG_HIDDEN);
}

// ---- Per-screen refreshers ----
void refreshStatus(const PrinterStatus& s, const String& label) {
    lv_color_t col = stateColor(s.state);
    lv_arc_set_value(arc_progress, (int)(s.progress + 0.5f));
    lv_obj_set_style_arc_color(arc_progress, col, LV_PART_INDICATOR);
    lv_label_set_text(lbl_printer, label.c_str());

    if (s.state == PrintState::OFFLINE) lv_label_set_text(lbl_percent, "--");
    else lv_label_set_text_fmt(lbl_percent, "%d%%", (int)(s.progress + 0.5f));

    lv_label_set_text(lbl_state, PrinterStatus::stateLabel(s.state));
    lv_obj_set_style_text_color(lbl_state, col, 0);
    lv_label_set_text(lbl_file, s.filename.length() ? s.filename.c_str() : "");
    lv_label_set_text_fmt(lbl_nozzle, "%d\xC2\xB0", (int)(s.nozzleTemp + 0.5f));
    lv_label_set_text_fmt(lbl_bed,    "%d\xC2\xB0", (int)(s.bedTemp + 0.5f));

    if (s.state == PrintState::PRINTING || s.state == PrintState::PAUSED) {
        lv_label_set_text(lbl_eta, fmtRemaining(s.remainingSec).c_str());
        if (s.totalLayer > 0) lv_label_set_text_fmt(lbl_layers, "%d/%d", max(s.currentLayer, 0), s.totalLayer);
        else                  lv_label_set_text(lbl_layers, "--");
    } else {
        lv_label_set_text(lbl_eta, "--");
        lv_label_set_text(lbl_layers, "--");
    }
}

void refreshDetails(const PrinterStatus& s, const String& label) {
    lv_label_set_text(dt_state, PrinterStatus::stateLabel(s.state));
    lv_obj_set_style_text_color(dt_state, stateColor(s.state), 0);

    // Resolve the active filament from the AMS snapshot (Bambu). -1 indices or
    // an absent/empty slot mean we have no colour to show.
    const AmsInfo& a = s.ams;
    const AmsSlot* sl = nullptr;
    if (a.present && a.activeUnit >= 0 && a.activeUnit < 4 &&
        a.activeSlot >= 0 && a.activeSlot < 4) {
        const AmsSlot& cand = a.unit[a.activeUnit].slot[a.activeSlot];
        if (cand.present) sl = &cand;
    }

    if (sl) {
        lv_obj_set_style_bg_color(dt_swatch, lv_color_hex(sl->color), 0);
        lv_obj_set_style_border_color(dt_swatch, lv_color_hex(0x9aa4ad), 0);
        lv_label_set_text(dt_type, sl->type.length() ? sl->type.c_str() : "Filament");

        String info;
        if (a.units > 1) info = "AMS " + String(a.activeUnit + 1) + " \xC2\xB7 ";
        info += "Slot " + String(a.activeSlot + 1);
        if (sl->remain >= 0) info += "  \xC2\xB7  " + String(sl->remain) + "%";
        lv_label_set_text(dt_slot, info.c_str());
    } else {
        // Klipper / no AMS / no loaded tray: neutral swatch + hint.
        lv_obj_set_style_bg_color(dt_swatch, lv_color_hex(0x2b3340), 0);
        lv_obj_set_style_border_color(dt_swatch, lv_color_hex(0x3a424c), 0);
        lv_label_set_text(dt_type, "—");
        lv_label_set_text(dt_slot, a.present ? "No active filament" : "No filament data");
    }

    lv_label_set_text(dt_file, s.filename.length() ? s.filename.c_str()
                                                   : (label.length() ? label.c_str() : ""));
}

void setBtnEnabled(lv_obj_t* b, bool en) {
    if (en) lv_obj_clear_state(b, LV_STATE_DISABLED);
    else    lv_obj_add_state(b, LV_STATE_DISABLED);
}

void refreshControls(const PrinterStatus& s) {
    bool printing = s.state == PrintState::PRINTING;
    bool paused   = s.state == PrintState::PAUSED;
    setBtnEnabled(btn_pause,  printing);
    setBtnEnabled(btn_resume, paused);
    setBtnEnabled(btn_stop,   printing || paused);
}

void refreshSystem() {
    if (WifiManager::isConnected())
        lv_label_set_text_fmt(sy_wifi, LV_SYMBOL_WIFI "  %s  %ddBm", WiFi.SSID().c_str(), (int)WiFi.RSSI());
    else
        lv_label_set_text(sy_wifi, LV_SYMBOL_WIFI "  offline");
    lv_label_set_text(sy_ip, WifiManager::ip().c_str());
    lv_label_set_text_fmt(sy_bright, "%d%%", (int)cfg.brightness);
}

void refreshIdle(const PrinterStatus& s, const String& label) {
    lv_label_set_text(id_printer, label.length() ? label.c_str() : "Printer");

    const char* word;
    switch (s.state) {
        case PrintState::COMPLETE: word = "Done";    break;
        case PrintState::OFFLINE:  word = "Offline"; break;
        default:                   word = "Ready";   break;  // IDLE
    }
    lv_label_set_text(id_state, word);
    lv_obj_set_style_text_color(id_state, stateColor(s.state), 0);

    // Temperatures (so cooling is visible); hidden when offline — no live data.
    lv_obj_t* trow = lv_obj_get_parent(id_temps);
    if (s.state == PrintState::OFFLINE) {
        lv_obj_add_flag(trow, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(trow, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(id_temps, "%d\xC2\xB0 / %d\xC2\xB0",
                              (int)(s.nozzleTemp + 0.5f), (int)(s.bedTemp + 0.5f));
    }
}

void renderAms() {
    const AmsInfo& a = g_ams;
    lv_obj_add_flag(ams_updown, LV_OBJ_FLAG_HIDDEN);  // replaced by the dot indicator
    if (!a.present || a.units == 0) {
        for (int i = 0; i < 4; i++) lv_obj_add_flag(ams_tile[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ams_humid, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(amsDots, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ams_title, "Filament");
        lv_obj_clear_flag(ams_none, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(ams_none, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ams_humid, LV_OBJ_FLAG_HIDDEN);
    if (amsUnitIdx >= a.units) amsUnitIdx = 0;
    const AmsUnit& U = a.unit[amsUnitIdx];

    if (a.units > 1) {
        if (U.isHT) lv_label_set_text(ams_title, "AMS HT");
        else        lv_label_set_text_fmt(ams_title, "AMS %d/%d", amsUnitIdx + 1, a.units);
        lv_obj_clear_flag(amsDots, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 4; i++) {
            if (i < a.units) {
                lv_obj_clear_flag(amsDot[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_bg_color(
                    amsDot[i], i == amsUnitIdx ? lv_palette_main(LV_PALETTE_CYAN) : lv_color_hex(0x3a424c), 0);
            } else {
                lv_obj_add_flag(amsDot[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    } else {
        lv_label_set_text(ams_title, U.isHT ? "AMS HT" : "Filament");
        lv_obj_add_flag(amsDots, LV_OBJ_FLAG_HIDDEN);
    }

    // AMS HT is single-slot: show one tile, hide the rest (flex re-centres it).
    int shown = U.isHT ? 1 : 4;
    for (int i = 0; i < 4; i++) {
        if (i >= shown) { lv_obj_add_flag(ams_tile[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_clear_flag(ams_tile[i], LV_OBJ_FLAG_HIDDEN);
        const AmsSlot& sl = U.slot[i];
        bool used = (i < U.count) && sl.present;
        uint32_t c = used ? sl.color : 0x2b3340;
        lv_obj_set_style_bg_color(ams_tile[i], lv_color_hex(c), 0);

        int lum = ((int)((c >> 16) & 0xff) * 299 + (int)((c >> 8) & 0xff) * 587 + (int)(c & 0xff) * 114) / 1000;
        lv_color_t txt = (used && lum > 140) ? lv_color_black() : lv_color_white();
        lv_obj_set_style_text_color(ams_type[i], txt, 0);
        lv_obj_set_style_text_color(ams_remain[i], txt, 0);

        lv_label_set_text(ams_type[i], used ? sl.type.c_str() : "empty");
        if (used && sl.remain >= 0) lv_label_set_text_fmt(ams_remain[i], "%d%%", sl.remain);
        else                        lv_label_set_text(ams_remain[i], "");

        bool active = (a.activeUnit == amsUnitIdx && a.activeSlot == i);
        lv_obj_set_style_border_color(ams_tile[i], active ? lv_palette_main(LV_PALETTE_CYAN) : lv_color_hex(c), 0);
        lv_obj_set_style_border_width(ams_tile[i], active ? 3 : 2, 0);
    }

    if (U.humidity >= 0) lv_label_set_text_fmt(ams_humid, "Humidity  %d/5", U.humidity);
    else                 lv_label_set_text(ams_humid, "");
}

void refreshAms(const AmsInfo& a) {
    g_ams = a;
    if (amsUnitIdx >= a.units) amsUnitIdx = 0;
    renderAms();
}

}  // namespace

namespace UI {

void begin() {
    buildStatusScreen();
    buildDetailsScreen();
    buildSystemScreen();
    bool hasAms = (cfg.printerType == PrinterType::BAMBU);
    if (hasAms) buildAmsScreen();
    buildControlsScreen();
    buildIdleScreen();
    buildSetupScreen();
    buildBootScreen();
    buildUpdateScreen();

    int i = 0;
    carousel[i++] = scr_status;
    carousel[i++] = scr_details;
    carousel[i++] = scr_system;
    if (hasAms) carousel[i++] = scr_ams;
    carousel[i++] = scr_controls;
    carCount = i;

    buildDots(carCount);
    showBoot("Starting", 0);
}

void setControlHandler(ControlCb cb) { g_ctrl = cb; }

bool isResting(PrintState s) {
    return s == PrintState::IDLE || s == PrintState::OFFLINE || s == PrintState::COMPLETE;
}

void update(const PrinterStatus& s, const String& printerLabel) {
    static bool started    = false;
    static bool wasResting = false;

    // Keep every screen's widgets current (cheap; ready whenever shown).
    refreshStatus(s, printerLabel);
    refreshDetails(s, printerLabel);
    if (scr_ams) refreshAms(s.ams);
    refreshControls(s);
    refreshSystem();
    refreshIdle(s, printerLabel);

    bool resting = isResting(s.state);
    if (!started) {
        started    = true;
        wasResting = resting;
        if (resting) showIdle();
        else         enterCarousel();
        return;
    }

    // Switch context only on an active<->resting transition, so the user can
    // freely swipe into the carousel while idle without being yanked back.
    if (resting != wasResting) {
        wasResting = resting;
        if (resting) showIdle();
        else         enterCarousel();
    }
}

void showSetup(const String& ssid, const String& ip) {
    carActive = false;
    lv_obj_add_flag(dots, LV_OBJ_FLAG_HIDDEN);
    if (lv_scr_act() != scr_setup) lv_scr_load(scr_setup);
    String body = "Connect to WiFi:\n#00e5ff " + ssid + "#\n\nThen open:\n#ffffff " + ip + "#";
    lv_label_set_recolor(setup_body, true);
    lv_label_set_text(setup_body, body.c_str());
}

void showBoot(const char* step, uint8_t pct, const char* detail) {
    carActive = false;
    if (dots) lv_obj_add_flag(dots, LV_OBJ_FLAG_HIDDEN);
    if (lv_scr_act() != scr_boot) lv_scr_load(scr_boot);
    lv_bar_set_value(boot_bar, pct, LV_ANIM_ON);
    lv_label_set_text(boot_step, step);
    lv_label_set_text(boot_detail, detail ? detail : "");
}

void showUpdate(uint8_t pct) {
    carActive = false;
    if (dots) lv_obj_add_flag(dots, LV_OBJ_FLAG_HIDDEN);
    if (lv_scr_act() != scr_update) lv_scr_load(scr_update);
    lv_bar_set_value(upd_bar, pct, LV_ANIM_OFF);
    lv_label_set_text_fmt(upd_pct, "%d%%", pct);
}

}  // namespace UI
