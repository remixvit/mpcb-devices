#include <Arduino.h>
#include <TFT_eSPI.h>
#include <MpcbIotCore.h>

// ─── Display ─────────────────────────────────────────────────────────────────
TFT_eSPI tft;

// ─── Core ────────────────────────────────────────────────────────────────────
MpcbIotCore   iot;
PeriphManager pm;
String        deviceId;

void showStatus(const char* line1, const char* line2 = "") {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println(line1);
    if (line2[0]) tft.println(line2);
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(200);

    tft.init();
    tft.setRotation(0);
    showStatus("MPCB T4", "Booting...");

    // NVS + MQTT defaults
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

    // Device ID
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
            dev.deviceName = "T4 Scale";
            iot.storage().saveDevice(dev);
        }
        deviceId = dev.deviceId;
        devName  = dev.deviceName;
    }

    iot.onStateChange([](IotState s) {
        switch (s) {
            case IotState::AP_PORTAL:     showStatus("AP Portal",    "Connect WiFi"); break;
            case IotState::CONNECTING:    showStatus("Connecting...", ""); break;
            case IotState::CONFIG_SERVER: showStatus("Config mode",   ""); break;
            case IotState::RUNNING:       showStatus("Online!", ""); break;
            default: break;
        }
    });

    iot.onMqttConnected([]() {
        pm.onMqttConnected();
    });

    iot.onMqttMessage([](const String& topic, const String& payload) {
        pm.handleMessage(topic, payload);
    });

    iot.onDashState([](){ return pm.getStateJson(); });
    iot.onDashCmd([](const String& key, const String& payload){ pm.handleLocalCmd(key, payload); });

    iot.begin(devName);

    pm.begin(deviceId, devName, iot.storage(), iot);
    if (iot.state() == IotState::RUNNING) pm.onMqttConnected();
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
    iot.loop();
    pm.loop();
}
