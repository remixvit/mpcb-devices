#include "DisplayManager.h"
#ifdef MPCB_USE_DISPLAY

DisplayManager::DisplayManager() {}
DisplayManager::~DisplayManager() {
    delete _lcd;
}

bool DisplayManager::begin() {
    Preferences prefs;
    prefs.begin("display", true);
    _pinMosi  = prefs.getInt("disp_mosi", 13);
    _pinClk   = prefs.getInt("disp_clk",  12);
    _pinCs    = prefs.getInt("disp_cs",   11);
    _pinDc    = prefs.getInt("disp_dc",   10);
    _pinRst   = prefs.getInt("disp_rst",   9);
    _pinLed   = prefs.getInt("disp_led",   7);
    _pinMiso     = prefs.getInt("disp_miso",  8);
    _pinTouchCs  = prefs.getInt("disp_t_cs",  6);
    _pinTouchIrq = prefs.getInt("disp_t_irq", 5);
    prefs.end();

    Serial.printf("[display] pins: mosi=%d clk=%d cs=%d dc=%d rst=%d led=%d miso=%d\n",
                  _pinMosi, _pinClk, _pinCs, _pinDc, _pinRst, _pinLed, _pinMiso);
    if (_pinMosi < 0 || _pinClk < 0 || _pinCs < 0 || _pinDc < 0) {
        Serial.println("[display] pins not configured — skipping init");
        return false;
    }
    _lcd = new lgfx::LGFX_Device();
    if (!_lcd) return false;

    // SPI bus — display on SPI2_HOST, touch on SPI3_HOST (same GPIO pins, different peripherals)
    auto busCfg = _bus.config();
    busCfg.spi_host    = SPI2_HOST;
    busCfg.pin_mosi    = _pinMosi;
    busCfg.pin_sclk    = _pinClk;
    busCfg.pin_miso    = _pinMiso;
    busCfg.spi_mode    = 0;
    busCfg.freq_write  = 20000000;   // lower frequency for reliability
    busCfg.freq_read   = 8000000;
    busCfg.spi_3wire   = (_pinMiso < 0);
    busCfg.pin_dc      = _pinDc;
    busCfg.dma_channel = SPI_DMA_CH_AUTO;
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
    panelCfg.bus_shared   = true;   // touch shares same SPI bus
    _panel.config(panelCfg);

    _lcd->setPanel(&_panel);

    // Backlight ON before begin() so it's already driving HIGH when LCD initialises
    if (_pinLed >= 0) {
        pinMode(_pinLed, OUTPUT);
        digitalWrite(_pinLed, HIGH);
        Serial.printf("[display] BL pin %d → HIGH\n", _pinLed);
    }

    // XPT2046 touch — same SPI host as display so LovyanGFX manages bus arbitration
    if (_pinTouchCs >= 0) {
        static lgfx::Touch_XPT2046 touch;
        auto touchCfg = touch.config();
        touchCfg.x_min      = 300;
        touchCfg.x_max      = 3800;
        touchCfg.y_min      = 300;
        touchCfg.y_max      = 3800;
        touchCfg.pin_int    = -1;         // disable IRQ — use pure polling
        touchCfg.bus_shared = true;
        touchCfg.offset_rotation = 0;
        touchCfg.spi_host   = SPI2_HOST;  // same host as display bus
        touchCfg.freq       = 500000;     // 500 kHz — conservative for XPT2046
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

    _ready = true;
    _stateMutex = xSemaphoreCreateMutex();
    _touchQueue = xQueueCreate(4, sizeof(TouchCmd));
    _calibrateQueue = xQueueCreate(1, sizeof(uint8_t));
    if (_pinTouchCs >= 0) _initTouch();

    // Re-assert backlight after LCD init (begin() may reconfigure SPI pins)
    if (_pinLed >= 0) {
        pinMode(_pinLed, OUTPUT);
        digitalWrite(_pinLed, HIGH);
        Serial.printf("[display] BL re-assert pin %d → HIGH\n", _pinLed);
    }

    _lcd->fillScreen(TFT_BLACK);

    // Sprite mode disabled: LGFX_Sprite buffer lands in PSRAM, DMA can't read it → white screen.
    // Dirty render (per-widget hash) is used instead — only changed widgets are redrawn.
    Serial.printf("[display] PSRAM: %d KB — dirty render mode\n", ESP.getPsramSize() / 1024);
    _useSpriteRender = false;

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
    Serial.printf("[display] saveAndRender: %d bytes\n", widgetsJson.length());
    // Guard against race: MQTT callback (Core 1) writes while render() (Core 0) reads
    if (_stateMutex) xSemaphoreTake(_stateMutex, portMAX_DELAY);
    _widgetsJson = widgetsJson;
    _needsRedraw = true;
    if (_stateMutex) xSemaphoreGive(_stateMutex);
}

void DisplayManager::loadAndRender() {
    // No-op: widget config comes from MQTT retain on reconnect.
    // Display shows blank until first MQTT delivery.
}

void DisplayManager::render() {
    if (!_ready || !_lcd) return;
    _needsRedraw = false;

    if (_useSpriteRender) {
        _renderToSprite();
    } else {
        _renderDirty();
    }
}

uint32_t DisplayManager::_widgetHash(const JsonObject& w) {
    String s;
    s += (const char*)(w["type"] | "");
    s += String(w["x"] | 0);
    s += String(w["y"] | 0);
    s += String(w["w"] | 0);
    s += String(w["h"] | 0);
    s += (const char*)(w["label"] | "");
    s += (const char*)(w["color"] | "");
    s += (const char*)(w["bgColor"] | "");
    const char* src = w["source"] | "";
    if (src && strlen(src) > 0) s += getStateValue(src);
    s += (const char*)(w["colorOn"]  | "");
    s += (const char*)(w["colorOff"] | "");
    // djb2
    uint32_t h = 5381;
    for (char c : s) h = ((h << 5) + h) + (uint8_t)c;
    return h;
}

void DisplayManager::_renderWidgets(lgfx::LGFXBase* tgt) {
    if (_stateMutex) xSemaphoreTake(_stateMutex, portMAX_DELAY);
    String json = _widgetsJson;
    if (_stateMutex) xSemaphoreGive(_stateMutex);

    if (json.isEmpty()) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[render] JSON error: %s\n", err.c_str());
        tgt->fillScreen(TFT_BLACK);
        tgt->setCursor(10, 10);
        tgt->setTextColor(TFT_RED, TFT_BLACK);
        tgt->setTextSize(1);
        tgt->print("JSON err: ");
        tgt->println(err.c_str());
        return;
    }

    JsonArray widgets = doc["widgets"].as<JsonArray>();
    Serial.printf("[render] widgets count=%d\n", widgets.isNull() ? -1 : (int)widgets.size());
    if (widgets.isNull() || widgets.size() == 0) return;

    _buttons.clear();

    for (JsonVariant v : widgets) {
        JsonObject w = v.as<JsonObject>();
        if (w.isNull()) continue;

        const char* type = w["type"] | "";
        int x  = w["x"] | 0;
        int y  = w["y"] | 0;
        int ww = w["w"] | 100;
        int hh = w["h"] | 28;
        String label  = w["label"] | "";
        int fontSize  = w["fontSize"] | 14;
        uint16_t color = _hexToRgb565(w["color"]   | "#ffffff");
        uint16_t bg    = _hexToRgb565(w["bgColor"]  | "#000000");

        if (strcmp(type, "value") == 0) {
            _drawValue(tgt, x, y, ww, hh, label, w["source"] | "", w["format"] | "{v}", fontSize, color, bg);
        } else if (strcmp(type, "label") == 0) {
            _drawLabel(tgt, x, y, ww, hh, label, fontSize, color, bg);
        } else if (strcmp(type, "button") == 0) {
            _drawButton(tgt, x, y, ww, hh, label, fontSize, color, bg);
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
            String periph = w["peripheral"] | "";
            topic.toCharArray(hit.topic, sizeof(hit.topic));
            payloadStr.toCharArray(hit.payload, sizeof(hit.payload));
            periph.toCharArray(hit.peripheral, sizeof(hit.peripheral));
            _buttons.push_back(hit);
        } else if (strcmp(type, "gauge") == 0) {
            float minV = w["min"] | 0.0f;
            float maxV = w["max"] | 100.0f;
            _drawGauge(tgt, x, y, ww, hh, label, w["source"] | "", minV, maxV, fontSize, color, bg);
        } else if (strcmp(type, "rect") == 0) {
            bool filled   = w["filled"] | false;
            int  radius   = w["borderRadius"] | 0;
            int  thk      = w["thickness"] | 1;
            _drawRect(tgt, x, y, ww, hh, color, bg, filled, radius, thk);
        } else if (strcmp(type, "line") == 0) {
            int thk = w["thickness"] | 1;
            _drawLine(tgt, x, y, ww, hh, color, thk);
        } else if (strcmp(type, "led") == 0) {
            uint16_t on  = _hexToRgb565(w["colorOn"]  | "#22c55e");
            uint16_t off = _hexToRgb565(w["colorOff"] | "#ef4444");
            _drawLed(tgt, x, y, ww, hh, w["source"] | "", on, off);
        }
    }
}

