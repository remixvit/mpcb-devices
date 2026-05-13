#include <Arduino.h>
#include <MpcbIotCore.h>

// ─── Core ────────────────────────────────────────────────────────────────────
MpcbIotCore   iot;
PeriphManager pm;
String        deviceId;

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    iot.onStateChange([](IotState s) {
        if (s == IotState::RUNNING) {
            deviceId = iot.storage().loadDevice().deviceId;
            pm.begin(deviceId, iot.storage(), iot);
        }
    });

    iot.onMqttConnected([]() {
        pm.onMqttConnected();
    });

    iot.onMqttMessage([](const String& topic, const String& payload) {
        pm.handleMessage(topic, payload);
    });

    iot.begin("mpcb-c3");
    deviceId = iot.storage().loadDevice().deviceId;
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
    iot.loop();
    pm.loop();
    // TODO: добавить PowerManager deep sleep между опросами сенсоров
}
