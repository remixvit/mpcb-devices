#include <Arduino.h>
#include <zigbee/MpcbZigbeeCore.h>
#include <peripheral/PeriphManager.h>
#include <neopixel/WS2812Strip.h>

// ─── Пины ────────────────────────────────────────────────────────────────────
#define BUTTON_PIN  9   // BOOT — factory reset Zigbee (3 сек)
#define LED_PIN     8   // WS2812 статус

// ─── Core ────────────────────────────────────────────────────────────────────
MpcbZigbeeCore core;
PeriphManager  pm;
WS2812Strip    statusLed;

void setLed(uint8_t r, uint8_t g, uint8_t b) {
    statusLed.fill(r, g, b, 40);
    statusLed.show();
}

// Мигание синим — поиск сети
static uint32_t _blinkLast = 0;
static bool     _blinkOn   = false;
static bool     _inNetwork = false;

void blinkLoop() {
    if (_inNetwork) return;
    uint32_t now = millis();
    if (now - _blinkLast >= 500) {
        _blinkLast = now;
        _blinkOn   = !_blinkOn;
        if (_blinkOn) setLed(0, 0, 30);
        else          setLed(0, 0,  0);
    }
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(100);

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    statusLed.begin(LED_PIN, 1);
    setLed(0, 0, 30);  // синий = загрузка

    core.onMessage([](const String& t, const String& p) {
        pm.handleMessage(t, p);
    });
    core.onReady([]() {
        _inNetwork = true;
        setLed(0, 8, 0);  // тусклый зелёный = в сети
        pm.onMqttConnected();
    });

    core.begin("mpcb-c6z");
    pm.begin(core.deviceId(), "mpcb-c6z", core.storage(), core);

    // Если Zigbee уже был подключён (реджойн после ребута)
    if (Zigbee.isStarted()) {
        _inNetwork = true;
        setLed(0, 8, 0);
        pm.onMqttConnected();
    }
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
    core.loop();
    pm.loop();
    blinkLoop();

    // BOOT 3 сек = покинуть Zigbee сеть + factory reset
    if (digitalRead(BUTTON_PIN) == LOW) {
        setLed(30, 15, 0);  // жёлтый = кнопка нажата
        uint32_t t = millis();
        while (digitalRead(BUTTON_PIN) == LOW) {
            if (millis() - t > 3000) {
                setLed(30, 0, 0);  // красный = сброс
                delay(500);
                Zigbee.factoryReset();
                return;
            }
        }
    }
}
