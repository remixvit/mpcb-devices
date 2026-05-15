# mpcb-devices — контекст для Claude Code

## Что это

Монорепо всех ESP32 устройств на платформе mpcbstudio.
Единый `platformio.ini`, каждая плата — своя папка.
Библиотека `mpcb-iot-core` — git submodule в `lib/`.

```
mpcb-devices/
├── lib/mpcb-iot-core/   ← git submodule (github.com/remixvit/mpcb-iot-core)
├── c6-wifi/             ← ESP32-C6 WiFi+BLE, рабочее устройство "Ворота цех"
├── c3/                  ← ESP32-C3 WiFi+BLE+DeepSleep (в разработке)
├── c6-zigbee/           ← ESP32-C6 Zigbee+BLE (запланировано)
└── h2/                  ← ESP32-H2 Zigbee+BLE батарейный (запланировано)
```

## Активное устройство: c6-wifi (Ворота цех)

- **Чип:** ESP32-C6FH4, COM10, MAC `58:E6:C5:18:FC:C8`, Device ID `esp32-FCC8`
- **IP:** `192.168.1.41`, mDNS: `http://mpcb-FCC8.local`
- **GPIO4** — реле Gate, **GPIO3** — кнопка (INPUT_PULLUP), **GPIO8** — WS2812 статус
- **Лог:** `curl http://192.168.1.41/api/log-text`
- **Прошивка (OTA):** `http://192.168.1.41/ota` — загрузить `.pio\build\c6-wifi\firmware.bin` через браузер
- **Прошивка (USB):** COM10 сейчас недоступен (повреждён при обратной полярности VL53)

> ⚠ Пока COM10 не работает — собирать `pio run -e c6-wifi`, затем заливать через OTA веб-интерфейс.

## Сборка и прошивка

```powershell
pio run -e c6-wifi                          # сборка
pio run -e c6-wifi --target upload          # прошивка
pio run                                     # все платы сразу
```

## Первый запуск на новой машине

```powershell
git clone --recurse-submodules https://github.com/remixvit/mpcb-devices
cd mpcb-devices
pio run -e c6-wifi --target upload
```

> Если забыл `--recurse-submodules`: `git submodule update --init`

## ESP32-C6 Super Mini — пины

ESP32-C6**FH4** — встроенный (embedded) flash внутри чипа, GPIO18/19 физически свободны.

| Категория | Пины | Причина |
|-----------|------|---------|
| ❌ Forbidden | 12 (USB−), 13 (USB+) | Нельзя использовать |
| ⚠ Warn | 4–7 (JTAG), 8 (WS2812), 9 (BOOT), 15 (LED) | Осторожно |
| ✅ Safe | 0, 1, 2, 3 (ADC), 14, 18 (SCL), 19 (SDA), 20 (RX), 21 (TX) | Свободно |
| ⚠ Alt | 22 (SDA alt), 23 (SCL alt) | На хедере неудобны (у USB разъёма) |

**I2C шина:** SDA=GPIO19, SCL=GPIO18 (правый хедер, макетка — `Wire.end(); Wire.begin(19, 18)`)

> ⚠ На ESP32-C6 повторный `Wire.begin()` без `Wire.end()` не переключает GPIO mux — шина не работает (линии не дёргаются, scan instant). Всегда `Wire.end()` перед `Wire.begin()`.

> ⚠ VL53L1X после `startContinuous()` при soft reset (ESP.restart()) требует soft-reset сенсора (запись 0x00/0x01 в регистр 0x0000) перед повторным `init()`, иначе MODEL_ID регистр 0x010F возвращает 0x00 вместо 0xEA.

> ⚠ I2C init в PeriphManager::begin() делает bus recovery (9 тактов SCL) перед Wire.begin() — это нужно для корректного переключения GPIO mux после soft reset.

## Библиотека mpcb-iot-core

Подключена как git submodule в `lib/mpcb-iot-core`.
`lib_extra_dirs = lib` — правки в библиотеке видны сразу на следующем билде без `pio pkg update`.