void DisplayManager::_renderToSprite() {
    if (!_sprite) return;
    _sprite->fillScreen(TFT_BLACK);
    _renderWidgets(_sprite);
    _sprite->pushSprite(0, 0);
    Serial.println("[render] sprite push");
}

void DisplayManager::_renderDirty() {
    if (_stateMutex) xSemaphoreTake(_stateMutex, portMAX_DELAY);
    String json = _widgetsJson;
    if (_stateMutex) xSemaphoreGive(_stateMutex);

    if (json.isEmpty()) return;

    JsonDocument doc;
    if (deserializeJson(doc, json)) return;

    JsonArray widgets = doc["widgets"].as<JsonArray>();
    if (widgets.isNull()) return;

    _buttons.clear();

    size_t idx = 0;
    for (JsonVariant v : widgets) {
        JsonObject w = v.as<JsonObject>();
        if (w.isNull()) { idx++; continue; }

        uint32_t hash = _widgetHash(w);
        int x  = w["x"] | 0;
        int y  = w["y"] | 0;
        int ww = w["w"] | 100;
        int hh = w["h"] | 28;
        String id = w["id"] | String(idx);

        bool dirty = true;
        if (idx < _widgetCache.size() && _widgetCache[idx].id == id) {
            dirty = (_widgetCache[idx].contentHash != hash);
        }

        if (dirty) {
            const char* bgStr = w["bgColor"] | "";
            bool transBg = _isTransparent(bgStr);
            uint16_t bg    = transBg ? TFT_BLACK : _hexToRgb565(bgStr);
            uint16_t color = _hexToRgb565(w["color"] | "#ffffff");
            int fontSize   = w["fontSize"] | 14;
            const char* type = w["type"] | "";

            if (!transBg) _lcd->fillRect(x, y, ww, hh, TFT_BLACK);

            if (strcmp(type, "value") == 0) {
                _drawValue(_lcd, x, y, ww, hh, w["label"] | "", w["source"] | "", w["format"] | "{v}", fontSize, color, bg);
            } else if (strcmp(type, "label") == 0) {
                _drawLabel(_lcd, x, y, ww, hh, w["label"] | "", fontSize, color, bg);
            } else if (strcmp(type, "button") == 0) {
                _drawButton(_lcd, x, y, ww, hh, w["label"] | "", fontSize, color, bg);
            } else if (strcmp(type, "gauge") == 0) {
                _drawGauge(_lcd, x, y, ww, hh, w["label"] | "", w["source"] | "",
                           w["min"] | 0.0f, w["max"] | 100.0f, fontSize, color, bg);
            } else if (strcmp(type, "rect") == 0) {
                _drawRect(_lcd, x, y, ww, hh, color, bg, w["filled"] | false,
                          w["borderRadius"] | 0, w["thickness"] | 1);
            } else if (strcmp(type, "line") == 0) {
                _drawLine(_lcd, x, y, ww, hh, color, w["thickness"] | 1);
            } else if (strcmp(type, "led") == 0) {
                uint16_t on  = _hexToRgb565(w["colorOn"]  | "#22c55e");
                uint16_t off = _hexToRgb565(w["colorOff"] | "#ef4444");
                _drawLed(_lcd, x, y, ww, hh, w["source"] | "", on, off);
            }

            if (idx >= _widgetCache.size()) _widgetCache.resize(idx + 1);
            _widgetCache[idx] = { id, x, y, ww, hh, 0, hash };
        }

        if (strcmp(w["type"] | "", "button") == 0) {
            ButtonHit hit;
            hit.x = x; hit.y = y; hit.w = ww; hit.h = hh;
            String topic = w["topic"] | "";
            JsonVariant pv = w["payload"];
            String payloadStr;
            if (pv.is<JsonObject>()) serializeJson(pv, payloadStr);
            else payloadStr = w["payload"] | "{}";
            String periph = w["peripheral"] | "";
            topic.toCharArray(hit.topic, sizeof(hit.topic));
            payloadStr.toCharArray(hit.payload, sizeof(hit.payload));
            periph.toCharArray(hit.peripheral, sizeof(hit.peripheral));
            _buttons.push_back(hit);
        }
        idx++;
    }
    if (_widgetCache.size() > idx) _widgetCache.resize(idx);
}

