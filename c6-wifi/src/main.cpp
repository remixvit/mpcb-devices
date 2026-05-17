#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <MpcbIotCore.h>

// ─── System LED (GPIO8 — встроенный WS2812, статус устройства) ──────────────
#define STATUS_PIN 8
Adafruit_NeoPixel statusLed(1, STATUS_PIN, NEO_GRB + NEO_KHZ800);

void setColor(uint8_t r, uint8_t g, uint8_t b) {
    statusLed.setPixelColor(0, statusLed.Color(r, g, b));
    statusLed.show();
}

// ─── Core ────────────────────────────────────────────────────────────────────
MpcbIotCore    iot;
PeriphManager  pm;
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

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(200);

    statusLed.begin();
    statusLed.setBrightness(50);
    setColor(0, 0, 30);  // blue — booting

    // ── NVS + MQTT defaults ────────────────────────────────────────────────
    iot.storage().begin();
    MqttConfig mqtt = iot.storage().loadMqtt();
    if (mqtt.host.isEmpty()) {
        mqtt.host = "mqtt.mpcbstudio.com";
        mqtt.port = 8883;
        mqtt.user = "user_4";
        mqtt.password = "DATeyjWZ6sd9N-5wFtj5vg";
        mqtt.tls  = true;
        iot.storage().saveMqtt(mqtt);
    }

    // ── Device ID (needed for pm.begin — created before network) ────────────
    // WiFi.mode(WIFI_STA) must be called before I2C — initializes power domains
    // and clocks needed for stable I2C operation on ESP32-C6.
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
            dev.deviceName = "mpcb device";
            iot.storage().saveDevice(dev);
        }
        deviceId = dev.deviceId;
        devName  = dev.deviceName;
    }

    // ── FSM indication ──────────────────────────────────────────────────────
    iot.onStateChange([](IotState s) {
        switch (s) {
            case IotState::AP_PORTAL:     setColor(30, 15,  0); break; // yellow
            case IotState::CONNECTING:    setColor( 0,  0, 30); break; // blue
            case IotState::CONFIG_SERVER: setColor( 0, 15, 30); break; // cyan
            case IotState::RUNNING: {
                // 2x green pulse = connected
                for (int i = 0; i < 2; i++) {
                    setColor(0, 60, 0); delay(150);
                    setColor(0,  0, 0); delay(100);
                }
                break;
            }
            default: break;
        }
    });

    // ── MQTT connected (initial + reconnect) ────────────────────────────────
    iot.onMqttConnected([]() {
        pm.onMqttConnected();  // re-subscribe + config + initial states
    });

    // ── MQTT incoming ───────────────────────────────────────────────────────
    iot.onMqttMessage([](const String& topic, const String& payload) {
        pm.handleMessage(topic, payload);
    });

    // ── Dashboard ───────────────────────────────────────────────────────────
    iot.onDashState([](){ return pm.getStateJson(); });
    iot.onDashCmd([](const String& key, const String& payload){ pm.handleLocalCmd(key, payload); });

    // ── Peripherals FIRST: I2C + sensors ready before network ───────────────
    pm.begin(deviceId, devName, iot.storage(), iot);

    // ── Network AFTER peripherals: WiFi → mDNS → MQTT ───────────────────────
    iot.begin(devName);
    // pm already initialized — onMqttConnected callback works correctly
    pm.resetI2C();  // BLE/WiFi init may disrupt I2C GPIO mux on ESP32-C6

    Log.log("App", "Device ready: " + deviceId);
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
    iot.loop();
    pm.loop();
    heartbeat();
}
