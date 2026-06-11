#include "display.h"
#include "lgfx_device.h"
#include "logbuf.h"
#include <lvgl.h>

static LGFX lcd;

static const uint16_t SCREEN_W = 240;
static const uint16_t SCREEN_H = 240;

// Two partial draw buffers (~1/6 screen each) in internal RAM.
static const uint32_t BUF_LINES = 40;
static lv_color_t buf1[SCREEN_W * BUF_LINES];
static lv_color_t buf2[SCREEN_W * BUF_LINES];
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t  disp_drv;
static lv_indev_drv_t indev_drv;

static void disp_flush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    lcd.startWrite();
    lcd.setAddrWindow(area->x1, area->y1, w, h);
    lcd.writePixels((lgfx::rgb565_t*)color_p, w * h);
    lcd.endWrite();
    lv_disp_flush_ready(drv);
}

// When set, the indev reports "released" until the finger lifts. Lets the sleep
// manager swallow the wake-up tap so it doesn't trigger a button or swipe.
static volatile bool s_suppress = false;

static void touch_read(lv_indev_drv_t* /*drv*/, lv_indev_data_t* data) {
    int32_t x, y;
    bool down = lcd.getTouch(&x, &y);

    if (s_suppress) {
        if (!down) s_suppress = false;   // finger lifted -> resume normal input
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if (down) {
        data->state   = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
        // Throttled debug: confirms the CST816S reports coordinates.
        static uint32_t lastLog = 0;
        if (millis() - lastLog > 200) {
            lastLog = millis();
            Log::printf("[Touch] x=%ld y=%ld\n", (long)x, (long)y);
        }
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

namespace Display {

void begin() {
    Log::printf("[Display] init (BL=GPIO%d)...\n", ORB_PIN_LCD_BL);
    bool ok = lcd.init();
    Log::printf("[Display] panel init %s\n", ok ? "ok" : "FAILED");
    lcd.setRotation(0);
    lcd.setBrightness(255);
    lcd.fillScreen(TFT_BLACK);

    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, SCREEN_W * BUF_LINES);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = SCREEN_W;
    disp_drv.ver_res  = SCREEN_H;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read;
    lv_indev_drv_register(&indev_drv);
}

void setBrightness(uint8_t pct) {
    pct = pct > 100 ? 100 : pct;
    lcd.setBrightness(map(pct, 0, 100, 0, 255));
}

bool touched() {
    int32_t x, y;
    return lcd.getTouch(&x, &y);
}

void suppressTouchUntilRelease() { s_suppress = true; }

}  // namespace Display