uint16_t DisplayManager::_hexToRgb565(const char* hex) {
    if (!hex || strlen(hex) < 7 || strcmp(hex, "transparent") == 0) return TFT_BLACK;
    uint8_t r = strtol(String(hex).substring(1, 3).c_str(), nullptr, 16);
    uint8_t g = strtol(String(hex).substring(3, 5).c_str(), nullptr, 16);
    uint8_t b = strtol(String(hex).substring(5, 7).c_str(), nullptr, 16);
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

bool DisplayManager::_isTransparent(const char* hex) {
    return !hex || strlen(hex) == 0 || strcmp(hex, "transparent") == 0;
}

float DisplayManager::_textScale(int fontSize) const {
    if (fontSize <= 8)  return 1.0f;
    if (fontSize <= 12) return 1.5f;
    if (fontSize <= 16) return 2.0f;
    if (fontSize <= 20) return 2.5f;
    return 3.0f;
}

void DisplayManager::_drawValue(lgfx::LGFXBase* tgt, int x, int y, int w, int h,
                                const String& label, const String& source,
                                const String& format,
                                int fontSize, uint16_t color, uint16_t bg) {
    if (!tgt) return;
    tgt->fillRect(x, y, w, h, bg);

    if (label.length() > 0) {
        tgt->setTextSize(1.0f);
        tgt->setTextColor(color, bg);
        tgt->setCursor(x + 4, y + 2);
        tgt->print(label);
    }

    float scale = _textScale(fontSize);
    tgt->setTextSize(scale);
    tgt->setTextColor(color, bg);
    int valY = label.length() > 0 ? y + h / 2 : y + (h - (int)(8 * scale)) / 2;
    tgt->setCursor(x + 4, valY);
    String disp;
    if (source.length() > 0) {
        String val = getStateValue(source);
        disp = format;
        disp.replace("{v}", val.length() > 0 ? val : "--");
    }
    else { disp = label; }
    tgt->println(disp);
}

void DisplayManager::_drawLabel(lgfx::LGFXBase* tgt, int x, int y, int w, int h,
                                const String& text,
                                int fontSize, uint16_t color, uint16_t bg) {
    if (!tgt) return;
    tgt->fillRect(x, y, w, h, bg);
    float scale = _textScale(fontSize);
    tgt->setTextSize(scale);
    tgt->setTextColor(color, bg);
    tgt->setCursor(x + 4, y + (h - (int)(8 * scale)) / 2);
    tgt->print(text);
}

void DisplayManager::_drawButton(lgfx::LGFXBase* tgt, int x, int y, int w, int h,
                                 const String& label,
                                 int fontSize, uint16_t color, uint16_t bg) {
    if (!tgt) return;
    tgt->fillRoundRect(x, y, w, h, 4, bg);
    tgt->drawRoundRect(x, y, w, h, 4, color);
    float scale = _textScale(fontSize);
    tgt->setTextSize(scale);
    tgt->setTextColor(color, bg);
    int16_t tw = label.length() * 6 * scale;
    int tx = x + (w - tw) / 2;
    if (tx < x + 4) tx = x + 4;
    tgt->setCursor(tx, y + (h - (int)(8 * scale)) / 2);
    tgt->print(label);
}

void DisplayManager::_drawRect(lgfx::LGFXBase* tgt, int x, int y, int w, int h,
                               uint16_t color, uint16_t bg, bool filled, int radius, int thickness) {
    if (!tgt) return;
    if (filled) {
        if (radius > 0) tgt->fillRoundRect(x, y, w, h, radius, color);
        else            tgt->fillRect(x, y, w, h, color);
    } else {
        // draw outline with thickness
        for (int i = 0; i < thickness; i++) {
            if (radius > 0) tgt->drawRoundRect(x + i, y + i, w - i * 2, h - i * 2, radius, color);
            else            tgt->drawRect(x + i, y + i, w - i * 2, h - i * 2, color);
        }
    }
}

void DisplayManager::_drawLine(lgfx::LGFXBase* tgt, int x, int y, int w, int h,
                               uint16_t color, int thickness) {
    if (!tgt) return;
    // Widget bbox: (x,y)→(x+w, y+h). Draw line from top-left to bottom-right of bbox.
    for (int i = 0; i < thickness; i++) {
        tgt->drawLine(x, y + i, x + w, y + h + i, color);
    }
}

void DisplayManager::_drawLed(lgfx::LGFXBase* tgt, int x, int y, int w, int h,
                              const String& source, uint16_t colorOn, uint16_t colorOff) {
    if (!tgt) return;
    String val = source.length() > 0 ? getStateValue(source) : "";
    bool on = (val == "true" || val == "1" || (val.length() > 0 && val != "false" && val != "0" && val.toFloat() > 0));
    int r = min(w, h) / 2 - 1;
    int cx = x + w / 2;
    int cy = y + h / 2;
    uint16_t clr = on ? colorOn : colorOff;
    tgt->fillCircle(cx, cy, r, clr);
    // dim ring when off
    uint16_t ring = on ? colorOn : (uint16_t)((colorOff >> 2) & 0x39E7);
    tgt->drawCircle(cx, cy, r, ring);
}

void DisplayManager::_drawGauge(lgfx::LGFXBase* tgt, int x, int y, int w, int h,
                                const String& label, const String& source,
                                float minV, float maxV,
                                int fontSize, uint16_t color, uint16_t bg) {
    if (!tgt) return;
    tgt->fillRect(x, y, w, h, bg);

    int barH = h - 4;
    int barY = y + 2;
    if (label.length() > 0) {
        tgt->setTextSize(1.0f);
        tgt->setTextColor(color, bg);
        tgt->setCursor(x + 4, y + 2);
        tgt->print(label);
        barH = h - 16;
        barY = y + 14;
    }

    float val = getStateValue(source).toFloat();
    float pct = (maxV > minV) ? constrain((val - minV) / (maxV - minV), 0.0f, 1.0f) : 0.0f;
    int fillW = (int)((w - 4) * pct);

    uint16_t dark = (uint16_t)(color >> 2) & 0x39E7;
    tgt->fillRoundRect(x + 2, barY, w - 4, barH, 2, dark);
    tgt->fillRoundRect(x + 2, barY, fillW, barH, 2, color);
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
    Serial.printf("[touch] XPT2046 init: cs=%d irq=%d miso=%d mosi=%d clk=%d\n",
                  _pinTouchCs, _pinTouchIrq, _pinMiso, _pinMosi, _pinClk);
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
        if (_pinTouchCs < 0) {
            Serial.println("[calibrate] no touch pin — skip");
            return;
        }
        _calibrating = true;

        // ── Phase 0: raw touch probe (5 s) ───────────────────────────────
        _lcd->fillScreen(TFT_BLACK);
        _lcd->setTextColor(TFT_YELLOW, TFT_BLACK);
        _lcd->setTextSize(2);
        _lcd->setCursor(10, 100);
        _lcd->print("Touch probe 5s");
        _lcd->setCursor(10, 125);
        _lcd->print("Tap anywhere...");
        Serial.println("[calibrate] raw touch probe — tap anywhere, watching 5s");

        bool touchSeen = false;
        uint32_t probeEnd = millis() + 5000;
        while (millis() < probeEnd) {
            uint16_t rx, ry;
            if (_lcd->getTouch(&rx, &ry)) {
                Serial.printf("[calibrate] RAW touch detected: x=%d y=%d\n", rx, ry);
                touchSeen = true;
                _lcd->fillScreen(TFT_BLACK);
                _lcd->setTextColor(TFT_GREEN, TFT_BLACK);
                _lcd->setTextSize(2);
                _lcd->setCursor(10, 110);
                _lcd->printf("Touch OK! x=%d y=%d", rx, ry);
                vTaskDelay(pdMS_TO_TICKS(800));
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (!touchSeen) {
            Serial.println("[calibrate] WARNING: no touch detected in 5s — XPT2046 may not respond");
            _lcd->fillScreen(TFT_RED);
            _lcd->setTextColor(TFT_WHITE, TFT_RED);
            _lcd->setTextSize(2);
            _lcd->setCursor(10, 110);
            _lcd->print("No touch! Check SPI");
            vTaskDelay(pdMS_TO_TICKS(2000));
            _calibrating = false;
            _needsRedraw = true;
            return;
        }

        // ── Phase 1: LovyanGFX built-in calibration ───────────────────────
        _lcd->fillScreen(TFT_BLACK);
        _lcd->setTextColor(TFT_WHITE, TFT_BLACK);
        _lcd->setTextSize(2);
        _lcd->setCursor(10, 110);
        _lcd->print("Tap each crosshair");
        vTaskDelay(pdMS_TO_TICKS(1000));

        uint16_t data[8];
        _lcd->calibrateTouch(data, TFT_WHITE, TFT_BLACK, 20);
        Serial.printf("[calibrate] cal data: %d %d %d %d %d %d %d %d\n",
                      data[0],data[1],data[2],data[3],data[4],data[5],data[6],data[7]);

        Preferences prefs;
        prefs.begin("display", false);
        prefs.putBytes("touch_cal", data, sizeof(data));
        prefs.end();
        _lcd->setTouchCalibrate(data);
        _calibrating = false;
        Serial.println("[calibrate] done, saved to NVS");

        _lcd->fillScreen(TFT_GREEN);
        _lcd->setTextColor(TFT_BLACK, TFT_GREEN);
        _lcd->setTextSize(2);
        _lcd->setCursor(10, 110);
        _lcd->print("Calibration saved!");
        vTaskDelay(pdMS_TO_TICKS(1500));
        _needsRedraw = true;
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
            // Visual feedback — clear cache so dirty render redraws everything after flash
            _lcd->fillRoundRect(btn.x, btn.y, btn.w, btn.h, 4, TFT_WHITE);
            vTaskDelay(pdMS_TO_TICKS(150));
            _widgetCache.clear();
            render();

            // Send to queue
            TouchCmd cmd;
            strncpy(cmd.topic,      btn.topic,      sizeof(cmd.topic)      - 1);
            strncpy(cmd.payload,    btn.payload,    sizeof(cmd.payload)    - 1);
            strncpy(cmd.peripheral, btn.peripheral, sizeof(cmd.peripheral) - 1);
            cmd.topic[sizeof(cmd.topic)           - 1] = '\0';
            cmd.payload[sizeof(cmd.payload)       - 1] = '\0';
            cmd.peripheral[sizeof(cmd.peripheral) - 1] = '\0';

            if (strlen(cmd.topic) == 0) {
                Serial.println("[touch] tap ignored — empty topic");
            } else if (xQueueSend(_touchQueue, &cmd, 0) != pdTRUE) {
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
