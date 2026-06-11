#include <Arduino.h>
#include <neopixel/WS2812Strip.h>
#include <MpcbIotCore.h>
#include "DisplayManager.h"

// ─── Board identity (→ device.state._board on API) ────────────────────────────
// Published as retained config message on MQTT connect.
#define MPCB_BOARD_ID "s3-zero"

// ─── Status LED (GPIO48 — built-in WS2812 on S3-Zero) ────────────────────────
#define STATUS_PIN 48
WS2812Strip statusLed;

void setColor(uint8_t r, uint8_t g, uint8_t b) {
    statusLed.fill(r, g, b, 50);
    statusLed.show();
}

// ─── Core ────────────────────────────────────────────────────────────────────
MpcbIotCore    iot;
PeriphManager  pm;
DisplayManager display;
String         deviceId;

// ─── Heartbeat (dim green pulse when idle) ───────────────────────────────────
void heartbeat() {
    if (iot.state() != IotState::RUNNING) return;
    static uint32_t lastBeat = 0;
    static bool     beatOn   = false;
    uint32_t now = millis();
    if (!beatOn && now - lastBeat >= 3000) { setColor(0, 8, 0); beatOn = true;  lastBeat = now; }
    else if (beatOn && now - lastBeat >= 80) { setColor(0, 0, 0); beatOn = false; }
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

    statusLed.begin(STATUS_PIN, 1);
    setColor(0, 0, 30);  // blue — booting

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
            dev.deviceId   = "esp32-" + String(suffix);
            dev.deviceName = "mpcb display";
            iot.storage().saveDevice(dev);
        }
        deviceId = dev.deviceId;
        devName  = dev.deviceName;
    }

    // ── FSM indication ─────────────────────────────────────────────────────
    iot.onStateChange([](IotState s) {
        switch (s) {
            case IotState::AP_PORTAL:     setColor(30, 15,  0); break; // yellow
            case IotState::CONNECTING:    setColor( 0,  0, 30); break; // blue
            case IotState::CONFIG_SERVER: setColor( 0, 15, 30); break; // cyan
            case IotState::RUNNING: {
                for (int i = 0; i < 2; i++) {
                    setColor(0, 60, 0); delay(150);
                    setColor(0,  0, 0); delay(100);
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
    heartbeat();
    displayLoop();
}
