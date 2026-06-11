#include "DisplayManager.h"

DisplayManager::DisplayManager() {}
DisplayManager::~DisplayManager() {
    delete _lcd;
}

bool DisplayManager::begin() {
    Preferences prefs;
    prefs.begin("display", true);
    _pinMosi  = prefs.getInt("disp_mosi", 11);
    _pinClk   = prefs.getInt("disp_clk",  12);
    _pinCs    = prefs.getInt("disp_cs",   10);
    _pinDc    = prefs.getInt("disp_dc",   9);
    _pinRst   = prefs.getInt("disp_rst",  8);
    _pinLed   = prefs.getInt("disp_led",  7);
    _pinMiso  = prefs.getInt("disp_miso", -1);
    prefs.end();

    _lcd = new lgfx::LGFX_Device();
    if (!_lcd) return false;

    // SPI bus
    auto busCfg = _bus.config();
    busCfg.pin_mosi   = _pinMosi;
    busCfg.pin_sclk   = _pinClk;
    busCfg.pin_miso   = _pinMiso;
    busCfg.spi_mode   = 0;
    busCfg.freq_write = 40000000;
    busCfg.freq_read  = 16000000;
    busCfg.spi_3wire  = (_pinMiso < 0);
    busCfg.pin_dc = _pinDc;
    _bus.config(busCfg);
    _panel.setBus(&_bus);

    // ILI9341 panel
    auto panelCfg = _panel.config();
    panelCfg.pin_cs       = _pinCs;
    panelCfg.pin_rst      = _pinRst;
    panelCfg.pin_busy     = -1;
    panelCfg.memory_width  = 240;
    panelCfg.memory_height = 320;
    panelCfg.panel_width   = 240;
    panelCfg.panel_height  = 320;
    panelCfg.offset_rotation = 0;
    panelCfg.readable     = false;
    panelCfg.invert       = false;
    panelCfg.rgb_order    = false;
    panelCfg.dlen_16bit   = false;
    panelCfg.bus_shared   = false;
    _panel.config(panelCfg);

    _lcd->setPanel(&_panel);
    _lcd->begin();
    _lcd->setRotation(3);
    _lcd->setColorDepth(16);

    // Backlight — ESP32 Arduino v3 API
    if (_pinLed >= 0) {
        ledcAttach(_pinLed, 5000, 8);
        ledcWrite(_pinLed, 180);
    }

    _ready = true;
    clear();
    return true;
}

void DisplayManager::setBrightness(uint8_t level) {
    if (_pinLed >= 0) ledcWrite(_pinLed, level);
}

void DisplayManager::clear() {
    if (!_lcd) return;
    _lcd->fillScreen(TFT_BLACK);
}

void DisplayManager::saveAndRender(const String& widgetsJson) {
    _widgetsJson = widgetsJson;
    Preferences prefs;
    prefs.begin("display", false);
    prefs.putString("widgets", _widgetsJson);
    prefs.end();
    _needsRedraw = true;
}

void DisplayManager::loadAndRender() {
    Preferences prefs;
    prefs.begin("display", true);
    _widgetsJson = prefs.getString("widgets", "");
    prefs.end();
    if (_widgetsJson.length() > 0) {
        _needsRedraw = true;
        render();
    }
}

void DisplayManager::render() {
    if (!_ready || !_lcd) return;
    _needsRedraw = false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, _widgetsJson);
    if (err) {
        _lcd->setCursor(10, 10);
        _lcd->setTextColor(TFT_RED, TFT_BLACK);
        _lcd->setTextSize(1);
        _lcd->print("JSON error: ");
        _lcd->println(err.c_str());
        return;
    }

    JsonArray widgets = doc.as<JsonArray>();
    if (widgets.size() == 0) return;

    _lcd->fillScreen(TFT_BLACK);

    for (JsonVariant v : widgets) {
        JsonObject w = v.as<JsonObject>();
        if (w.isNull()) continue;

        const char* type = w["type"] | "";
        int x = w["x"] | 0;
        int y = w["y"] | 0;
        int ww = w["w"] | 100;
        int hh = w["h"] | 28;
        String label = w["label"] | "";
        int fontSize = w["fontSize"] | 14;
        uint16_t color = _hexToRgb565(w["color"] | "#ffffff");
        uint16_t bg    = _hexToRgb565(w["bgColor"] | "#000000");

        if (strcmp(type, "value") == 0) {
            _drawValue(x, y, ww, hh, label, w["source"] | "", w["format"] | "{v}", fontSize, color, bg);
        } else if (strcmp(type, "label") == 0) {
            _drawLabel(x, y, ww, hh, label, fontSize, color, bg);
        } else if (strcmp(type, "button") == 0) {
            _drawButton(x, y, ww, hh, label, fontSize, color, bg);
        } else if (strcmp(type, "gauge") == 0) {
            _drawGauge(x, y, ww, hh, label, w["source"] | "", fontSize, color, bg);
        }
    }
}

