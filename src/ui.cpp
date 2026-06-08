#include "ui.h"
#include <lvgl.h>

namespace {

lv_obj_t* scr_status   = nullptr;
lv_obj_t* scr_setup    = nullptr;
lv_obj_t* scr_message  = nullptr;

// status screen widgets
lv_obj_t* arc_progress = nullptr;
lv_obj_t* lbl_percent  = nullptr;
lv_obj_t* lbl_state    = nullptr;
lv_obj_t* lbl_printer  = nullptr;
lv_obj_t* lbl_file     = nullptr;
lv_obj_t* lbl_nozzle   = nullptr;
lv_obj_t* lbl_bed      = nullptr;
lv_obj_t* lbl_bottom   = nullptr;

// message / setup screen widgets
lv_obj_t* msg_title    = nullptr;
lv_obj_t* msg_sub      = nullptr;
lv_obj_t* setup_title  = nullptr;
lv_obj_t* setup_body   = nullptr;

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

void buildStatusScreen() {
    scr_status = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_status, lv_color_black(), 0);
    lv_obj_clear_flag(scr_status, LV_OBJ_FLAG_SCROLLABLE);

    // Progress arc (full ring)
    arc_progress = lv_arc_create(scr_status);
    lv_obj_set_size(arc_progress, 226, 226);
    lv_obj_center(arc_progress);
    lv_arc_set_rotation(arc_progress, 135);
    lv_arc_set_bg_angles(arc_progress, 0, 270);
    lv_arc_set_range(arc_progress, 0, 100);
    lv_arc_set_value(arc_progress, 0);
    lv_obj_remove_style(arc_progress, NULL, LV_PART_KNOB);   // no knob
    lv_obj_clear_flag(arc_progress, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc_progress, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_progress, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_progress, lv_color_hex(0x202830), LV_PART_MAIN);

    // Printer name (top)
    lbl_printer = lv_label_create(scr_status);
    lv_obj_set_style_text_color(lbl_printer, lv_color_hex(0x9aa4ad), 0);
    lv_obj_set_style_text_font(lbl_printer, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl_printer, "Printer");
    lv_obj_align(lbl_printer, LV_ALIGN_TOP_MID, 0, 36);

