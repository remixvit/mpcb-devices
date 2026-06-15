#pragma once
#ifdef MPCB_USE_DISPLAY
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LovyanGFX.hpp>
#include <Preferences.h>
#include <map>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <vector>

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

struct TouchCmd {
    char topic[128];
    char payload[256];
    char peripheral[64];  // peripheral key for rules engine (may be empty)
};

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
    bool needsRedraw() const { return _needsRedraw; }

    // Touch (XPT2046) — poll on Core 0, read queue on Core 1
    void pollTouch();
    bool getTouchCmd(TouchCmd& cmd);

    // Calibration
    void requestCalibrate();
    bool isCalibrating() const { return _calibrating; }

    // Thread-safe state cache for value/gauge widgets
    void updateStateValue(const String& dotKey, const String& value);
    void clearState();
    String getStateValue(const String& dotKey) const;

    // LED widget override (rules engine target: disp_<key>)
    void setWidgetState(const String& key, const String& action);
    bool getLedState(const String& key) const;

private:
    static uint16_t _hexToRgb565(const char* hex);
    static bool _isTransparent(const char* hex);
    void _drawValue (lgfx::LGFXBase* tgt, int x, int y, int w, int h,
                     const String& label, const String& source, const String& format,
                     int fontSize, uint16_t color, uint16_t bg);
    void _drawLabel (lgfx::LGFXBase* tgt, int x, int y, int w, int h,
                     const String& text, int fontSize, uint16_t color, uint16_t bg);
    void _drawButton(lgfx::LGFXBase* tgt, int x, int y, int w, int h,
                     const String& label, int fontSize, uint16_t color, uint16_t bg);
    void _drawGauge (lgfx::LGFXBase* tgt, int x, int y, int w, int h,
                     const String& label, const String& source,
                     float minV, float maxV, int fontSize, uint16_t color, uint16_t bg);
    void _drawRect  (lgfx::LGFXBase* tgt, int x, int y, int w, int h,
                     uint16_t color, uint16_t bg, bool filled, int radius, int thickness);
    void _drawLine  (lgfx::LGFXBase* tgt, int x, int y, int w, int h,
                     uint16_t color, int thickness);
    void _drawLed   (lgfx::LGFXBase* tgt, int x, int y, int w, int h,
                     const String& source, uint16_t colorOn, uint16_t colorOff,
                     const String& key = "");
    float _textScale(int fontSize) const;

    void _renderToSprite();
    void _renderDirty();
    void _renderWidgets(lgfx::LGFXBase* tgt);
    uint32_t _widgetHash(const JsonObject& w);

    // LovyanGFX objects — must persist (device holds references)
    lgfx::Bus_SPI         _bus;
    lgfx::Panel_ILI9341   _panel;
    lgfx::LGFX_Device*    _lcd    = nullptr;
    lgfx::LGFX_Sprite*    _sprite = nullptr;
    bool _useSpriteRender = false;

    // Dirty tracking (no-PSRAM fallback)
    struct WidgetCache {
        String   id;
        int      x, y, w, h, z;
        uint32_t contentHash;
    };
    std::vector<WidgetCache> _widgetCache;

    int  _pinMosi               = -1;
    int  _pinClk                = -1;
    int  _pinCs                 = -1;
    int  _pinDc                 = -1;
    int  _pinRst                = -1;
    int  _pinLed                = -1;
    int  _pinMiso               = -1;
    int  _pinTouchCs            = -1;
    int  _pinTouchIrq           = -1;
    bool _ready                 = false;
    bool _needsRedraw           = false;
    String _widgetsJson;        // cached JSON string

    // Thread-safe state cache for value/gauge widget data
    mutable SemaphoreHandle_t _stateMutex = nullptr;
    std::map<String, String>  _stateCache;

    // LED override state (from rules engine, key = sanitized widget label, max 8 LEDs)
    struct LedOverride { char key[32]; bool state; };
    LedOverride _ledOverrides[8] = {};
    uint8_t     _ledOverrideCount = 0;

    // Touch (XPT2046) button hit tracking
    QueueHandle_t _touchQueue = nullptr;
    uint32_t _lastTapMs = 0;

    // Calibration (request from Core 1, execute on Core 0 via pollTouch)
    QueueHandle_t _calibrateQueue = nullptr;
    bool _calibrating = false;

    struct ButtonHit { int x, y, w, h; char topic[128]; char payload[256]; char peripheral[64]; };
    std::vector<ButtonHit> _buttons;

    void _initTouch();
};
#endif // MPCB_USE_DISPLAY
