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
    setColor(0, 0, 30);  // синий — загрузка

    // MQTT defaults при первом запуске
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

    // ── FSM индикация ────────────────────────────────────────────────────────
    iot.onStateChange([](IotState s) {
        switch (s) {
            case IotState::AP_PORTAL:     setColor(30, 15,  0); break; // жёлтый
            case IotState::CONNECTING:    setColor( 0,  0, 30); break; // синий
            case IotState::CONFIG_SERVER: setColor( 0, 15, 30); break; // голубой
            case IotState::RUNNING:
                // 2× зелёный пульс → подключились
                for (int i = 0; i < 2; i++) {
                    setColor(0, 60, 0); delay(150);
                    setColor(0,  0, 0); delay(100);
                }
                // Загружаем deviceId здесь — iot.begin() уже сохранил его в NVS
                deviceId = iot.storage().loadDevice().deviceId;
                pm.begin(deviceId, iot.storage(), iot);
                break;
            default: break;
        }
    });

    // ── MQTT подключение (начальное + реконнект) ─────────────────────────────
    iot.onMqttConnected([]() {
        pm.onMqttConnected();  // re-subscribe + config + initial states
    });

    // ── MQTT входящие ────────────────────────────────────────────────────────
    iot.onMqttMessage([](const String& topic, const String& payload) {
        pm.handleMessage(topic, payload);
    });

    // ── Dashboard ────────────────────────────────────────────────────────────
    iot.onDashState([](){ return pm.getStateJson(); });
    iot.onDashCmd([](const String& key, const String& payload){ pm.handleLocalCmd(key, payload); });

    iot.begin("mpcb device");
    deviceId = iot.storage().loadDevice().deviceId;
    Log.log("App", "Device ready: " + deviceId);
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
    iot.loop();
    pm.loop();
    heartbeat();
}