    // Filename (scrolls if long), under printer name
    lbl_file = lv_label_create(scr_status);
    lv_obj_set_style_text_color(lbl_file, lv_color_hex(0x6b7480), 0);
    lv_obj_set_style_text_font(lbl_file, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(lbl_file, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_file, 150);
    lv_obj_set_style_text_align(lbl_file, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(lbl_file, "");
    lv_obj_align(lbl_file, LV_ALIGN_TOP_MID, 0, 54);

    // Big percent (center)
    lbl_percent = lv_label_create(scr_status);
    lv_obj_set_style_text_color(lbl_percent, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_percent, &lv_font_montserrat_40, 0);
    lv_label_set_text(lbl_percent, "--");
    lv_obj_align(lbl_percent, LV_ALIGN_CENTER, 0, -6);

    // State label (under percent)
    lbl_state = lv_label_create(scr_status);
    lv_obj_set_style_text_font(lbl_state, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_state, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_label_set_text(lbl_state, "Offline");
    lv_obj_align(lbl_state, LV_ALIGN_CENTER, 0, 28);

    // Temps row
    lbl_nozzle = lv_label_create(scr_status);
    lv_obj_set_style_text_color(lbl_nozzle, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_obj_set_style_text_font(lbl_nozzle, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl_nozzle, "N --");
    lv_obj_align(lbl_nozzle, LV_ALIGN_CENTER, -42, 58);

    lbl_bed = lv_label_create(scr_status);
    lv_obj_set_style_text_color(lbl_bed, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_text_font(lbl_bed, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl_bed, "B --");
    lv_obj_align(lbl_bed, LV_ALIGN_CENTER, 42, 58);

    // Bottom line: remaining time / layers
    lbl_bottom = lv_label_create(scr_status);
    lv_obj_set_style_text_color(lbl_bottom, lv_color_hex(0x9aa4ad), 0);
    lv_obj_set_style_text_font(lbl_bottom, &lv_font_montserrat_12, 0);
    lv_label_set_text(lbl_bottom, "");
    lv_obj_align(lbl_bottom, LV_ALIGN_BOTTOM_MID, 0, -34);
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

void buildMessageScreen() {
    scr_message = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_message, lv_color_black(), 0);
    lv_obj_clear_flag(scr_message, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* spinner = lv_spinner_create(scr_message, 1200, 70);
    lv_obj_set_size(spinner, 70, 70);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -40);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x202830), LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, lv_palette_main(LV_PALETTE_CYAN), LV_PART_INDICATOR);

    msg_title = lv_label_create(scr_message);
    lv_obj_set_style_text_color(msg_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(msg_title, &lv_font_montserrat_20, 0);
    lv_label_set_text(msg_title, "PrintOrb");
    lv_obj_align(msg_title, LV_ALIGN_CENTER, 0, 30);

    msg_sub = lv_label_create(scr_message);
    lv_obj_set_style_text_color(msg_sub, lv_color_hex(0x9aa4ad), 0);
    lv_obj_set_style_text_font(msg_sub, &lv_font_montserrat_14, 0);
    lv_label_set_text(msg_sub, "");
    lv_obj_align(msg_sub, LV_ALIGN_CENTER, 0, 58);
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

}  // namespace

namespace UI {

void begin() {
    buildStatusScreen();
    buildSetupScreen();
    buildMessageScreen();
    showMessage("PrintOrb", "starting...");
}

void showStatus(const PrinterStatus& s, const String& printerLabel) {
    if (lv_scr_act() != scr_status) lv_scr_load(scr_status);

    lv_color_t col = stateColor(s.state);
    lv_arc_set_value(arc_progress, (int)(s.progress + 0.5f));
    lv_obj_set_style_arc_color(arc_progress, col, LV_PART_INDICATOR);

    lv_label_set_text(lbl_printer, printerLabel.c_str());

    if (s.state == PrintState::OFFLINE) {
        lv_label_set_text(lbl_percent, "--");
    } else {
        lv_label_set_text_fmt(lbl_percent, "%d%%", (int)(s.progress + 0.5f));
    }

    lv_label_set_text(lbl_state, PrinterStatus::stateLabel(s.state));
    lv_obj_set_style_text_color(lbl_state, col, 0);

    if (s.filename.length())
        lv_label_set_text(lbl_file, s.filename.c_str());
    else
        lv_label_set_text(lbl_file, "");

    lv_label_set_text_fmt(lbl_nozzle, "N %d\xC2\xB0", (int)(s.nozzleTemp + 0.5f));
    lv_label_set_text_fmt(lbl_bed,    "B %d\xC2\xB0", (int)(s.bedTemp + 0.5f));

    // Bottom line: time remaining + layer info when available
    String bottom;
    if (s.state == PrintState::PRINTING || s.state == PrintState::PAUSED) {
        bottom = LV_SYMBOL_REFRESH " " + fmtRemaining(s.remainingSec);
        if (s.totalLayer > 0)
            bottom += "   " + String(max(s.currentLayer, 0)) + "/" + String(s.totalLayer);
    } else if (s.state == PrintState::ERROR && s.errorMsg.length()) {
        bottom = s.errorMsg;
    }
    lv_label_set_text(lbl_bottom, bottom.c_str());
}

void showSetup(const String& ssid, const String& ip) {
    if (lv_scr_act() != scr_setup) lv_scr_load(scr_setup);
    String body = "Connect to WiFi:\n#00e5ff " + ssid + "#\n\nThen open:\n#ffffff " + ip + "#";
    lv_label_set_recolor(setup_body, true);
    lv_label_set_text(setup_body, body.c_str());
}

void showMessage(const String& title, const String& subtitle) {
    if (lv_scr_act() != scr_message) lv_scr_load(scr_message);
    lv_label_set_text(msg_title, title.c_str());
    lv_label_set_text(msg_sub, subtitle.c_str());
}

}  // namespace UI
