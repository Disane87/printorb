/**
 * @file lgfx_device.h
 * LovyanGFX device definition for the Waveshare ESP32-S3-Touch-LCD-1.28.
 *   - Display : GC9A01, 240x240 round, SPI
 *   - Touch   : CST816S, I2C
 *
 * ⚠️ PIN VERIFICATION: These pins match the common Waveshare ESP32-S3-Touch-
 *    LCD-1.28 revision. If your screen stays dark or touch is dead, double-
 *    check the pin defines below against your board's wiki page.
 */
#pragma once

#ifndef LGFX_USE_V1
#define LGFX_USE_V1
#endif
#include <LovyanGFX.hpp>

// ---- LCD (GC9A01, SPI) -----------------------------------------------------
#define ORB_PIN_LCD_SCLK 10
#define ORB_PIN_LCD_MOSI 11
#define ORB_PIN_LCD_MISO 12  // unused by GC9A01 (write-only); set -1 if conflict
#define ORB_PIN_LCD_DC    8
#define ORB_PIN_LCD_CS    9
#define ORB_PIN_LCD_RST  14
#define ORB_PIN_LCD_BL   40  // backlight (verify; some revisions differ)

// ---- Touch (CST816S, I2C) --------------------------------------------------
#define ORB_PIN_TP_SDA    6
#define ORB_PIN_TP_SCL    7
#define ORB_PIN_TP_INT    5
#define ORB_PIN_TP_RST   13

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_GC9A01    _panel;
    lgfx::Bus_SPI         _bus;
    lgfx::Light_PWM       _light;
    lgfx::Touch_CST816S   _touch;

public:
    LGFX() {
        // ---- SPI bus ----
        {
            auto cfg = _bus.config();
            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = 80000000;
            cfg.freq_read   = 20000000;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = ORB_PIN_LCD_SCLK;
            cfg.pin_mosi    = ORB_PIN_LCD_MOSI;
            cfg.pin_miso    = ORB_PIN_LCD_MISO;
            cfg.pin_dc      = ORB_PIN_LCD_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }

        // ---- Panel ----
        {
            auto cfg = _panel.config();
            cfg.pin_cs          = ORB_PIN_LCD_CS;
            cfg.pin_rst         = ORB_PIN_LCD_RST;
            cfg.pin_busy        = -1;
            cfg.panel_width     = 240;
            cfg.panel_height    = 240;
            cfg.offset_x        = 0;
            cfg.offset_y        = 0;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable        = false;
            cfg.invert          = true;   // GC9A01 needs inversion
            cfg.rgb_order       = false;  // BGR
            cfg.dlen_16bit      = false;
            cfg.bus_shared      = false;
            _panel.config(cfg);
        }

        // ---- Backlight ----
        {
            auto cfg = _light.config();
            cfg.pin_bl      = ORB_PIN_LCD_BL;
            cfg.invert      = false;
            cfg.freq        = 44100;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }

        // ---- Touch ----
        {
            auto cfg = _touch.config();
            cfg.x_min      = 0;
            cfg.x_max      = 239;
            cfg.y_min      = 0;
            cfg.y_max      = 239;
            cfg.pin_int    = ORB_PIN_TP_INT;
            cfg.pin_rst    = ORB_PIN_TP_RST;
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;
            cfg.i2c_port   = 0;
            cfg.i2c_addr   = 0x15;
            cfg.pin_sda    = ORB_PIN_TP_SDA;
            cfg.pin_scl    = ORB_PIN_TP_SCL;
            cfg.freq       = 400000;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }

        setPanel(&_panel);
    }
};
