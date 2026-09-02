#include "ui.h"
#include "theme.h"
#include "orb_icons.h"
#include "logbuf.h"
#include "display.h"
#include "wifi_manager.h"
#include "timekeeper.h"
#include "updater.h"
#include "version.h"
#include <lvgl.h>
#include <WiFi.h>

namespace {

using Theme::c;

// --- Screens ---------------------------------------------------------------
lv_obj_t* scr_status   = nullptr;
lv_obj_t* scr_job      = nullptr;
lv_obj_t* scr_ams      = nullptr;   // built only when a Bambu is configured
lv_obj_t* scr_control  = nullptr;
lv_obj_t* scr_printers = nullptr;   // built only with more than one printer
lv_obj_t* scr_system   = nullptr;
lv_obj_t* scr_idle     = nullptr;
lv_obj_t* scr_setup    = nullptr;
lv_obj_t* scr_boot     = nullptr;
lv_obj_t* scr_update   = nullptr;

// Carousel: the ordered subset of screens horizontal swipes cycle through.
// Rebuilt whenever the printer selection changes (see rebuildCarousel).
const int MAX_PAGES = 6;
lv_obj_t* carousel[MAX_PAGES] = {};
int  carCount = 0;
int  carIdx   = 0;

UI::ControlCb       g_ctrl   = nullptr;
UI::PrinterSwitchCb g_switch = nullptr;

PrintState g_lastState = PrintState::OFFLINE;  // for the update button print-guard

// Idle-screen <-> carousel state machine (see UI::update). `g_rebaseline` makes
// the next status take effect without moving the user, which is what a printer
// switch needs: the new client reports OFFLINE for a moment and would otherwise
// yank the user off the page they just tapped.
bool g_uiStarted  = false;
bool g_wasResting = false;
bool g_rebaseline = false;
AmsInfo    g_ams;                              // last AMS snapshot (re-render on swipe)
int        amsUnitIdx = 0;                     // currently shown AMS unit

// LVGL reports a swipe while the finger is still down, and still emits CLICKED /
// LONG_PRESSED on release for the widget the swipe started on. This flag lets
// button handlers reject a press that turned into a page change.
bool g_swiping = false;

// --- Widgets ---------------------------------------------------------------
lv_obj_t *st_arc, *st_name, *st_file, *st_pct, *st_state,
         *st_eta_row, *st_eta, *st_temp_row, *st_noz, *st_bed;

lv_obj_t *jb_swatch, *jb_type, *jb_slot, *jb_layer, *jb_eta, *jb_file;

lv_obj_t *ams_tile[4], *ams_type[4], *ams_remain[4];
lv_obj_t *ams_title, *ams_humid, *ams_none, *ams_dots, *ams_dot[4];
lv_obj_t *btn_dry, *lbl_dry;

lv_obj_t *ct_state, *ct_hint, *btn_primary, *lbl_primary, *btn_stop;

lv_obj_t *pr_row[ORB_MAX_PRINTERS], *pr_badge[ORB_MAX_PRINTERS],
         *pr_name[ORB_MAX_PRINTERS], *pr_mark[ORB_MAX_PRINTERS];

lv_obj_t *sy_wifi, *sy_ip, *sy_bright, *sy_ver, *lbl_upd, *btn_upd;

lv_obj_t *id_clock, *id_orb, *id_name, *id_state, *id_temps;

lv_obj_t *boot_bar, *boot_step, *boot_detail;
lv_obj_t *setup_body;
lv_obj_t *upd_bar, *upd_pct;

lv_obj_t *dots = nullptr, *dot[MAX_PAGES] = {};
lv_obj_t *toastBox = nullptr, *toastLbl = nullptr;
lv_timer_t* toastTimer = nullptr;

// --- Formatting ------------------------------------------------------------
String fmtRemaining(int32_t sec) {
    if (sec < 0) return String("--");
    int h = sec / 3600, m = (sec % 3600) / 60;
    char buf[16];
    if (h > 0) snprintf(buf, sizeof(buf), "%dh %02dm", h, m);
    else       snprintf(buf, sizeof(buf), "%dm", m);
    return String(buf);
}

/**
 * Wall-clock time the job should finish at ("18:42"), or "" when the clock has
 * not synced or nothing is running. More useful than a countdown for long jobs.
 */
String fmtFinishTime(int32_t remainingSec) {
    if (remainingSec < 0) return String();
    int now = Time::localMinutes();
    if (now < 0) return String();
    int at = (now + (remainingSec + 30) / 60) % (24 * 60);
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", at / 60, at % 60);
    return String(buf);
}

// ---------------------------------------------------------------- widgets ---
void gesture_cb(lv_event_t* e);

/** Full-screen page with the shared background and the carousel gesture hook. */
lv_obj_t* makeScreen() {
    lv_obj_t* s = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s, c(Theme::BG), 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s, gesture_cb, LV_EVENT_GESTURE, NULL);
    return s;
}

/** Thin bezel-hugging ring; ties the rectangular widget world to the round panel. */
void addRing(lv_obj_t* scr) {
    lv_obj_t* ring = lv_obj_create(scr);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 234, 234);
    lv_obj_center(ring);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_set_style_border_color(ring, c(Theme::LINE), 0);
    lv_obj_set_style_border_opa(ring, LV_OPA_60, 0);
}

/** Small accent page title pinned near the top of the circle. */
void addPageTitle(lv_obj_t* scr, const char* text) {
    lv_obj_t* t = lv_label_create(scr);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t, c(Theme::ACCENT), 0);
    lv_label_set_text(t, text);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 30);
}

/**
 * Centred, transparent content column. `scrollable` allows vertical dragging on
 * pages whose content is taller than the panel; those pages give up vertical
 * gestures in exchange (which only the AMS page uses).
 */
lv_obj_t* makeColumn(lv_obj_t* scr, lv_coord_t h, lv_coord_t gap, bool scrollable = false) {
    lv_obj_t* col = lv_obj_create(scr);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, Theme::CONTENT_W + 12, h);
    lv_obj_center(col);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, gap, 0);
    if (scrollable) {
        lv_obj_set_scroll_dir(col, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_style_pad_ver(col, 6, 0);
    } else {
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    }
    return col;
}

lv_obj_t* addLabel(lv_obj_t* parent, const lv_font_t* font, uint32_t color,
                   const char* text = "") {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, c(color), 0);
    lv_label_set_text(l, text);
    return l;
}