Ключевые файлы (пути от корня репо):
- `lib/mpcb-iot-core/src/MpcbIotCore.h/.cpp` — WiFi, MQTT, BLE, FSM
- `lib/mpcb-iot-core/src/peripheral/PeriphManager.h/.cpp` — GPIO, rules engine, MQTT
- `lib/mpcb-iot-core/src/web/ConfigServer.cpp` — веб-интерфейс (тёмная тема, #7b2feb accent)
- `lib/mpcb-iot-core/src/storage/ConfigStorage.h/.cpp` — NVS хранилище
- `lib/mpcb-iot-core/src/log/RingLog.h/.cpp` — кольцевой лог (60 записей, /api/log-text)

## MQTT протокол

**Брокер:** `mqtt.mpcbstudio.com:8883` TLS
**Credentials в NVS:** `user_4` / `DATeyjWZ6sd9N-5wFtj5vg`
**Спецификация:** `github.com/remixvit/mpcbstudio-api` → `FIRMWARE_SPEC.md`

Последовательность при старте:
```
announce → subscribe mpcb/devices/{id}/+/set → config → {key}/state
```
Датчики (dht22, ds18b20, aht10, vl53) НЕ публикуют state при старте — только из loop после первого чтения.

Топики:
- `mpcb/devices/{id}/announce` — LWT + старт
- `mpcb/devices/{id}/config` — JSON с peripherals[], retain=true
- `mpcb/devices/{id}/{key}/state` — состояние периферии, retain=true
- `mpcb/devices/{id}/{key}/set` — команды от сервера/приложения

## Что реализовано в mpcb-iot-core ✅

### WiFi + AP Portal
AP portal (captive): после ввода WiFi ESP коннектится в фоне (AP+STA режим), показывает IP,
кнопка "Закрыть AP" запускает ConfigServer без ребута.
ESP32-C6 не умеет сканировать в чистом AP режиме — сканируем сети ДО старта AP.

### MQTT
LWT, announce, config, state publish, +/set subscribe, TLS.

### BLE provisioning (NimBLE-Arduino v2)
Настройка WiFi, MQTT, GPIO, Rules через BLE. OTA через BLE.

### OTA
Через BLE и через веб-интерфейс (HTTP upload .bin).

### ConfigServer (веб UI)
`http://mpcb-XXXX.local` — Device, WiFi, MQTT, GPIO, Logs, OTA, Dashboard. Тёмная тема.

Эндпоинты:
- `GET  /dash`        — Dashboard: live-состояние всех периферий + управление реле
- `GET  /api/state`   — JSON массив состояний всех периферий (для Dashboard)
- `POST /api/cmd`     — Команда периферии `{"key":"gate","payload":"{\"on\":true}"}`
- `GET  /api/i2c-scan[?sda=N&scl=N]` — Сканирование I2C шины, список устройств
- `GET  /api/status`  — WiFi/MQTT статус
- `GET  /api/log-text` — Лог устройства (plain text)

### PeriphManager — типы периферии

| Тип | Описание | MQTT payload |
|-----|----------|--------------|
| `relay` | Цифровой выход ON/OFF/pulse | `{"on": bool}` |
| `button` | Цифровой вход, 30мс debounce | `{"pressed": bool}` |
| `analog` | ADC, каждые 10с | `{"value": int, "voltage": float}` |
| `pwm` | ШИМ 0–255 | `{"duty": int}` |
| `neopixel` | WS2812 ack-only | `{"r":int,"g":int,"b":int}` |
| `dht22` | Temp+humidity, 30с, `__has_include<DHT.h>` | `{"temp":float,"humidity":float}` |
| `ds18b20` | Температура, 30с, `__has_include<DallasTemperature.h>` | `{"temp":float}` |
| `aht10` | I2C temp+hum, 30с, `__has_include<Adafruit_AHTX0.h>`, адреса 0x38/0x39 | `{"temp":float,"humidity":float}` |
| `vl53` | ToF дистанция мм, 500мс, auto-detect L0X/L1X, адрес 0x29 | `{"distance": int}` |
| `pcf_relay` | PCF8574 выход | `{"on": bool}` |
| `pcf_button` | PCF8574 вход | `{"pressed": bool}` |

VL53: если оба lib (L0X и L1X) скомпилированы — runtime detection: пробует L1X, fallback L0X.

### Dashboard (веб UI)
`GET /dash` — страница с live-состоянием всех периферий, обновление каждые 2с.
- Реле: ВКЛ/ВЫКЛ + кнопка управления прямо из браузера (без MQTT)
- Кнопки: индикатор нажата/отпущена
- Датчики: числовые значения (temp, humidity, distance, converted)
- Проводка: `iot.onDashState(cb)` + `iot.onDashCmd(cb)` в main.cpp

### Rules engine
- Кнопки/pcf_button: `pressed` / `released` / `any` → `on` / `off` / `toggle` / `pulse`
- Датчики: `temp_above` / `temp_below` / `hum_above` / `hum_below` / `above` / `below`
  с порогом (float) и гистерезисным лэтчем (re-arms когда условие перестаёт выполняться)

### GPIO конструктор веб UI
- Dropdown пинов (forbidden/warn/safe)
- I2C типы: per-type адреса + подсказка `SDA→19  SCL→18`
  - aht10: 0x38, 0x39
  - vl53: 0x29
  - pcf8574: 0x20–0x27
- Автопереключение pin↔addr при смене типа (в т.ч. I2C→I2C)
- Per-type limits (UI + серверная валидация):
  relay:8, button:8, analog:4, pwm:4, neopixel:2, dht22:2, ds18b20:2, aht10:2, vl53:1, pcf8574:2
- Кнопка "Перезагрузить" + хинт "Изменения требуют перезагрузки"

### Rules UI
- trigger = только входы (button/analog/dht22/ds18b20/aht10/vl53)
- target = только выходы (relay/pwm/neopixel/pcf_relay)
- события и поле порога меняются динамически по типу триггера

### Нормализация ключей
`_sanitize()` оставляет только `[a-z0-9]` — пробелы/дефисы удаляются, не конвертируются в `_`.
JS `san()` синхронизирован. **Breaking:** старые метки с пробелами дадут другой MQTT-ключ.

## Архитектура → многочиповость ✅

```cpp
// РЕАЛИЗОВАНО: ITransport в src/ITransport.h
class ITransport {
    virtual bool publish(const String& topic, const String& payload, bool retain) = 0;
    virtual bool subscribe(const String& topic) = 0;
};
// MpcbIotCore    : public ITransport  ← C6 WiFi  ✅ реализовано
// MpcbZigbeeCore : public ITransport  ← C6/H2 Zigbee (будущее)
// PeriphManager принимает ITransport& — работает с любым транспортом ✅
```

## Flash/RAM бюджет (c6-wifi, актуально 2026-05)

Замеры: WiFi+BLE+MQTT+AHT10+VL53L0X+VL53L1X+PCF8574+rules+analog_cal+dashboard+ITransport
```
Flash: 88.6%  (1625 КБ из 1835 КБ)  — свободно ~210 КБ
RAM:   19.2%  (63 КБ из 320 КБ)     — свободно ~254 КБ
```

Крупнейшие константы в Flash:
| Символ | Размер |
|--------|--------|
| `_handleGpio` inline JS | ~12.9 КБ |
| `_handleDash` inline JS | ~3.5 КБ |
| `PORTAL_HTML` | 3.4 КБ |
| `CONFIG_CSS` | 3.2 КБ |

**Критическая отметка — 90% (1651 КБ).** До неё ~32 КБ.

**Правило:** после каждого крупного добавления — `pio run -e c6-wifi`, зафиксировать % здесь.

Gzip для CSS+JS (~10 КБ экономии) — откладываем до 89%+. Подход: Python extra_scripts
сжимает при билде, отдаём с `Content-Encoding: gzip`.

## Роадмап

### ✅ PCF8574 — РЕАЛИЗОВАН

Два типа `pcf_relay` и `pcf_button`. Каждый пин PCF8574 — отдельная запись.

**Модель данных:**
```json
[
  {"type": "pcf_relay",  "i2cAddr": 32, "channel": 0, "label": "Relay1"},
  {"type": "pcf_relay",  "i2cAddr": 32, "channel": 1, "label": "Relay2"},
  {"type": "pcf_button", "i2cAddr": 32, "channel": 6, "label": "DoorSensor"}
]
```

**MQTT — идентично relay/button.** Сервер и Flutter работают без изменений.

### ✅ Аналоговая калибровка — РЕАЛИЗОВАНА

`calMode` + параметры в struct Peripheral.

| Режим | Параметры | Формула |
|-------|-----------|---------|
| `0` raw | — | `{"value":2048,"voltage":1.65}` |
| `1` linear | raw_min, raw_max, val_min, val_max, unit | линейная интерполяция |
| `2` thermistor | R_ref, Beta, R25 | Beta-уравнение NTC |

MQTT с калибровкой: `{"value":2048,"voltage":1.65,"converted":24.7,"unit":"°C"}`
Tare-offset: `{"tare":true}` / `{"tare_reset":true}` через MQTT set. Хранится отдельно в NVS (namespace `poffsets`).

### ✅ I2C сканер — РЕАЛИЗОВАН

`GET /api/i2c-scan[?sda=N&scl=N]` — сканирует адреса 1–127, возвращает найденные устройства с именами.
Кнопка "Сканировать шину" в нижней части страницы GPIO.

### ✅ Dashboard — РЕАЛИЗОВАН

`GET /dash` — live-состояние всех периферий, обновление каждые 2с.
Управление реле прямо из браузера через `/api/cmd`.

### ✅ ITransport абстракция — РЕАЛИЗОВАНА

`MpcbIotCore : public ITransport` (publish/subscribe override).
`PeriphManager::begin()` принимает `ITransport&` — не знает про WiFi/Zigbee/etc.
Будущий `MpcbZigbeeCore` подключается без изменений в PeriphManager.

### VL53L1X — ждём новый модуль

Старый модуль сгорел (обратная полярность). Soft-reset диагностика уже в коде.
После покупки — подключить, проверить что `modelId=0xEA` и `distance` публикуется.

### OTA via MQTT (ждём сервер)

Сервер пришлёт `{"cmd":"ota","url":"https://..."}` через MQTT.
`HTTPClient` скачивает `.bin`, `Update.h` прошивает.

### Новые устройства

| Устройство | Трудозатраты | Основная работа |
|------------|-------------|-----------------|
| C3 (WiFi) | ~полдня | Пиноут C3 в ConfigServer |
| C6 Zigbee | ~2 нед. | MpcbZigbeeCore + ITransport |
| H2 (battery) | ~3 нед. | + PowerManager deep sleep |

### Flutter app (`e:/Projects/mpcb-app`)
- GPIO экран: тумблеры реле, значения датчиков
- Rules экран
- Маркер PS→MC

## Как работать с библиотекой (submodule workflow)

```powershell
# Правим библиотеку напрямую — изменения сразу в следующем билде
cd lib/mpcb-iot-core
# ... редактируем файлы ...
git add -A
git commit -m "feat: ..."
git push

# Обновляем указатель submodule в monorepo
cd ../..
git add lib/mpcb-iot-core
git commit -m "chore: update mpcb-iot-core"
git push
```

## Известные проблемы сервера

| Проблема | Статус |
|----------|--------|
| Дублирующиеся устройства на сервере | INSERT вместо UPSERT по device_id. Ждём фикса. |
| Capabilities пустые в кабинете | Сервер добавит автозаполнение из peripherals[]. |
