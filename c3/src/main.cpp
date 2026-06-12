#include <Arduino.h>
#include <MpcbIotCore.h>

#ifndef MPCB_DEVICE_NAME
#define MPCB_DEVICE_NAME "ESP32-C3"
#endif

// ─── System LED (GPIO8 — onboard blue LED, active HIGH) ──────────────────
#define LED_PIN 8
#define LED_ON  HIGH
#define LED_OFF LOW

// ─── Core ────────────────────────────────────────────────────────────────────
MpcbIotCore   iot;
PeriphManager pm;
String        deviceId;

// ─── LED helpers ─────────────────────────────────────────────────────────────
void led(bool on) { digitalWrite(LED_PIN, on ? LED_ON : LED_OFF); }

// AP/Connecting — non-blocking blink
void blinkLoop() {
    IotState s = iot.state();
    if (s != IotState::AP_PORTAL && s != IotState::CONNECTING) return;
    static uint32_t last = 0;
    static bool     on   = false;
    uint32_t interval = (s == IotState::AP_PORTAL) ? 250 : 500;
    uint32_t now = millis();
    if (now - last >= interval) { last = now; on = !on; led(on); }
}

// Running — dim heartbeat pulse every 3s
void heartbeat() {
    if (iot.state() != IotState::RUNNING) return;
    static uint32_t lastBeat = 0;
    static bool     beatOn   = false;
    uint32_t now = millis();
    if (!beatOn && now - lastBeat >= 3000) { led(true);  beatOn = true;  lastBeat = now; }
    else if (beatOn && now - lastBeat >= 80) { led(false); beatOn = false; }
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(LED_PIN, OUTPUT);
    led(true);  // on during boot

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

    // ── Device ID (needed for pm.begin — created before network) ────────────
    // WiFi.mode(WIFI_STA) must be called before I2C — initializes power domains
    // and clocks needed for stable I2C operation on ESP32-C3.
    WiFi.mode(WIFI_STA);
    String devName;
    {
        DeviceConfig dev = iot.storage().loadDevice();
        // Migrate old format: "esp32-XXXX" → "esp32-c3-XXXX"
        if (!dev.deviceId.isEmpty() && dev.deviceId.indexOf("c3") < 0) {
            dev.deviceId.clear();
        }
        if (dev.deviceId.isEmpty()) {
            uint8_t mac[6];
            WiFi.macAddress(mac);
            char suffix[5];
            snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
            dev.deviceId   = "esp32-c3-" + String(suffix);
            dev.deviceName = MPCB_DEVICE_NAME;
            iot.storage().saveDevice(dev);
        }
        deviceId = dev.deviceId;
        devName  = dev.deviceName;
    }

    // ── FSM indication ──────────────────────────────────────────────────────
    iot.onStateChange([](IotState s) {
        switch (s) {
            case IotState::AP_PORTAL:     break;  // blinkLoop()
            case IotState::CONNECTING:    break;  // blinkLoop()
            case IotState::CONFIG_SERVER: led(false); break;
            case IotState::RUNNING: {
                // 2x pulse = connected
                for (int i = 0; i < 2; i++) {
                    led(true); delay(150); led(false); delay(100);
                }
                break;
            }
            default: break;
        }
    });

    // ── MQTT connected (initial + reconnect) ────────────────────────────────
    iot.onMqttConnected([]() {
        pm.onMqttConnected();
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
    pm.resetI2C();  // BLE/WiFi init may disrupt I2C GPIO mux on ESP32-C3

    Log.log("App", "Device ready: " + deviceId);
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
    iot.loop();
    pm.loop();
    blinkLoop();
    heartbeat();
}
