# mpcb-devices — контекст для Claude Code

## Что это

Монорепо всех ESP32 устройств на платформе mpcbstudio.
Единый `platformio.ini`, каждая плата — своя папка.

```
mpcb-devices/
├── lib/mpcb-iot-core/   ← git submodule (github.com/remixvit/mpcb-iot-core)
├── c6-wifi/             ← ESP32-C6 WiFi+BLE, рабочее устройство "Ворота цех"
├── c3/                  ← ESP32-C3 WiFi+BLE+DeepSleep (в разработке)
├── c6-zigbee/           ← ESP32-C6 Zigbee+BLE (запланировано)
└── h2/                  ← ESP32-H2 Zigbee+BLE батарейный (запланировано)
```

## Сборка и прошивка

```bash
pio run -e c6-wifi                        # сборка
pio run -e c6-wifi --target upload        # прошивка
pio run                                   # все платы сразу
```

## Первый запуск на новой машине

```bash
git clone --recurse-submodules https://github.com/remixvit/mpcb-devices
cd mpcb-devices
pio run -e c6-wifi --target upload
```

> Если забыл `--recurse-submodules`: `git submodule update --init`

## Активное устройство: c6-wifi (Ворота цех)

- **Чип:** ESP32-C6FH4, COM10
- **MAC:** `58:E6:C5:18:FC:C8`, Device ID: `esp32-FCC8`
- **IP:** `192.168.1.41`, mDNS: `http://mpcb-FCC8.local`
- **GPIO4** — реле Gate, **GPIO3** — кнопка, **GPIO8** — WS2812 статус
- **Лог:** `curl http://192.168.1.41/api/log-text`
- **MQTT credentials в NVS:** `user_4` / `DATeyjWZ6sd9N-5wFtj5vg`
  - Обновить через `http://192.168.1.41/mqtt` если изменились

## Библиотека mpcb-iot-core

Подключена как git submodule в `lib/mpcb-iot-core`.
`lib_extra_dirs = lib` в platformio.ini — путь относительный, работает на любой машине.

Ключевые файлы библиотеки:
- `lib/mpcb-iot-core/src/MpcbIotCore.h/.cpp` — WiFi, MQTT, BLE, FSM
- `lib/mpcb-iot-core/src/peripheral/PeriphManager.h/.cpp` — GPIO, rules, MQTT
- `lib/mpcb-iot-core/src/web/ConfigServer.cpp` — веб-интерфейс (тёмная тема, #7b2feb)

## MQTT протокол

**Брокер:** `mqtt.mpcbstudio.com:8883` TLS
**Спецификация:** `github.com/remixvit/mpcbstudio-api` → `FIRMWARE_SPEC.md`

Последовательность при старте:
```
announce → subscribe mpcb/devices/{id}/+/set → config → {key}/state
```

## Известные проблемы

| Проблема | Статус |
|----------|--------|
| Дублирующиеся устройства на сервере | Серверный баг: INSERT вместо UPSERT по device_id. Ждём фикса. |
| Capabilities пустые в кабинете | Сервер добавит автозаполнение из peripherals[] в config. |

## План развития

### Ждём от сервера
- Upsert по `device_id` при получении config топика
- Автозаполнение capabilities/properties из peripherals[]

### Следующая разработка
- **Flutter app** (`github.com/remixvit/mpcb-app`): GPIO экран, Rules экран
- **c3:** PowerManager deep sleep, DHT22/DS18B20 полная реализация
- **c6-zigbee / h2:** интеграция ESP Zigbee SDK

## Как обновить submodule (библиотеку)

```bash
cd lib/mpcb-iot-core
git pull origin master
cd ../..
git add lib/mpcb-iot-core
git commit -m "chore: update mpcb-iot-core"
git push
```
