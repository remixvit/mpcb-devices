#include <Arduino.h>
// TODO: подключить ESP Zigbee SDK (через IDF компоненты)
// Документация: https://docs.espressif.com/projects/esp-zigbee-sdk/

// ─── Концепция ───────────────────────────────────────────────────────────────
// ESP32-C6 имеет два отдельных радио: WiFi/BLE и IEEE 802.15.4 (Zigbee).
// Этот проект использует Zigbee как транспорт, BLE — только для конфигурации.
//
// Топология:
//   [mpcb-c6-zigbee] ──Zigbee──→ [Zigbee координатор] ──→ [Home Assistant / mpcb cloud]

void setup() {
    Serial.begin(115200);
    Serial.println("mpcb-c6-zigbee — TODO: Zigbee SDK integration");
    // TODO: инициализировать Zigbee end device / router
    // TODO: BLE конфигурация через MpcbBleConfig (без WiFi-стека)
}

void loop() {
    // TODO: Zigbee loop + BLE loop
}
