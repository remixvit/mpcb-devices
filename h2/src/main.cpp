#include <Arduino.h>
// TODO: подключить ESP Zigbee SDK
// ESP32-H2 — только Zigbee + BLE, нет WiFi.

// ─── Концепция ───────────────────────────────────────────────────────────────
// Ультра-низкое потребление для батарейных узлов.
// Работает годами от двух AA батарей.
//
// Режим работы:
//   1. Проснуться из deep sleep
//   2. Прочитать сенсор
//   3. Передать по Zigbee
//   4. Уйти в deep sleep на N секунд

void setup() {
    Serial.begin(115200);
    Serial.println("mpcb-h2 — TODO: Zigbee SDK + deep sleep integration");
    // TODO: инициализировать Zigbee end device (sleep-capable)
    // TODO: BLE конфигурация при первом запуске (без WiFi)
    // TODO: PowerManager: deep sleep между передачами
}

void loop() {
    // TODO: Zigbee loop, deep sleep управление
}
