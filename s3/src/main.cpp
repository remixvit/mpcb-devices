#include <Arduino.h>
#include <neopixel/WS2812Strip.h>
#include <MpcbIotCore.h>

#ifdef MPCB_USE_DISPLAY
#include "DisplayManager.h"
#endif

#ifndef MPCB_DEVICE_NAME
#define MPCB_DEVICE_NAME "ESP32-S3"
#endif

// ─── Board identity (→ device.state._board on API) ────────────────────────────
#define MPCB_BOARD_ID "s3-zero"

// ─── Status LED (GPIO21 — built-in WS2812 on S3-Zero) ────────────────────────
#define LED_PIN 21
WS2812Strip led;

void ledSet(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness = 50) {
    led.fill(r, g, b, brightness);
    led.show();
}

// ─── Core ────────────────────────────────────────────────────────────────────
MpcbIotCore   iot;
PeriphManager pm;
String        deviceId;

#ifdef MPCB_USE_DISPLAY
DisplayManager display;

// ─── Display loop (render when dirty) ────────────────────────────────────────
void displayLoop() {
    static uint32_t lastRedraw = 0;
    if (millis() - lastRedraw < 500) return;
    lastRedraw = millis();
    display.render();
}

// ─── Display Task (Core 0) — rendering only, no WiFi/MQTT ───────────────────
void displayTask(void* pv) {
    for (;;) {
        displayLoop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
#endif

// ─── LED state indication ───────────────────────────────────────────────────
void updateLed() {
    static uint32_t lastBlink = 0;
    static bool     blinkOn   = false;
    uint32_t now = millis();

    switch (iot.state()) {
        case IotState::BOOTING:
            ledSet(10, 10, 10);
            break;
        case IotState::CONNECTING:
            if (now - lastBlink >= 500) {
                lastBlink = now;
                blinkOn   = !blinkOn;
                ledSet(blinkOn ? 30 : 0, blinkOn ? 15 : 0, 0);  // yellow blink
            }
            break;
        case IotState::AP_PORTAL:
            ledSet(0, 0, 30);  // blue
            break;
        case IotState::CONFIG_SERVER:
            ledSet(0, 20, 20);  // cyan
            break;
        case IotState::RUNNING: {
            static uint32_t lastBeat = 0;
            static bool     beatOn   = false;
            if (!beatOn && now - lastBeat >= 3000) { ledSet(0, 8, 0); beatOn = true;  lastBeat = now; }
            else if (beatOn && now - lastBeat >= 80) { ledSet(0, 0, 0); beatOn = false; }
            break;
        }
    }
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(200);

    led.begin(LED_PIN, 1);
    ledSet(10, 10, 10);  // white dim — booting

    // ── NVS + MQTT defaults ────────────────────────────────────────────────
    iot.storage().begin();
    MqttConfig mqtt = iot.storage().loadMqtt();
    if (mqtt.host.isEmpty()) {
        mqtt.host     = "mqtt.mpcbstudio.com";
        mqtt.port     = 8883;
        mqtt.user     = "user_4";
        mqtt.password = "DATeyjWZ6sd9N-5wFtj5vg";
        mqtt.tls      = true;
        iot.storage().saveMqtt(mqtt);
    }

    // ── Device ID ──────────────────────────────────────────────────────────
    WiFi.mode(WIFI_STA);
    String devName;
    {
        DeviceConfig dev = iot.storage().loadDevice();
        if (dev.deviceId.isEmpty()) {
            uint8_t mac[6];
            WiFi.macAddress(mac);
            char suffix[5];
            snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
            dev.deviceId   = "esp32-s3-" + String(suffix);
            dev.deviceName = MPCB_DEVICE_NAME;
            iot.storage().saveDevice(dev);
        }
        deviceId = dev.deviceId;
        devName  = dev.deviceName;
    }

    // ── FSM indication ─────────────────────────────────────────────────────
    iot.onStateChange([](IotState s) {
        switch (s) {
            case IotState::AP_PORTAL:     ledSet(30, 15,  0); break;
            case IotState::CONNECTING:    ledSet( 0,  0, 30); break;
            case IotState::CONFIG_SERVER: ledSet( 0, 15, 30); break;
            case IotState::RUNNING: {
                for (int i = 0; i < 2; i++) {
                    ledSet(0, 60, 0); delay(150);
                    ledSet(0,  0, 0); delay(100);
                }
#ifdef MPCB_USE_DISPLAY
                display.loadAndRender();
#endif
                break;
            }
            default: break;
        }
    });

    // ── MQTT connected ─────────────────────────────────────────────────────
    iot.onMqttConnected([&]() {
        pm.onMqttConnected();
        String boardTopic = "mpcb/devices/" + deviceId + "/config/board";
        iot.publish(boardTopic, MPCB_BOARD_ID, true);
        iot.subscribe("mpcb/devices/" + deviceId + "/configure/set");
#ifdef MPCB_USE_DISPLAY
        iot.subscribe("mpcb/devices/" + deviceId + "/display/set");
#endif
    });

    // ── MQTT incoming ──────────────────────────────────────────────────────
    iot.onMqttMessage([](const String& topic, const String& payload) {
        if (pm.handleMessage(topic, payload)) return;
#ifdef MPCB_USE_DISPLAY
        if (topic.endsWith("/display/set")) {
            display.saveAndRender(payload);
        }
#endif
        if (topic.endsWith("/configure/set")) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, payload);
            if (err != DeserializationError::Ok) {
                Serial.println("[display] JSON parse error: " + String(err.c_str()));
                return;
            }
            JsonObject canvas = doc["canvas"];
            if (canvas.isNull()) return;
            JsonArray nodes = canvas["nodes"];
            if (nodes.isNull()) return;
            for (JsonObject node : nodes) {
                const char* ptype = node["peripheralType"];
                if (!ptype) continue;
                if (strcmp(ptype, "ili9341") != 0 && strcmp(ptype, "ili9341_touch") != 0) continue;
                // Found display node — extract pin connections
                // handle → NVS key mapping
                JsonArray conns = node["connections"];
                if (conns.isNull()) continue;
                Preferences prefs;
                prefs.begin("display", false);
                for (JsonObject conn : conns) {
                    const char* handle = conn["handle"];
                    int pin = conn["pin"] | -1;
                    if (!handle || pin < 0) continue;
                    if      (strcmp(handle, "mosi")  == 0) prefs.putInt("disp_mosi",  pin);
                    else if (strcmp(handle, "clk")   == 0) prefs.putInt("disp_clk",   pin);
                    else if (strcmp(handle, "cs")    == 0) prefs.putInt("disp_cs",    pin);
                    else if (strcmp(handle, "dc")    == 0) prefs.putInt("disp_dc",    pin);
                    else if (strcmp(handle, "rst")   == 0) prefs.putInt("disp_rst",   pin);
                    else if (strcmp(handle, "led")   == 0) prefs.putInt("disp_led",   pin);
                    else if (strcmp(handle, "miso")  == 0) prefs.putInt("disp_miso",  pin);
                    else if (strcmp(handle, "t_cs")  == 0) prefs.putInt("disp_t_cs",  pin);
                    else if (strcmp(handle, "t_irq") == 0) prefs.putInt("disp_t_irq", pin);
                }
                prefs.end();
                Serial.println("[display] pins saved from canvas, rebooting...");
                delay(200);
                ESP.restart();
                return;
            }
        }
    });

    // ── Dashboard ──────────────────────────────────────────────────────────
    iot.onDashState([](){ return pm.getStateJson(); });
    iot.onDashCmd([](const String& key, const String& payload){ pm.handleLocalCmd(key, payload); });

    // ── Network first ──────────────────────────────────────────────────────
    iot.begin(devName);

    // ── Peripherals ─────────────────────────────────────────────────────────
    pm.begin(deviceId, devName, iot.storage(), iot);
    if (iot.state() == IotState::RUNNING) pm.onMqttConnected();

#ifdef MPCB_USE_DISPLAY
    // ── Display init + Core 0 task ──────────────────────────────────────────
    if (display.begin()) {
        Serial.println("[display] init OK");
        display.loadAndRender();
    } else {
        Serial.println("[display] init FAILED — check NVS pins");
    }
    // Core 0 — display uses polling SPI (no DMA), no conflict with WiFi GDMA
    xTaskCreatePinnedToCore(displayTask, "display", 8192, NULL, 1, NULL, 0);
#endif

    Log.log("App", "Device ready: " + deviceId + " board=" MPCB_BOARD_ID);
}

// ─── Loop (Core 1) — WiFi + MQTT + peripherals + LED ─────────────────────────
void loop() {
    iot.loop();
    pm.loop();
    updateLed();
}
