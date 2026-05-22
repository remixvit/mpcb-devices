#include <Arduino.h>
#include <ZigbeeCore.h>
#include <ep/ZigbeeLight.h>
#include <neopixel/WS2812Strip.h>

// ─── Пины ────────────────────────────────────────────────────────────────────
#define RELAY_PIN   4   // реле (активный HIGH)
#define BUTTON_PIN  9   // BOOT кнопка — factory reset (3 сек)
#define LED_PIN     8   // WS2812 статус

// ─── Zigbee endpoint ─────────────────────────────────────────────────────────
ZigbeeLight zbRelay(1);  // endpoint 1 — On/Off Light (кластер 0x0006)

// ─── Статусный LED ───────────────────────────────────────────────────────────
WS2812Strip statusLed;

void setLed(uint8_t r, uint8_t g, uint8_t b) {
    statusLed.fill(r, g, b, 40);
    statusLed.show();
}

// ─── Blink state (поиск сети) ────────────────────────────────────────────────
static bool     _searching    = true;
static uint32_t _blinkLast    = 0;
static bool     _blinkOn      = false;

void blinkLoop() {
    if (!_searching) return;
    uint32_t now = millis();
    if (now - _blinkLast >= 500) {
        _blinkLast = now;
        _blinkOn   = !_blinkOn;
        if (_blinkOn) setLed(0, 0, 30);  // blue blink = ищем сеть
        else          setLed(0, 0,  0);
    }
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(100);

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    statusLed.begin(LED_PIN, 1);
    setLed(0, 0, 30);  // blue = загрузка

    // ── Zigbee endpoint ──────────────────────────────────────────────────────
    zbRelay.setManufacturerAndModel("mpcbstudio", "mpcb-relay");
    zbRelay.onLightChange([](bool on) {
        _searching = false;  // команда пришла — значит в сети
        digitalWrite(RELAY_PIN, on ? HIGH : LOW);
        setLed(on ? 0 : 0, on ? 60 : 0, 0);  // зелёный = реле вкл, выкл = темно
    });

    Zigbee.addEndpoint(&zbRelay);
    // erase_nvs=false — сохраняем сеть между перезагрузками
    Zigbee.begin(ZIGBEE_END_DEVICE, false);

    // Восстановить последнее состояние реле из NVS после ребута
    zbRelay.restoreLight();
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
    blinkLoop();

    // BOOT кнопка: удержание 3 сек = factory reset (покинуть сеть + стереть NVS)
    if (digitalRead(BUTTON_PIN) == LOW) {
        setLed(30, 15, 0);  // жёлтый = кнопка нажата
        uint32_t t = millis();
        while (digitalRead(BUTTON_PIN) == LOW) {
            if (millis() - t > 3000) {
                setLed(30, 0, 0);  // красный = сброс
                delay(500);
                Zigbee.factoryReset();  // покидаем сеть + reboot
                return;
            }
        }
        // Короткое нажатие — ничего не делаем
    }
}