/** Single-line label that ellipsises instead of wrapping out of the circle. */
lv_obj_t* addClippedLabel(lv_obj_t* parent, const lv_font_t* font, uint32_t color,
                          lv_coord_t w, lv_label_long_mode_t mode = LV_LABEL_LONG_DOT) {
    lv_obj_t* l = addLabel(parent, font, color);
    lv_label_set_long_mode(l, mode);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

/** Transparent horizontal flex row for icon+value pairs. */
lv_obj_t* addRow(lv_obj_t* parent, lv_coord_t w, lv_coord_t h, lv_coord_t gap) {
    lv_obj_t* r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, w, h);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, gap, 0);
    return r;
}

/** Icon glyph + value label pair; returns the value label. */
lv_obj_t* addIconValue(lv_obj_t* row, const char* icon, uint32_t color,
                       const lv_font_t* valueFont) {
    lv_obj_t* ic = lv_label_create(row);
    lv_obj_set_style_text_font(ic, &orb_icons, 0);
    lv_obj_set_style_text_color(ic, c(color), 0);
    lv_label_set_text(ic, icon);

    lv_obj_t* v = lv_label_create(row);
    lv_obj_set_style_text_font(v, valueFont, 0);
    lv_obj_set_style_text_color(v, c(color), 0);
    lv_label_set_text(v, "--");
    return v;
}

// --- Buttons ---------------------------------------------------------------
// Every button clears the swipe flag when pressed, so a fresh tap is always
// accepted even if the previous gesture ended on top of it.
void press_cb(lv_event_t* /*e*/) { g_swiping = false; }

/** True when the current press was a tap/hold rather than the start of a swipe. */
bool accepted() { return !g_swiping; }

/**
 * True when an event should act. LVGL's pointer path does not itself suppress
 * events on LV_STATE_DISABLED widgets, so greyed-out buttons must check.
 */
bool actionable(lv_event_t* e) {
    return accepted() && !lv_obj_has_state(lv_event_get_target(e), LV_STATE_DISABLED);
}

void styleButton(lv_obj_t* b, uint32_t color) {
    lv_obj_set_style_bg_color(b, c(color), 0);
    lv_obj_set_style_bg_color(b, c(color), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_radius(b, Theme::RADIUS, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, press_cb, LV_EVENT_PRESSED, NULL);
}

lv_obj_t* makeButton(lv_obj_t* parent, lv_coord_t w, lv_coord_t h,
                     uint32_t color, lv_event_cb_t cb, void* ud, lv_obj_t** outLabel) {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_set_size(b, w, h);
    styleButton(b, color);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t* l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_center(l);
    if (outLabel) *outLabel = l;
    return b;
}

/**
 * Hold-to-confirm button. A tinted bar sweeps across it while the finger is
 * down and reaches the far edge exactly when the action fires, so the user can
 * see how long to hold and can abort by lifting early.
 */
void holdFeedback_cb(lv_event_t* e) {
    // Note: LV_EVENT_LONG_PRESSED deliberately does not reset the fill. LVGL keeps
    // sending PRESSING until the finger lifts, so clearing it here would only make
    // the bar flicker back to full; leaving it filled reads as "done".
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSING && code != LV_EVENT_PRESSED &&
        code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) return;

    lv_obj_t* btn  = lv_event_get_target(e);
    lv_obj_t* fill = lv_obj_get_child(btn, 0);

    if (code == LV_EVENT_PRESSING && actionable(e)) {
        lv_indev_t* d = lv_indev_get_act();
        uint32_t held = d ? lv_tick_elaps(d->proc.pr_timestamp) : 0;
        if (held > Theme::HOLD_MS) held = Theme::HOLD_MS;
        lv_obj_set_width(fill, (lv_coord_t)((int32_t)lv_obj_get_width(btn) * held / Theme::HOLD_MS));
    } else {
        lv_obj_set_width(fill, 0);
    }
}

