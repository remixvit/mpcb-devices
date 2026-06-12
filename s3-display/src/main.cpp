#include <Arduino.h>
#include <neopixel/WS2812Strip.h>
#include <MpcbIotCore.h>
#include "DisplayManager.h"

#ifndef MPCB_DEVICE_NAME
#define MPCB_DEVICE_NAME "S3 Display"
#endif

// ─── Board identity (→ device.state._board on API) ────────────────────────────
// Published as retained config message on MQTT connect.
#define MPCB_BOARD_ID "s3-display"

// ─── Status LED (GPIO21 — built-in WS2812 on S3-Zero) ────────────────────────
#define LED_PIN 21
WS2812Strip led;

void ledSet(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness = 50) {
    led.fill(r, g, b, brightness);
    led.show();
}

// ─── Core ────────────────────────────────────────────────────────────────────
MpcbIotCore    iot;
PeriphManager  pm;
DisplayManager display;
String         deviceId;

// ─── LED state indication ───────────────────────────────────────────────────
void updateLed() {
    static uint32_t lastBlink = 0;
    static bool     blinkOn   = false;
    uint32_t now = millis();

    switch (iot.state()) {
        case IotState::BOOTING:
            ledSet(10, 10, 10);  // white dim
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
        case IotState::RUNNING:
            ledSet(0, 20, 0);  // green
            break;
    }
}

// ─── Display loop (render when dirty) ────────────────────────────────────────
void displayLoop() {
    static uint32_t lastRedraw = 0;
    uint32_t now = millis();
    // Debounce re-draws: at most once per 500ms
    if (now - lastRedraw < 500) return;
    lastRedraw = now;
    display.render();
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
        // Migrate old format: "esp32-XXXX" or "esp32-s3-XXXX" → "esp32-s3display-XXXX"
        if (!dev.deviceId.isEmpty() && dev.deviceId.indexOf("s3display") < 0) {
            dev.deviceId.clear();
        }
        if (dev.deviceId.isEmpty()) {
            uint8_t mac[6];
            WiFi.macAddress(mac);
            char suffix[5];
            snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
            dev.deviceId   = "esp32-s3display-" + String(suffix);
            dev.deviceName = MPCB_DEVICE_NAME;
            iot.storage().saveDevice(dev);
        }
        deviceId = dev.deviceId;
        devName  = dev.deviceName;
    }

    // ── FSM indication ─────────────────────────────────────────────────────
    iot.onStateChange([](IotState s) {
        switch (s) {
            case IotState::AP_PORTAL:     ledSet(30, 15,  0); break; // yellow
            case IotState::CONNECTING:    ledSet( 0,  0, 30); break; // blue
            case IotState::CONFIG_SERVER: ledSet( 0, 15, 30); break; // cyan
            case IotState::RUNNING: {
                for (int i = 0; i < 2; i++) {
                    ledSet(0, 60, 0); delay(150);
                    ledSet(0,  0, 0); delay(100);
                }
                // Load and render saved display widgets on startup
                display.loadAndRender();
                break;
            }
            default: break;
        }
    });

    // ── MQTT connected ─────────────────────────────────────────────────────
    iot.onMqttConnected([&]() {
        pm.onMqttConnected();

        // Publish board identity (retained — API stores as device.state._board)
        String boardTopic = "mpcb/devices/" + deviceId + "/config/board";
        iot.publish(boardTopic, MPCB_BOARD_ID, true);

        // Subscribe to display widget updates from API
        iot.subscribe("mpcb/devices/" + deviceId + "/display/set");
    });

    // ── MQTT incoming ──────────────────────────────────────────────────────
    iot.onMqttMessage([](const String& topic, const String& payload) {
        // Let PeriphManager handle peripheral commands first
        if (pm.handleMessage(topic, payload)) return;

        // Handle display widget config
        if (topic.endsWith("/display/set")) {
            display.saveAndRender(payload);
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

    // ── Display ─────────────────────────────────────────────────────────────
    display.begin();
    display.loadAndRender();

    Log.log("App", "Device ready: " + deviceId + " board=" MPCB_BOARD_ID);
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
    iot.loop();
    pm.loop();
    updateLed();
    displayLoop();
}