uint16_t DisplayManager::_hexToRgb565(const char* hex) {
    if (!hex || strlen(hex) < 7) return TFT_WHITE;
    uint8_t r = strtol(String(hex).substring(1, 3).c_str(), nullptr, 16);
    uint8_t g = strtol(String(hex).substring(3, 5).c_str(), nullptr, 16);
    uint8_t b = strtol(String(hex).substring(5, 7).c_str(), nullptr, 16);
    // color565 is a static-style function in LovyanGFX
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

float DisplayManager::_textScale(int fontSize) const {
    if (fontSize <= 8)  return 1.0f;
    if (fontSize <= 12) return 1.5f;
    if (fontSize <= 16) return 2.0f;
    if (fontSize <= 20) return 2.5f;
    return 3.0f;
}

void DisplayManager::_drawValue(int x, int y, int w, int h,
                                const String& label, const String& source,
                                const String& format,
                                int fontSize, uint16_t color, uint16_t bg) {
    if (!_lcd) return;
    if (bg != TFT_BLACK) _lcd->fillRect(x, y, w, h, bg);

    if (label.length() > 0) {
        _lcd->setTextSize(1.0f);
        _lcd->setTextColor(color, bg);
        _lcd->setCursor(x + 4, y + 2);
        _lcd->print(label);
    }

    float scale = _textScale(fontSize);
    _lcd->setTextSize(scale);
    _lcd->setTextColor(color, bg);
    int valY = label.length() > 0 ? y + h / 2 : y + (h - (int)(8 * scale)) / 2;
    _lcd->setCursor(x + 4, valY);
    String disp = source.length() > 0 ? String(format).replace("{v}", "...") : label;
    _lcd->println(disp);
}

void DisplayManager::_drawLabel(int x, int y, int w, int h,
                                const String& text,
                                int fontSize, uint16_t color, uint16_t bg) {
    if (!_lcd) return;
    if (bg != TFT_BLACK) _lcd->fillRect(x, y, w, h, bg);
    float scale = _textScale(fontSize);
    _lcd->setTextSize(scale);
    _lcd->setTextColor(color, bg);
    _lcd->setCursor(x + 4, y + (h - (int)(8 * scale)) / 2);
    _lcd->print(text);
}

void DisplayManager::_drawButton(int x, int y, int w, int h,
                                 const String& label,
                                 int fontSize, uint16_t color, uint16_t bg) {
    if (!_lcd) return;
    _lcd->fillRoundRect(x, y, w, h, 4, bg);
    _lcd->drawRoundRect(x, y, w, h, 4, color);
    float scale = _textScale(fontSize);
    _lcd->setTextSize(scale);
    _lcd->setTextColor(color, bg);
    int16_t tw = label.length() * 6 * scale;
    int tx = x + (w - tw) / 2;
    if (tx < x + 4) tx = x + 4;
    _lcd->setCursor(tx, y + (h - (int)(8 * scale)) / 2);
    _lcd->print(label);
}

void DisplayManager::_drawGauge(int x, int y, int w, int h,
                                const String& label, const String& source,
                                int fontSize, uint16_t color, uint16_t bg) {
    if (!_lcd) return;
    _lcd->fillRect(x, y, w, h, bg);

    int barH = h - 4;
    int barY = y + 2;
    if (label.length() > 0) {
        _lcd->setTextSize(1.0f);
        _lcd->setTextColor(color, bg);
        _lcd->setCursor(x + 4, y + 2);
        _lcd->print(label);
        barH = h - 16;
        barY = y + 14;
    }

    uint16_t dark = (uint16_t)(color >> 2) & 0x39E7;
    _lcd->fillRoundRect(x + 2, barY, w - 4, barH, 2, dark);
    int fillW = ((w - 4) * 50) / 100;
    _lcd->fillRoundRect(x + 2, barY, fillW, barH, 2, color);
}