lv_obj_t* makeHoldButton(lv_obj_t* parent, lv_coord_t w, lv_coord_t h,
                         uint32_t color, lv_event_cb_t cb, void* ud, lv_obj_t** outLabel) {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_set_size(b, w, h);
    styleButton(b, color);
    lv_obj_set_style_clip_corner(b, true, 0);
    lv_obj_set_style_pad_all(b, 0, 0);

    lv_obj_t* fill = lv_obj_create(b);            // child 0: the sweep indicator
    lv_obj_remove_style_all(fill);
    lv_obj_set_size(fill, 0, LV_PCT(100));
    lv_obj_align(fill, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(fill, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(fill, lv_color_white(), 0);

    lv_obj_add_event_cb(b, holdFeedback_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(b, cb, LV_EVENT_LONG_PRESSED, ud);

    lv_obj_t* l = lv_label_create(b);             // child 1: on top of the fill
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_center(l);
    if (outLabel) *outLabel = l;
    return b;
}

void setEnabled(lv_obj_t* b, bool en) {
    if (en) lv_obj_clear_state(b, LV_STATE_DISABLED);
    else    lv_obj_add_state(b, LV_STATE_DISABLED);
}

void setHidden(lv_obj_t* o, bool hidden) {
    if (hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else        lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// ------------------------------------------------------------- navigation ---
void layoutDots() {
    lv_obj_set_size(dots, carCount * 14, 12);
    for (int i = 0; i < MAX_PAGES; i++) {
        setHidden(dot[i], i >= carCount);
        lv_obj_set_style_bg_color(dot[i], i == carIdx ? c(Theme::ACCENT) : c(Theme::LINE), 0);
        lv_obj_set_size(dot[i], i == carIdx ? 8 : 6, i == carIdx ? 8 : 6);
    }
}

void gotoPage(int idx, lv_scr_load_anim_t anim) {
    if (carCount <= 0) return;
    idx = (idx + carCount) % carCount;          // wrap around at both ends
    if (idx == carIdx) return;
    carIdx = idx;
    lv_scr_load_anim(carousel[idx], anim, 200, 0, false);
    layoutDots();
}

/** Recompute the page order for the active printer and keep the user in place. */
void rebuildCarousel() {
    lv_obj_t* current = (carCount > 0) ? carousel[carIdx] : nullptr;

    int i = 0;
    carousel[i++] = scr_status;
    carousel[i++] = scr_job;
    if (scr_ams && cfg.printer().type == PrinterType::BAMBU) carousel[i++] = scr_ams;
    carousel[i++] = scr_control;
    if (scr_printers && cfg.printerCount > 1)                carousel[i++] = scr_printers;
    carousel[i++] = scr_system;
    carCount = i;

    carIdx = 0;
    bool kept = false;
    for (int k = 0; k < carCount; k++)
        if (carousel[k] == current) { carIdx = k; kept = true; break; }

    // The page the user was on can disappear (the AMS page when switching to a
    // Klipper printer). Leaving it loaded would strand them on a screen the
    // swipe handler no longer recognises, so fall back to the first page.
    if (!kept && current && lv_scr_act() == current) lv_scr_load(carousel[0]);
    layoutDots();
}

bool carouselShowing() {
    lv_obj_t* act = lv_scr_act();
    for (int i = 0; i < carCount; i++) if (carousel[i] == act) return true;
    return false;
}

void enterCarousel() {
    carIdx = 0;
    if (lv_scr_act() != scr_status) lv_scr_load(scr_status);
    setHidden(dots, false);
    layoutDots();
}

void showIdle() {
    setHidden(dots, true);
    if (scr_idle && lv_scr_act() != scr_idle) lv_scr_load(scr_idle);
}

void renderAms();

void gesture_cb(lv_event_t* /*e*/) {
    g_swiping = true;
    lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());

    // From the idle screen any horizontal swipe opens the carousel.
    if (scr_idle && lv_scr_act() == scr_idle) {
        if (d == LV_DIR_LEFT || d == LV_DIR_RIGHT) enterCarousel();
        return;
    }
    if (!carouselShowing()) return;

    // On the AMS page vertical swipes step through the AMS units instead.
    if (scr_ams && lv_scr_act() == scr_ams && (d == LV_DIR_TOP || d == LV_DIR_BOTTOM)) {
        if (g_ams.units > 1) {
            amsUnitIdx = (amsUnitIdx + (d == LV_DIR_TOP ? 1 : g_ams.units - 1)) % g_ams.units;
            renderAms();
        }
        return;
    }

    if      (d == LV_DIR_LEFT)  gotoPage(carIdx + 1, LV_SCR_LOAD_ANIM_MOVE_LEFT);
    else if (d == LV_DIR_RIGHT) gotoPage(carIdx - 1, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
}

// ------------------------------------------------------------------ toast ---
void toastHide_cb(lv_timer_t* /*t*/) {
    setHidden(toastBox, true);
    toastTimer = nullptr;
}

void showToast(const char* text) {
    if (!toastBox) return;
    lv_label_set_text(toastLbl, text);
    setHidden(toastBox, false);
    lv_obj_move_foreground(toastBox);
    if (toastTimer) lv_timer_del(toastTimer);
    toastTimer = lv_timer_create(toastHide_cb, 1600, NULL);
    lv_timer_set_repeat_count(toastTimer, 1);
}

// -------------------------------------------------------------- callbacks ---
void primary_cb(lv_event_t* e) {
    if (!actionable(e)) return;
    bool paused = (g_lastState == PrintState::PAUSED);
    UI::Ctrl c2 = paused ? UI::CTRL_RESUME : UI::CTRL_PAUSE;
    Log::printf("[UI] control %d\n", (int)c2);
    showToast(paused ? "Resuming…" : "Pausing…");
    if (g_ctrl) g_ctrl(c2);
}

void ctrl_cb(lv_event_t* e) {
    if (!actionable(e)) return;
    UI::Ctrl c2 = (UI::Ctrl)(intptr_t)lv_event_get_user_data(e);
    Log::printf("[UI] control %d\n", (int)c2);
    static const char* words[] = { "Pausing…", "Resuming…", "Stopping…",
                                   "Drying…", "Stopping dryer…" };
    showToast(words[c2]);
    if (g_ctrl) g_ctrl(c2);
}

// Toggles AMS HT drying; start vs. stop follows the unit's current state.
void dry_cb(lv_event_t* e) {
    if (!actionable(e)) return;
    for (uint8_t i = 0; i < g_ams.units; i++) {
        if (!g_ams.unit[i].isHT) continue;
        UI::Ctrl c2 = g_ams.unit[i].drying ? UI::CTRL_DRY_STOP : UI::CTRL_DRY_START;
        showToast(g_ams.unit[i].drying ? "Stopping dryer…" : "Drying…");
        if (g_ctrl) g_ctrl(c2);
        return;
    }
}

void reboot_cb(lv_event_t* e) {
    if (!actionable(e)) return;
    Log::printf("[UI] reboot requested\n");
    ESP.restart();
}

void update_cb(lv_event_t* e) {
    if (!actionable(e)) return;
    if (g_lastState == PrintState::PRINTING || g_lastState == PrintState::PAUSED) {
        Log::printf("[UI] update blocked (print active)\n");
        showToast("Busy — job running");
        return;
    }
    Log::printf("[UI] firmware update confirmed\n");
    showToast("Downloading…");
    Updater::requestApply();
}

void bright_cb(lv_event_t* e) {
    if (!actionable(e)) return;
    int v = (int)cfg.brightness + (int)(intptr_t)lv_event_get_user_data(e);
    cfg.brightness = (uint8_t)constrain(v, 10, 100);
    Display::setBrightness(cfg.brightness);
    Config::save();
    lv_label_set_text_fmt(sy_bright, "%d%%", (int)cfg.brightness);
}

void pickPrinter_cb(lv_event_t* e) {
    if (!actionable(e)) return;
    uint8_t idx = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    if (idx >= cfg.printerCount || idx == cfg.activePrinter) return;
    Log::printf("[UI] switch to printer %u\n", (unsigned)idx);
    if (g_switch) g_switch(idx);
}

// --------------------------------------------------------------- builders ---
void buildStatusScreen() {
    scr_status = makeScreen();

    st_arc = lv_arc_create(scr_status);
    lv_obj_set_size(st_arc, 228, 228);
    lv_obj_center(st_arc);
    lv_arc_set_rotation(st_arc, 135);
    lv_arc_set_bg_angles(st_arc, 0, 270);
    lv_arc_set_range(st_arc, 0, 100);
    lv_arc_set_value(st_arc, 0);
    lv_obj_remove_style(st_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(st_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(st_arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(st_arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(st_arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(st_arc, c(Theme::TRACK), LV_PART_MAIN);

    lv_obj_t* col = makeColumn(scr_status, 200, 4);
    st_name  = addClippedLabel(col, &lv_font_montserrat_12, Theme::TEXT_DIM, 170);
    st_file  = addClippedLabel(col, &lv_font_montserrat_12, Theme::TEXT_FAINT, 158,
                               LV_LABEL_LONG_SCROLL_CIRCULAR);
    st_pct   = addLabel(col, &lv_font_montserrat_40, Theme::TEXT, "--");
    st_state = addLabel(col, &lv_font_montserrat_16, Theme::TEXT_FAINT, "Offline");

    st_eta_row = addRow(col, 170, 22, 6);
    st_eta     = addIconValue(st_eta_row, ORB_ICON_CLOCK, Theme::TEXT_DIM, &lv_font_montserrat_14);

    st_temp_row = addRow(col, 176, 20, 6);
    st_noz = addIconValue(st_temp_row, ORB_ICON_NOZZLE, Theme::NOZZLE, &lv_font_montserrat_12);
    st_bed = addIconValue(st_temp_row, ORB_ICON_BED,    Theme::BED,    &lv_font_montserrat_12);
}

// "Job" page: what is actually being printed — filament colour and type first,
// because that is the thing a glance from across the room cannot infer.
void buildJobScreen() {
    scr_job = makeScreen();
    addRing(scr_job);
    addPageTitle(scr_job, "JOB");
    lv_obj_t* col = makeColumn(scr_job, 190, 7);

    jb_swatch = lv_obj_create(col);
    lv_obj_remove_style_all(jb_swatch);
    lv_obj_set_size(jb_swatch, 66, 66);
    lv_obj_clear_flag(jb_swatch, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(jb_swatch, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(jb_swatch, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(jb_swatch, c(Theme::SURFACE_HI), 0);
    lv_obj_set_style_border_width(jb_swatch, 3, 0);
    lv_obj_set_style_border_color(jb_swatch, c(Theme::LINE), 0);

    jb_type  = addLabel(col, &lv_font_montserrat_20, Theme::TEXT, "—");
    jb_slot  = addLabel(col, &lv_font_montserrat_12, Theme::TEXT_DIM, "");
    jb_layer = addLabel(col, &lv_font_montserrat_14, Theme::TEXT, "");
    jb_eta   = addLabel(col, &lv_font_montserrat_12, Theme::TEXT_DIM, "");
    jb_file  = addClippedLabel(col, &lv_font_montserrat_12, Theme::TEXT_FAINT, 164);
}

void buildAmsScreen() {
    scr_ams = makeScreen();
    addRing(scr_ams);
    lv_obj_t* col = makeColumn(scr_ams, 210, 8);

    ams_title = addLabel(col, &lv_font_montserrat_12, Theme::ACCENT, "FILAMENT");

    lv_obj_t* grid = lv_obj_create(col);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, 172, 128);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(grid, 8, 0);
    lv_obj_set_style_pad_column(grid, 8, 0);

    for (int i = 0; i < 4; i++) {
        lv_obj_t* tile = lv_obj_create(grid);
        lv_obj_remove_style_all(tile);
        lv_obj_set_size(tile, 78, 56);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(tile, Theme::RADIUS, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(tile, c(Theme::SURFACE_HI), 0);
        lv_obj_set_style_border_width(tile, 2, 0);
        lv_obj_set_style_border_color(tile, c(Theme::SURFACE_HI), 0);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(tile, 2, 0);

        ams_type[i]   = addLabel(tile, &lv_font_montserrat_14, Theme::TEXT, "-");
        ams_remain[i] = addLabel(tile, &lv_font_montserrat_12, Theme::TEXT, "");
        ams_tile[i]   = tile;
    }

    ams_humid = addLabel(col, &lv_font_montserrat_12, Theme::TEXT_DIM, "");
    ams_none  = addLabel(col, &lv_font_montserrat_14, Theme::TEXT_FAINT, "No AMS detected");
    setHidden(ams_none, true);

    // One dot per AMS unit on the right edge; vertical swipes move between them.
    ams_dots = lv_obj_create(scr_ams);
    lv_obj_remove_style_all(ams_dots);
    lv_obj_set_size(ams_dots, 12, 4 * 13);
    lv_obj_align(ams_dots, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_clear_flag(ams_dots, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(ams_dots, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ams_dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ams_dots, 6, 0);
    for (int i = 0; i < 4; i++) {
        ams_dot[i] = lv_obj_create(ams_dots);
        lv_obj_remove_style_all(ams_dot[i]);
        lv_obj_set_size(ams_dot[i], 6, 6);
        lv_obj_set_style_radius(ams_dot[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(ams_dot[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(ams_dot[i], c(Theme::LINE), 0);
    }
    setHidden(ams_dots, true);

    // Overlaid at the bottom so it never disturbs the centred column layout.
    btn_dry = makeHoldButton(scr_ams, 132, 32, Theme::WARN, dry_cb, NULL, &lbl_dry);
    lv_obj_align(btn_dry, LV_ALIGN_BOTTOM_MID, 0, -22);
    setHidden(btn_dry, true);
}

// One primary action that follows the job state (Pause <-> Resume) instead of a
// stack of buttons that are mostly disabled, plus a hold-to-confirm Stop.
void buildControlScreen() {
    scr_control = makeScreen();
    addRing(scr_control);
    addPageTitle(scr_control, "CONTROL");
    lv_obj_t* col = makeColumn(scr_control, 190, 14);

    ct_state    = addLabel(col, &lv_font_montserrat_20, Theme::TEXT, "Offline");
    btn_primary = makeButton(col, 156, 44, Theme::SURFACE_HI, primary_cb, NULL, &lbl_primary);
    lv_label_set_text(lbl_primary, LV_SYMBOL_PAUSE "  Pause");
    lv_obj_t* ls;
    btn_stop = makeHoldButton(col, 156, 40, Theme::DANGER, ctrl_cb,
                              (void*)(intptr_t)UI::CTRL_STOP, &ls);
    lv_label_set_text(ls, LV_SYMBOL_STOP "  Hold to stop");
    ct_hint     = addLabel(col, &lv_font_montserrat_12, Theme::TEXT_FAINT, "No active job");
}

void buildPrintersScreen() {
    scr_printers = makeScreen();
    addRing(scr_printers);
    addPageTitle(scr_printers, "PRINTERS");
    lv_obj_t* col = makeColumn(scr_printers, 190, 8);

    for (uint8_t i = 0; i < ORB_MAX_PRINTERS; i++) {
        lv_obj_t* row = lv_btn_create(col);
        lv_obj_set_size(row, Theme::ROW_W, 38);
        styleButton(row, Theme::SURFACE);
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_set_style_border_color(row, c(Theme::SURFACE), 0);
        lv_obj_set_style_pad_hor(row, 8, 0);
        lv_obj_add_event_cb(row, pickPrinter_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 8, 0);

        // Round K/B badge: the printer's backend at a glance.
        lv_obj_t* badge = lv_label_create(row);
        lv_obj_set_style_text_font(badge, &lv_font_montserrat_12, 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_all(badge, 5, 0);
        lv_label_set_text(badge, "K");

        lv_obj_t* name = lv_label_create(row);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, 96);
        lv_label_set_text(name, "");

        lv_obj_t* mark = lv_label_create(row);
        lv_obj_set_style_text_font(mark, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(mark, c(Theme::ACCENT), 0);
        lv_label_set_text(mark, LV_SYMBOL_OK);

        pr_row[i] = row; pr_badge[i] = badge; pr_name[i] = name; pr_mark[i] = mark;
        setHidden(row, true);
    }
}

void buildSystemScreen() {
    scr_system = makeScreen();
    addRing(scr_system);
    addPageTitle(scr_system, "SYSTEM");
    // Taller than the panel on purpose: vertical drags scroll this page.
    lv_obj_t* col = makeColumn(scr_system, 196, 12, true);

    sy_wifi = addClippedLabel(col, &lv_font_montserrat_14, Theme::TEXT, 168);
    lv_label_set_text(sy_wifi, LV_SYMBOL_WIFI);
    sy_ip   = addLabel(col, &lv_font_montserrat_16, Theme::TEXT_DIM, "0.0.0.0");

    lv_obj_t* brow = addRow(col, 156, 36, 12);
    lv_obj_t* lm;
    makeButton(brow, 42, 32, Theme::SURFACE_HI, bright_cb, (void*)(intptr_t)-10, &lm);
    lv_label_set_text(lm, LV_SYMBOL_MINUS);
    sy_bright = addLabel(brow, &lv_font_montserrat_14, Theme::TEXT, "100%");
    lv_obj_set_width(sy_bright, 46);
    lv_obj_set_style_text_align(sy_bright, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t* lp;
    makeButton(brow, 42, 32, Theme::SURFACE_HI, bright_cb, (void*)(intptr_t)10, &lp);
    lv_label_set_text(lp, LV_SYMBOL_PLUS);

    sy_ver = addLabel(col, &lv_font_montserrat_12, Theme::TEXT_FAINT, "v?");

    // Only shown once a newer release has been seen (see refreshSystem).
    btn_upd = makeHoldButton(col, 156, 36, Theme::OK, update_cb, NULL, &lbl_upd);
    lv_label_set_text(lbl_upd, LV_SYMBOL_DOWNLOAD "  Hold to update");
    setHidden(btn_upd, true);

    lv_obj_t* lr;
    makeHoldButton(col, 156, 36, Theme::DANGER, reboot_cb, NULL, &lr);
    lv_label_set_text(lr, LV_SYMBOL_POWER "  Hold to reboot");
}

/** The brand mark: a glowing orb, reused by the boot, idle and update screens. */
lv_obj_t* makeOrb(lv_obj_t* parent, lv_coord_t size, uint32_t from, uint32_t to) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_set_size(o, size, size);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_bg_color(o, c(from), 0);
    lv_obj_set_style_bg_grad_color(o, c(to), 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_color(o, c(from), 0);
    lv_obj_set_style_shadow_width(o, 22, 0);
    lv_obj_set_style_shadow_spread(o, 1, 0);
    return o;
}

// Resting screen. Shows the wall clock when NTP has synced — a printer that is
// not printing is more useful as a desk clock than as an empty status page.
void buildIdleScreen() {
    scr_idle = makeScreen();
    addRing(scr_idle);
    lv_obj_t* col = makeColumn(scr_idle, 200, 10);

    id_orb   = makeOrb(col, 40, Theme::ACCENT, 0x0d47a1);
    id_clock = addLabel(col, &lv_font_montserrat_40, Theme::TEXT, "--:--");
    setHidden(id_clock, true);
    id_name  = addClippedLabel(col, &lv_font_montserrat_14, Theme::TEXT_DIM, 176);
    id_state = addLabel(col, &lv_font_montserrat_24, Theme::IDLE, "Ready");

    lv_obj_t* trow = addRow(col, 176, 20, 6);
    id_temps = addIconValue(trow, ORB_ICON_NOZZLE, Theme::TEXT_DIM, &lv_font_montserrat_12);
}

void buildSetupScreen() {
    scr_setup = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_setup, c(0x0b1016), 0);
    lv_obj_clear_flag(scr_setup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* col = makeColumn(scr_setup, 200, 12);
    lv_obj_t* t = addLabel(col, &lv_font_montserrat_20, Theme::ACCENT, "Setup");
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);

    setup_body = addLabel(col, &lv_font_montserrat_14, Theme::TEXT, "");
    lv_obj_set_style_text_align(setup_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(setup_body, 190);
    lv_label_set_recolor(setup_body, true);
}

void buildBootScreen() {
    scr_boot = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_boot, c(Theme::BG), 0);
    lv_obj_clear_flag(scr_boot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* orb = makeOrb(scr_boot, 54, Theme::ACCENT, 0x0d47a1);
    lv_obj_align(orb, LV_ALIGN_TOP_MID, 0, 48);

    lv_obj_t* title = addLabel(scr_boot, &lv_font_montserrat_20, Theme::TEXT, "PrintOrb");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 112);

    boot_bar = lv_bar_create(scr_boot);
    lv_obj_set_size(boot_bar, 140, 6);
    lv_obj_align(boot_bar, LV_ALIGN_CENTER, 0, 30);
    lv_bar_set_range(boot_bar, 0, 100);
    lv_bar_set_value(boot_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(boot_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(boot_bar, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(boot_bar, c(Theme::TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(boot_bar, c(Theme::ACCENT), LV_PART_INDICATOR);

    boot_step = addLabel(scr_boot, &lv_font_montserrat_14, Theme::TEXT, "Starting");
    lv_obj_set_style_text_align(boot_step, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(boot_step, LV_ALIGN_CENTER, 0, 52);

    boot_detail = addClippedLabel(scr_boot, &lv_font_montserrat_12, Theme::TEXT_FAINT, 176);
    lv_obj_align(boot_detail, LV_ALIGN_CENTER, 0, 74);
}

void buildUpdateScreen() {
    scr_update = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_update, c(Theme::BG), 0);
    lv_obj_clear_flag(scr_update, LV_OBJ_FLAG_SCROLLABLE);
    addRing(scr_update);

    lv_obj_t* orb = makeOrb(scr_update, 54, Theme::WARN, 0x8d4b00);
    lv_obj_align(orb, LV_ALIGN_TOP_MID, 0, 48);

    lv_obj_t* title = addLabel(scr_update, &lv_font_montserrat_20, Theme::TEXT, "Updating");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 112);

    upd_pct = addLabel(scr_update, &lv_font_montserrat_28, Theme::WARN, "0%");
    lv_obj_align(upd_pct, LV_ALIGN_CENTER, 0, 8);

    upd_bar = lv_bar_create(scr_update);
    lv_obj_set_size(upd_bar, 140, 6);
    lv_obj_align(upd_bar, LV_ALIGN_CENTER, 0, 44);
    lv_bar_set_range(upd_bar, 0, 100);
    lv_bar_set_value(upd_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(upd_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(upd_bar, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(upd_bar, c(Theme::TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(upd_bar, c(Theme::WARN), LV_PART_INDICATOR);

    lv_obj_t* warn = addLabel(scr_update, &lv_font_montserrat_12, Theme::TEXT_FAINT,
                              "Do not power off");
    lv_obj_align(warn, LV_ALIGN_CENTER, 0, 68);
}

// Page dots and the toast live on the top layer so they float above whichever
// screen is loaded. The toast is created last to stay above the dots.
void buildOverlays() {
    dots = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(dots);
    lv_obj_set_size(dots, MAX_PAGES * 14, 12);
    lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_clear_flag(dots, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots, 6, 0);
    for (int i = 0; i < MAX_PAGES; i++) {
        dot[i] = lv_obj_create(dots);
        lv_obj_remove_style_all(dot[i]);
        lv_obj_set_size(dot[i], 6, 6);
        lv_obj_set_style_radius(dot[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot[i], c(Theme::LINE), 0);
    }
    setHidden(dots, true);

    toastBox = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(toastBox);
    lv_obj_set_size(toastBox, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(toastBox, 190, 0);
    lv_obj_align(toastBox, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_clear_flag(toastBox, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(toastBox, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(toastBox, c(Theme::SURFACE_HI), 0);
    lv_obj_set_style_radius(toastBox, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(toastBox, 1, 0);
    lv_obj_set_style_border_color(toastBox, c(Theme::LINE), 0);
    lv_obj_set_style_pad_hor(toastBox, 14, 0);
    lv_obj_set_style_pad_ver(toastBox, 7, 0);
    toastLbl = addLabel(toastBox, &lv_font_montserrat_12, Theme::TEXT, "");
    setHidden(toastBox, true);
}

// -------------------------------------------------------------- refreshers ---
void setArc(int pct) {
    if (lv_arc_get_value(st_arc) == pct) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, st_arc);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_arc_set_value);
    lv_anim_set_values(&a, lv_arc_get_value(st_arc), pct);
    lv_anim_set_time(&a, 350);
    lv_anim_start(&a);
}

void refreshStatus(const PrinterStatus& s) {
    lv_color_t col = Theme::state(s.state);
    bool active = (s.state == PrintState::PRINTING || s.state == PrintState::PAUSED);

    setArc((int)(s.progress + 0.5f));
    lv_obj_set_style_arc_color(st_arc, col, LV_PART_INDICATOR);

    lv_label_set_text(st_name, cfg.printer().label().c_str());
    if (s.state == PrintState::OFFLINE) lv_label_set_text(st_pct, "--");
    else                                lv_label_set_text_fmt(st_pct, "%d%%", (int)(s.progress + 0.5f));
    lv_label_set_text(st_state, PrinterStatus::stateLabel(s.state));
    lv_obj_set_style_text_color(st_state, col, 0);

    setHidden(st_file, !s.filename.length());
    if (s.filename.length()) lv_label_set_text(st_file, s.filename.c_str());

    setHidden(st_eta_row, !active || s.remainingSec < 0);
    if (active && s.remainingSec >= 0) {
        String at = fmtFinishTime(s.remainingSec);
        String txt = fmtRemaining(s.remainingSec);
        if (at.length()) txt += "  \xC2\xB7  " + at;   // "1h 20m · 18:42"
        lv_label_set_text(st_eta, txt.c_str());
    }

    setHidden(st_temp_row, s.state == PrintState::OFFLINE);
    lv_label_set_text_fmt(st_noz, "%d\xC2\xB0", (int)(s.nozzleTemp + 0.5f));
    lv_label_set_text_fmt(st_bed, "%d\xC2\xB0", (int)(s.bedTemp + 0.5f));
}

void refreshJob(const PrinterStatus& s) {
    // Resolve the active filament from the AMS snapshot (Bambu only).
    const AmsInfo& a = s.ams;
    const AmsSlot* sl = nullptr;
    if (a.present && a.activeUnit >= 0 && a.activeUnit < 4 &&
        a.activeSlot >= 0 && a.activeSlot < 4) {
        const AmsSlot& cand = a.unit[a.activeUnit].slot[a.activeSlot];
        if (cand.present) sl = &cand;
    }

    if (sl) {
        lv_obj_set_style_bg_color(jb_swatch, lv_color_hex(sl->color), 0);
        lv_obj_set_style_border_color(jb_swatch, c(Theme::TEXT_DIM), 0);
        lv_label_set_text(jb_type, sl->type.length() ? sl->type.c_str() : "Filament");

        String info;
        if (a.units > 1) info = "AMS " + String(a.activeUnit + 1) + " \xC2\xB7 ";
        info += "Slot " + String(a.activeSlot + 1);
        if (sl->remain >= 0) info += "  \xC2\xB7  " + String(sl->remain) + "%";
        lv_label_set_text(jb_slot, info.c_str());
    } else {
        lv_obj_set_style_bg_color(jb_swatch, c(Theme::SURFACE_HI), 0);
        lv_obj_set_style_border_color(jb_swatch, c(Theme::LINE), 0);
        lv_label_set_text(jb_type, PrinterStatus::stateLabel(s.state));
        lv_label_set_text(jb_slot, a.present ? "No active filament" : "No filament data");
    }

    if (s.totalLayer > 0)
        lv_label_set_text_fmt(jb_layer, "Layer %d / %d", (int)max(s.currentLayer, (int32_t)0),
                              (int)s.totalLayer);
    else
        lv_label_set_text(jb_layer, "");
    setHidden(jb_layer, s.totalLayer <= 0);

    String at = fmtFinishTime(s.remainingSec);
    bool haveEta = (s.remainingSec >= 0) &&
                   (s.state == PrintState::PRINTING || s.state == PrintState::PAUSED);
    setHidden(jb_eta, !haveEta);
    if (haveEta) {
        String txt = at.length() ? ("Done at " + at) : (fmtRemaining(s.remainingSec) + " left");
        lv_label_set_text(jb_eta, txt.c_str());
    }

    setHidden(jb_file, !s.filename.length());
    if (s.filename.length()) lv_label_set_text(jb_file, s.filename.c_str());
}

void refreshControl(const PrinterStatus& s) {
    bool printing = s.state == PrintState::PRINTING;
    bool paused   = s.state == PrintState::PAUSED;

    lv_label_set_text(ct_state, PrinterStatus::stateLabel(s.state));
    lv_obj_set_style_text_color(ct_state, Theme::state(s.state), 0);

    // The primary button follows the job: pause a running print, resume a
    // paused one. Idle leaves it disabled rather than showing a misleading verb.
    uint32_t tint = paused ? Theme::OK : Theme::SURFACE_HI;
    lv_label_set_text(lbl_primary, paused ? LV_SYMBOL_PLAY "  Resume"
                                          : LV_SYMBOL_PAUSE "  Pause");
    lv_obj_set_style_bg_color(btn_primary, c(tint), 0);
    lv_obj_set_style_bg_color(btn_primary, c(tint), LV_STATE_PRESSED);
    setEnabled(btn_primary, printing || paused);
    setEnabled(btn_stop,    printing || paused);

    setHidden(ct_hint, printing || paused);
}

void refreshPrinterList() {
    for (uint8_t i = 0; i < ORB_MAX_PRINTERS; i++) {
        bool used = i < cfg.printerCount;
        setHidden(pr_row[i], !used);
        if (!used) continue;

        const PrinterCfg& p = cfg.printers[i];
        bool isActive = (i == cfg.activePrinter);
        bool bambu    = (p.type == PrinterType::BAMBU);

        lv_label_set_text(pr_badge[i], bambu ? "B" : "K");
        lv_obj_set_style_bg_color(pr_badge[i], bambu ? c(Theme::ACCENT) : c(Theme::NOZZLE), 0);
        lv_obj_set_style_text_color(pr_badge[i], lv_color_black(), 0);

        lv_label_set_text(pr_name[i], p.label().c_str());
        lv_obj_set_style_text_color(pr_name[i], isActive ? c(Theme::TEXT) : c(Theme::TEXT_DIM), 0);
        lv_obj_set_style_border_color(pr_row[i], isActive ? c(Theme::ACCENT) : c(Theme::SURFACE), 0);
        setHidden(pr_mark[i], !isActive);
    }
}

void refreshSystem() {
    if (WifiManager::isConnected())
        lv_label_set_text_fmt(sy_wifi, LV_SYMBOL_WIFI "  %s  %d", WiFi.SSID().c_str(), (int)WiFi.RSSI());
    else
        lv_label_set_text(sy_wifi, LV_SYMBOL_WIFI "  offline");
    lv_label_set_text(sy_ip, WifiManager::ip().c_str());
    lv_label_set_text_fmt(sy_bright, "%d%%", (int)cfg.brightness);
    lv_label_set_text_fmt(sy_ver, "v%s", Version::STRING);

    if (Updater::updateAvailable()) {
        lv_label_set_text_fmt(lbl_upd, LV_SYMBOL_DOWNLOAD "  Hold: v%s",
                              Updater::latestVersion().c_str());
        setHidden(btn_upd, false);
    } else {
        setHidden(btn_upd, true);
    }
}

void refreshIdle(const PrinterStatus& s) {
    int now = Time::localMinutes();
    setHidden(id_clock, now < 0);
    setHidden(id_orb,   now >= 0);          // clock replaces the orb once synced
    if (now >= 0) lv_label_set_text_fmt(id_clock, "%02d:%02d", now / 60, now % 60);

    lv_label_set_text(id_name, cfg.printer().label().c_str());

    const char* word;
    switch (s.state) {
        case PrintState::COMPLETE: word = "Done";    break;
        case PrintState::OFFLINE:  word = "Offline"; break;
        default:                   word = "Ready";   break;  // IDLE
    }
    lv_label_set_text(id_state, word);
    lv_obj_set_style_text_color(id_state, Theme::state(s.state), 0);

    // Temperatures make cool-down visible; meaningless without a connection.
    lv_obj_t* trow = lv_obj_get_parent(id_temps);
    setHidden(trow, s.state == PrintState::OFFLINE);
    if (s.state != PrintState::OFFLINE)
        lv_label_set_text_fmt(id_temps, "%d\xC2\xB0 / %d\xC2\xB0",
                              (int)(s.nozzleTemp + 0.5f), (int)(s.bedTemp + 0.5f));
}

void renderAms() {
    if (!scr_ams) return;
    const AmsInfo& a = g_ams;

    if (!a.present || a.units == 0) {
        for (int i = 0; i < 4; i++) setHidden(ams_tile[i], true);
        setHidden(ams_humid, true);
        setHidden(ams_dots, true);
        setHidden(btn_dry, true);
        lv_label_set_text(ams_title, "FILAMENT");
        setHidden(ams_none, false);
        return;
    }
    setHidden(ams_none, true);
    setHidden(ams_humid, false);
    if (amsUnitIdx >= a.units) amsUnitIdx = 0;
    const AmsUnit& U = a.unit[amsUnitIdx];

    if (a.units > 1) {
        if (U.isHT) lv_label_set_text(ams_title, "AMS HT");
        else        lv_label_set_text_fmt(ams_title, "AMS %d / %d", amsUnitIdx + 1, a.units);
        setHidden(ams_dots, false);
        for (int i = 0; i < 4; i++) {
            setHidden(ams_dot[i], i >= a.units);
            lv_obj_set_style_bg_color(ams_dot[i],
                i == amsUnitIdx ? c(Theme::ACCENT) : c(Theme::LINE), 0);
        }
    } else {
        lv_label_set_text(ams_title, U.isHT ? "AMS HT" : "FILAMENT");
        setHidden(ams_dots, true);
    }

    // AMS HT is single-slot: show one tile, hide the rest (flex re-centres it).
    int shown = U.isHT ? 1 : 4;
    for (int i = 0; i < 4; i++) {
        setHidden(ams_tile[i], i >= shown);
        if (i >= shown) continue;
        const AmsSlot& sl = U.slot[i];
        bool used = (i < U.count) && sl.present;
        uint32_t col = used ? sl.color : Theme::SURFACE_HI;
        lv_obj_set_style_bg_color(ams_tile[i], lv_color_hex(col), 0);

        lv_color_t txt = used ? Theme::textOn(col) : c(Theme::TEXT_DIM);
        lv_obj_set_style_text_color(ams_type[i], txt, 0);
        lv_obj_set_style_text_color(ams_remain[i], txt, 0);

        lv_label_set_text(ams_type[i], used ? sl.type.c_str() : "empty");
        if (used && sl.remain >= 0) lv_label_set_text_fmt(ams_remain[i], "%d%%", sl.remain);
        else                        lv_label_set_text(ams_remain[i], "");

        bool act = (g_ams.activeUnit == amsUnitIdx && g_ams.activeSlot == i);
        lv_obj_set_style_border_color(ams_tile[i], act ? c(Theme::ACCENT) : lv_color_hex(col), 0);
        lv_obj_set_style_border_width(ams_tile[i], act ? 3 : 2, 0);
    }

    // Humidity / temperature line: prefer the actual RH% over the 1..5 level.
    char hum[48];
    int n = 0;
    if (U.humidityPct >= 0)   n += snprintf(hum + n, sizeof(hum) - n, "RH %d%%", U.humidityPct);
    else if (U.humidity >= 0) n += snprintf(hum + n, sizeof(hum) - n, "Humidity %d/5", U.humidity);
    if (U.tempC > -99.0f)     n += snprintf(hum + n, sizeof(hum) - n, "%s%d\xC2\xB0",
                                            n ? "   " : "", (int)(U.tempC + 0.5f));
    lv_label_set_text(ams_humid, hum);

    // Drying toggle — only meaningful on an AMS HT.
    setHidden(btn_dry, !U.isHT);
    if (U.isHT) {
        bool loaded = (U.count > 0) && U.slot[0].present;
        if (U.drying) {
            if (U.dryRemainMin > 0)
                lv_label_set_text_fmt(lbl_dry, LV_SYMBOL_STOP " Hold: stop (%ldm)", (long)U.dryRemainMin);
            else
                lv_label_set_text(lbl_dry, LV_SYMBOL_STOP " Hold to stop");
            lv_obj_set_style_bg_color(btn_dry, c(Theme::DANGER), 0);
            setEnabled(btn_dry, true);
        } else {
            lv_label_set_text(lbl_dry, LV_SYMBOL_CHARGE " Hold to dry");
            lv_obj_set_style_bg_color(btn_dry, c(Theme::WARN), 0);
            setEnabled(btn_dry, loaded);   // nothing to dry when empty
        }
    }
}

}  // namespace

namespace UI {

void begin() {
    // Give destructive actions a deliberate hold. LVGL's 400 ms default is easy
    // to trigger by accident on a 240 px touch panel.
    for (lv_indev_t* d = lv_indev_get_next(NULL); d; d = lv_indev_get_next(d))
        d->driver->long_press_time = Theme::HOLD_MS;

    buildStatusScreen();
    buildJobScreen();
    if (cfg.anyBambu())        buildAmsScreen();
    buildControlScreen();
    if (cfg.printerCount > 1)  buildPrintersScreen();
    buildSystemScreen();
    buildIdleScreen();
    buildSetupScreen();
    buildBootScreen();
    buildUpdateScreen();
    buildOverlays();

    if (scr_printers) refreshPrinterList();
    rebuildCarousel();
    showBoot("Starting", 0);
}

void setControlHandler(ControlCb cb)       { g_ctrl   = cb; }
void setPrinterSwitchHandler(PrinterSwitchCb cb) { g_switch = cb; }

void toast(const char* text) { showToast(text); }

bool isResting(PrintState s) {
    return s == PrintState::IDLE || s == PrintState::OFFLINE || s == PrintState::COMPLETE;
}

void refreshPrinters() {
    if (scr_printers) refreshPrinterList();
    rebuildCarousel();
    g_rebaseline = true;
}

void update(const PrinterStatus& s) {
    g_lastState = s.state;

    // Keep every page current (cheap) so any of them is ready when swiped to.
    refreshStatus(s);
    refreshJob(s);
    if (scr_ams) { g_ams = s.ams; if (amsUnitIdx >= s.ams.units) amsUnitIdx = 0; renderAms(); }
    refreshControl(s);
    refreshSystem();
    refreshIdle(s);

    bool resting = isResting(s.state);
    if (!g_uiStarted) {
        g_uiStarted  = true;
        g_wasResting = resting;
        if (resting) showIdle();
        else         enterCarousel();
        return;
    }

    // Adopt the new printer's state without moving the user off the page they
    // are looking at; the next real transition takes over from there.
    if (g_rebaseline) {
        g_rebaseline = false;
        g_wasResting = resting;
        return;
    }

    // Switch context only on an active<->resting transition, so the user can
    // freely swipe into the carousel while idle without being yanked back.
    if (resting != g_wasResting) {
        g_wasResting = resting;
        if (resting) showIdle();
        else         enterCarousel();
    }
}

void showSetup(const String& ssid, const String& ip) {
    setHidden(dots, true);
    if (lv_scr_act() != scr_setup) lv_scr_load(scr_setup);
    String body = "Join WiFi\n#22d3ee " + ssid + "#\n\nThen open\n#f2f6fa " + ip + "#";
    lv_label_set_text(setup_body, body.c_str());
}

void showBoot(const char* step, uint8_t pct, const char* detail) {
    if (dots) setHidden(dots, true);
    if (lv_scr_act() != scr_boot) lv_scr_load(scr_boot);
    lv_bar_set_value(boot_bar, pct, LV_ANIM_ON);
    lv_label_set_text(boot_step, step);
    lv_label_set_text(boot_detail, detail ? detail : "");
}

void showUpdate(uint8_t pct) {
    if (dots) setHidden(dots, true);
    if (lv_scr_act() != scr_update) lv_scr_load(scr_update);
    lv_bar_set_value(upd_bar, pct, LV_ANIM_OFF);
    lv_label_set_text_fmt(upd_pct, "%d%%", pct);
}

}  // namespace UI
