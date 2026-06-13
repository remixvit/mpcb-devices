#include "DisplayManager.h"
#ifdef MPCB_USE_DISPLAY

DisplayManager::DisplayManager() {}
DisplayManager::~DisplayManager() {
    delete _lcd;
}

bool DisplayManager::begin() {
    Preferences prefs;
    prefs.begin("display", true);
    _pinMosi  = prefs.getInt("disp_mosi", -1);
    _pinClk   = prefs.getInt("disp_clk",  -1);
    _pinCs    = prefs.getInt("disp_cs",   -1);
    _pinDc    = prefs.getInt("disp_dc",   -1);
    _pinRst   = prefs.getInt("disp_rst",  -1);
    _pinLed   = prefs.getInt("disp_led",  -1);
    _pinMiso     = prefs.getInt("disp_miso", -1);
    _pinTouchCs  = prefs.getInt("disp_t_cs",  -1);
    _pinTouchIrq = prefs.getInt("disp_t_irq", -1);
    prefs.end();

    Serial.printf("[display] pins: mosi=%d clk=%d cs=%d dc=%d rst=%d led=%d miso=%d\n",
                  _pinMosi, _pinClk, _pinCs, _pinDc, _pinRst, _pinLed, _pinMiso);
    if (_pinMosi < 0 || _pinClk < 0 || _pinCs < 0 || _pinDc < 0) {
        Serial.println("[display] pins not configured — skipping init");
        return false;
    }
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
    busCfg.spi_3wire   = (_pinMiso < 0);
    busCfg.pin_dc      = _pinDc;
    busCfg.dma_channel = 0;  // polling SPI — avoids GDMA conflict with WiFi on Core 0
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

    // XPT2046 touch
    if (_pinTouchCs >= 0) {
        static lgfx::Touch_XPT2046 touch;
        auto touchCfg = touch.config();
        touchCfg.x_min      = 300;
        touchCfg.x_max      = 3800;
        touchCfg.y_min      = 300;
        touchCfg.y_max      = 3800;
        touchCfg.pin_int    = _pinTouchIrq;
        touchCfg.bus_shared = true;
        touchCfg.offset_rotation = 0;
        touchCfg.spi_host   = SPI3_HOST;
        touchCfg.freq       = 1000000;
        touchCfg.pin_sclk   = _pinClk;
        touchCfg.pin_mosi   = _pinMosi;
        touchCfg.pin_miso   = _pinMiso;
        touchCfg.pin_cs     = _pinTouchCs;
        touch.config(touchCfg);
        _panel.setTouch(&touch);
    }

    _lcd->begin();
    _lcd->setRotation(3);
    _lcd->setColorDepth(16);

    // Backlight — ESP32 Arduino v3 API
    if (_pinLed >= 0) {
        ledcAttach(_pinLed, 5000, 8);
        ledcWrite(_pinLed, 180);
    }

    _ready = true;
    _stateMutex = xSemaphoreCreateMutex();
    _touchQueue = xQueueCreate(4, sizeof(TouchCmd));
    _calibrateQueue = xQueueCreate(1, sizeof(uint8_t));
    if (_pinTouchCs >= 0) _initTouch();
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
    // NVS is not used for widget storage — JSON can exceed NVS string limit (4000 bytes)
    // and fill the 20KB NVS partition, causing WiFi/MQTT credentials to fail to save.
    // Widget config is re-delivered by MQTT broker on reconnect (retain=true).
    _widgetsJson = widgetsJson;
    _needsRedraw = true;
}

void DisplayManager::loadAndRender() {
    // No-op: widget config comes from MQTT retain on reconnect.
    // Display shows blank until first MQTT delivery.
}

void DisplayManager::render() {
    if (!_ready || !_lcd) return;
    _needsRedraw = false;
    _buttons.clear();

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
            // Save hit area for touch polling
            ButtonHit hit;
            hit.x = x; hit.y = y; hit.w = ww; hit.h = hh;
            String topic = w["topic"] | "";
            JsonVariant payloadVar = w["payload"];
            String payloadStr;
            if (payloadVar.is<JsonObject>()) {
                serializeJson(payloadVar, payloadStr);
            } else {
                payloadStr = w["payload"] | "{}";
            }
            topic.toCharArray(hit.topic, sizeof(hit.topic));
            payloadStr.toCharArray(hit.payload, sizeof(hit.payload));
            _buttons.push_back(hit);
        } else if (strcmp(type, "gauge") == 0) {
            float minV = w["min"] | 0.0f;
            float maxV = w["max"] | 100.0f;
            _drawGauge(x, y, ww, hh, label, w["source"] | "", minV, maxV, fontSize, color, bg);
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
    String disp;
    if (source.length() > 0) {
        String val = getStateValue(source);
        disp = format;
        disp.replace("{v}", val.length() > 0 ? val : "—");
    }
    else { disp = label; }
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
                                float minV, float maxV,
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

    float val = getStateValue(source).toFloat();
    float pct = (maxV > minV) ? constrain((val - minV) / (maxV - minV), 0.0f, 1.0f) : 0.0f;
    int fillW = (int)((w - 4) * pct);

    uint16_t dark = (uint16_t)(color >> 2) & 0x39E7;
    _lcd->fillRoundRect(x + 2, barY, w - 4, barH, 2, dark);
    _lcd->fillRoundRect(x + 2, barY, fillW, barH, 2, color);
}

