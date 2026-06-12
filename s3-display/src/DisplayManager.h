#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LovyanGFX.hpp>
#include <Preferences.h>

// ─── ESP32-S3-Zero ILI9341 Display Manager ────────────────────────────────────
//
// Runtime pin configuration from NVS (keys with defaults):
//   disp_mosi=13  disp_clk=12  disp_cs=11  disp_dc=10
//   disp_rst=9    disp_led=7   disp_miso=8
//
// Widgets stored in NVS under "display" namespace, key "widgets".
// Receive updates via MQTT topic: mpcb/devices/{id}/display/set
//
// Supported widget types: value, label, button, gauge
// ───────────────────────────────────────────────────────────────────────────────

class DisplayManager {
public:
    DisplayManager();
    ~DisplayManager();

    // Initialize display with runtime pins from NVS.
    // Returns true on success.
    bool begin();

    // Set backlight brightness (0–255).
    void setBrightness(uint8_t level);

    // Full redraw from internal widget cache.
    void render();

    // Save widgets JSON to NVS and trigger render.
    void saveAndRender(const String& widgetsJson);

    // Load widgets from NVS and render (call after begin()).
    void loadAndRender();

    // Clear screen (fill black).
    void clear();

    bool isReady() const { return _ready; }

private:
    static uint16_t _hexToRgb565(const char* hex);
    void _drawValue(int x, int y, int w, int h, const String& label,
                    const String& source, const String& format,
                    int fontSize, uint16_t color, uint16_t bg);
    void _drawLabel(int x, int y, int w, int h, const String& text,
                    int fontSize, uint16_t color, uint16_t bg);
    void _drawButton(int x, int y, int w, int h, const String& label,
                     int fontSize, uint16_t color, uint16_t bg);
    void _drawGauge(int x, int y, int w, int h, const String& label,
                    const String& source,
                    int fontSize, uint16_t color, uint16_t bg);
    float _textScale(int fontSize) const;

    // LovyanGFX objects — must persist (device holds references)
    lgfx::Bus_SPI         _bus;
    lgfx::Panel_ILI9341   _panel;
    lgfx::LGFX_Device*    _lcd = nullptr;

    int  _pinMosi               = 13;
    int  _pinClk                = 12;
    int  _pinCs                 = 11;
    int  _pinDc                 = 10;
    int  _pinRst                = 9;
    int  _pinLed                = 7;
    int  _pinMiso               = 8;
    bool _ready                 = false;
    bool _needsRedraw           = false;
    String _widgetsJson;        // cached JSON string
};