// ─── Thread-safe state cache ──────────────────────────────────────────────────

void DisplayManager::updateStateValue(const String& dotKey, const String& value) {
    if (!_stateMutex) return;
    xSemaphoreTake(_stateMutex, portMAX_DELAY);
    _stateCache[dotKey] = value;
    xSemaphoreGive(_stateMutex);
    _needsRedraw = true;
    Serial.printf("[display] state update: key=%s val=%s\n", dotKey.c_str(), value.c_str());
}

void DisplayManager::clearState() {
    if (!_stateMutex) return;
    xSemaphoreTake(_stateMutex, portMAX_DELAY);
    _stateCache.clear();
    xSemaphoreGive(_stateMutex);
}

String DisplayManager::getStateValue(const String& dotKey) const {
    if (!_stateMutex) return "";
    xSemaphoreTake(_stateMutex, portMAX_DELAY);
    auto it = _stateCache.find(dotKey);
    String result = (it != _stateCache.end()) ? it->second : "";
    xSemaphoreGive(_stateMutex);
    return result;
}

// ─── Touch (XPT2046) ──────────────────────────────────────────────────────────

void DisplayManager::_initTouch() {
    Serial.printf("[touch] XPT2046 init: cs=%d irq=%d\n", _pinTouchCs, _pinTouchIrq);
    Preferences prefs;
    prefs.begin("display", true);
    uint16_t data[8];
    if (prefs.getBytes("touch_cal", data, sizeof(data)) == sizeof(data)) {
        _lcd->setTouchCalibrate(data);
        Serial.println("[touch] calibration loaded from NVS");
    }
    prefs.end();
}

void DisplayManager::requestCalibrate() {
    if (!_calibrateQueue || _calibrating) return;
    uint8_t sig = 1;
    xQueueSend(_calibrateQueue, &sig, 0);
    Serial.println("[calibrate] request queued");
}

void DisplayManager::pollTouch() {
    if (!_ready || !_lcd || !_touchQueue) return;

    // Check calibration request (from Core 1 via MQTT/HTTP)
    uint8_t sig;
    if (_calibrateQueue && xQueueReceive(_calibrateQueue, &sig, 0) == pdTRUE) {
        _calibrating = true;
        Serial.println("[calibrate] starting...");
        uint16_t data[8];
        _lcd->calibrateTouch(data, TFT_WHITE, TFT_BLACK, 15);
        Preferences prefs;
        prefs.begin("display", false);
        prefs.putBytes("touch_cal", data, sizeof(data));
        prefs.end();
        _lcd->setTouchCalibrate(data);
        _calibrating = false;
        Serial.println("[calibrate] done, saved to NVS");
        return;
    }

    if (_buttons.empty()) return;

    uint16_t tx, ty;
    if (!_lcd->getTouch(&tx, &ty)) return;

    uint32_t now = millis();
    if (now - _lastTapMs < 300) return;  // debounce
    _lastTapMs = now;

    for (const auto& btn : _buttons) {
        if (tx >= (uint16_t)btn.x && tx <= (uint16_t)(btn.x + btn.w) &&
            ty >= (uint16_t)btn.y && ty <= (uint16_t)(btn.y + btn.h)) {
            // Visual feedback
            _lcd->fillRoundRect(btn.x, btn.y, btn.w, btn.h, 4, TFT_WHITE);
            vTaskDelay(pdMS_TO_TICKS(150));
            render();  // restore normal appearance

            // Send to queue
            TouchCmd cmd;
            strncpy(cmd.topic,   btn.topic,   sizeof(cmd.topic)   - 1);
            strncpy(cmd.payload, btn.payload, sizeof(cmd.payload) - 1);
            cmd.topic[sizeof(cmd.topic) - 1]   = '\0';
            cmd.payload[sizeof(cmd.payload) - 1] = '\0';

            if (xQueueSend(_touchQueue, &cmd, 0) != pdTRUE) {
                Serial.println("[touch] queue full — tap dropped");
            } else {
                Serial.printf("[touch] tap → topic=%s\n", cmd.topic);
            }
            break;
        }
    }
}

bool DisplayManager::getTouchCmd(TouchCmd& cmd) {
    if (!_touchQueue) return false;
    return xQueueReceive(_touchQueue, &cmd, 0) == pdTRUE;
}

#endif // MPCB_USE_DISPLAY
