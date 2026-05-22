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
├── c6-zigbee/           ← ESP32-C6 Zigbee+BLE (в разработке, работает)
└── h2/                  ← ESP32-H2 Zigbee+BLE батарейный (запланировано)
```

## Активное устройство: c6-wifi (Ворота цех)

- **Чип:** ESP32-C6FH4, COM10, MAC `58:E6:C5:18:FC:C8`, Device ID `esp32-FCC8`
- **IP:** `192.168.1.41`, mDNS: `http://mpcb-FCC8.local`
- **GPIO4** — реле Gate, **GPIO3** — кнопка (INPUT_PULLUP), **GPIO8** — WS2812 статус
- **Лог:** `curl http://192.168.1.41/api/log-text`
- **Прошивка (OTA):** `http://192.168.1.41/ota` — загрузить `.pio\build\c6-wifi\firmware.bin` через браузер
- **Прошивка (USB):** `pio run -e c6-wifi --target upload --upload-port COM10`

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

> ⚠ VL53L0X (Pololu) после soft reset: `init()` падает в `getSpadInfo()` — NVM hardware не сбрасывается 0xBF. `getSpadInfo()` при timeout НЕ восстанавливает регистры (оставляет `0xFF=0x07, 0x81=0x01, 0x80=0x01` в private-mode). warmStart в PeriphManager сначала применяет cleanup (зеркало строк 905–912 VL53L0X.cpp), затем вручную дописывает StaticInit + RefCalibration через публичный API.

> ⚠ Порядок запуска в main.cpp: `iot.begin()` первым (BLE стартует и нарушает GPIO mux), затем `pm.begin()` (bus recovery + Wire.begin(400kHz) + инициализация сенсоров). Обратный порядок ломает I2C: Wire.end() внутри resetI2C() прерывает работающий VL53L1X startContinuous, шина залипает. После pm.begin() нужен явный `pm.onMqttConnected()` если `iot.state() == RUNNING`.

> ⚠ Wire.end() нельзя вызывать внутри _initPeriph отдельных устройств — прерывает уже инициализированные. Bus recovery и Wire.begin(400kHz) — один раз в pm.begin() перед всеми _initPeriph.

> ⚠ Wire.end() НИКОГДА не вызывать в loop() — прерывает работающий VL53 continuous mode, после этого VL53 стабильно возвращает invalid (mm=65535 / status=99). Bus recovery с Wire.end() допустима только в pm.begin().

> ⚠ VL53 (L0X/L1X) `init()` имеет внутренние I2C таймауты (часть getSpadInfo / setVcselPulsePeriod / performSingleRefCalibration), которые оставляют SDA залипшим. Если сразу после init() запустить startContinuous и попытаться читать другие I2C устройства (AHT10) — получим err=5. Архитектура в PeriphManager: `_initPeriph` НЕ вызывает startContinuous; после цикла всех инитов в `pm.begin()` делается одна bus recovery (9 SCL + Wire.end() + Wire.begin(400kHz)), и только потом для всех инициализированных VL53 вызывается startContinuous. В loop() bus recovery не нужна.

> ⚠ VL53L0X Long Range mode (`setSignalRateLimit(0.1f)` + `setVcselPulsePeriod(PreRange,18)` + `setVcselPulsePeriod(FinalRange,14)` + `setMeasurementTimingBudget(200000)`) — на ESP32-C6 эти вызовы дают каскад I2C таймаутов (~6 секунд) и оставляют сенсор в полу-сконфигурированном состоянии: continuous mode даёт mm=65535 постоянно. Использовать дефолтные настройки: `setMeasurementTimingBudget(33000)` + `startContinuous(35)`.

> ⚠ VL53L1X status=99 (RANGESTATUS_NONE_RETURN) — нет отражённого сигнала, цель вне зоны видимости. Не ошибка. Фильтр: `!timeoutOccurred() && mm > 0 && mm < 8190`.

## Библиотека mpcb-iot-core

Подключена как git submodule в `lib/mpcb-iot-core`.
`lib_extra_dirs = lib` — правки в библиотеке видны сразу на следующем билде без `pio pkg update`.

Ключевые файлы (пути от корня репо):
- `lib/mpcb-iot-core/src/MpcbIotCore.h/.cpp` — WiFi, MQTT, BLE, FSM
- `lib/mpcb-iot-core/src/peripheral/PeriphManager.h/.cpp` — GPIO, rules engine, MQTT
- `lib/mpcb-iot-core/src/neopixel/WS2812Strip.h/.cpp` — IDF5 RMT драйвер WS2812, без внешних библиотек
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
Датчики (dht22, ds18b20, aht10, vl53l0, vl53l1) НЕ публикуют state при старте — только из loop после первого чтения.

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
| `neopixel` | WS2812 лента, IDF5 RMT, до 300–500 LED, 8 эффектов | state: `{"on":bool,"effect":"...","r":N,"g":N,"b":N,"brightness":N,"pixels":N}` |
| `dht22` | Temp+humidity, 30с, `__has_include<DHT.h>` | `{"temp":float,"humidity":float}` |
| `ds18b20` | Температура, 30с, `__has_include<DallasTemperature.h>` | `{"temp":float}` |
| `aht10` | I2C temp+hum, 30с, `__has_include<Adafruit_AHTX0.h>`, адреса 0x38/0x39 | `{"temp":float,"humidity":float}` |
| `vl53l0` | VL53L0X ToF дистанция мм, 500мс, Pololu lib, адрес 0x29 | `{"distance": int}` |
| `vl53l1` | VL53L1X ToF дистанция мм, 500мс, Pololu lib, адрес 0x29 | `{"distance": int}` |
| `ccs811` | TVOC+eCO2, 10с, нативный I2C (без библиотеки), адрес 0x5A/0x5B | `{"eco2": int, "tvoc": int}` |
| `pcf_relay` | PCF8574 выход | `{"on": bool}` |
| `pcf_button` | PCF8574 вход | `{"pressed": bool}` |

VL53: два отдельных типа в UI — `vl53l0` (VL53L0X) и `vl53l1` (VL53L1X). Оба живут на адресе 0x29, подключать по одному.
**Не использовать Adafruit_VL53L0X** — вызывает Wire.begin() внутри, ломает I2C на ESP32-C6.
VL53L1X: `Wire.setClock(400000)` + `setMeasurementTimingBudget(200000)` + `startContinuous(210)` — 200мс бюджет нужен для надёжного измерения на дистанции 1.5м+ (50мс даёт SignalFail). Чтение: `read(true)` блокирующий + `!timeoutOccurred() && mm > 0 && mm < 8190`.

### Dashboard (веб UI)
`GET /dash` — страница с live-состоянием всех периферий, обновление каждые 2с.
- Реле: ВКЛ/ВЫКЛ + кнопка управления прямо из браузера (без MQTT)
- Кнопки: индикатор нажата/отпущена
- Датчики: числовые значения (temp, humidity, distance, converted)
- Neopixel: цветной квадратик + имя эффекта; управление — color picker, слайдер яркости, dropdown эффектов, кнопка 🌈 (rainbow тест)
- Проводка: `iot.onDashState(cb)` + `iot.onDashCmd(cb)` в main.cpp

### Rules engine
- Кнопки/pcf_button: `pressed` / `released` / `any` → `on` / `off` / `toggle` / `pulse`
- Датчики: `temp_above` / `temp_below` / `hum_above` / `hum_below` / `above` / `below`
  с порогом (float) и гистерезисным лэтчем (re-arms когда условие перестаёт выполняться)

### GPIO конструктор веб UI
- Dropdown пинов (forbidden/warn/safe)
- I2C типы: per-type адреса + подсказка `SDA→19  SCL→18`
  - aht10: 0x38, 0x39
  - vl53l0, vl53l1: 0x29
  - pcf8574: 0x20–0x27
- Автопереключение pin↔addr при смене типа (в т.ч. I2C→I2C)
- Per-type limits (UI + серверная валидация):
  relay:8, button:8, analog:4, pwm:4, neopixel:2, dht22:2, ds18b20:2, aht10:2, vl53l0:1, vl53l1:1, pcf8574:2
- Кнопка "Перезагрузить" + хинт "Изменения требуют перезагрузки"

### Rules UI
- trigger = только входы (button/analog/dht22/ds18b20/aht10/vl53l0/vl53l1)
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
// MpcbZigbeeCore : public ITransport  ← C6/H2 Zigbee ✅ реализовано
// PeriphManager принимает ITransport& — работает с любым транспортом ✅
```

## Flash/RAM бюджет (c6-wifi, актуально 2026-05)

Замеры: WiFi+BLE+MQTT+AHT10+VL53L0X(Pololu)+VL53L1X(Pololu)+PCF8574+rules+analog_cal+dashboard+ITransport+WS2812Strip+neopixel_effects
```
Flash: 88.6%  (1626 КБ из 1835 КБ)  — свободно ~209 КБ
RAM:   19.4%  (63 КБ из 320 КБ)     — свободно ~257 КБ
```

Крупнейшие константы в Flash:
| Символ | Размер |
|--------|--------|
| `_handleGpio` inline JS | ~12.9 КБ |
| `_handleDash` inline JS | ~3.5 КБ |
| `PORTAL_HTML` | 3.4 КБ |
| `CONFIG_CSS` | 3.2 КБ |

**Критическая отметка — 90% (1651 КБ).** До неё ~25 КБ.

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
Реализован `MpcbZigbeeCore` — подключается без изменений в PeriphManager.

### ✅ NeoPixel лента — РЕАЛИЗОВАНА

Тип `neopixel` — полноценный драйвер адресных лент WS2812/WS2812B.

**Драйвер:** `WS2812Strip` — собственная реализация, без внешних библиотек.
Использует ESP-IDF 5 RMT hardware (`driver/rmt_tx.h` + `driver/rmt_encoder.h`),
GRB byte order, аппаратный DMA, работает на C3/C6 и любом ESP32-IDF5.
В `main.cpp`: `#include <neopixel/WS2812Strip.h>` (не `<WS2812Strip.h>` — лежит в подкаталоге).
Статусный LED GPIO8 в c6-wifi/main.cpp также переведён на WS2812Strip (убрана Adafruit).

**Эффекты (8 штук):**
| Эффект | Описание | `neoSpeed` |
|--------|----------|-----------|
| `off` | Выключено | — |
| `static` | Статический цвет | — |
| `blink` | Мигание (сигнализация) | интервал вкл/выкл, мс |
| `breathe` | Плавное дыхание | период полного цикла, мс |
| `rainbow` | Радуга по всей ленте | шаг каждые N мс |
| `strobe` | Строб (быстрое мигание) | интервал, мс |
| `sunrise` | Рассвет (256 шагов тёмно-красный → тёплый белый → STATIC) | шаг каждые N мс |
| `wipe` | Последовательное заполнение | интервал на пиксель, мс |

**MQTT set:** `{"effect":"blink","r":255,"g":0,"b":0,"speed":400,"count":10,"brightness":200}`
- `count`: количество повторений (-1 = бесконечно)
- `{"on":false}` — выключить
**MQTT state:** `{"on":bool,"effect":"blink","r":255,"g":0,"b":0,"brightness":200,"pixels":30}`

**Конфигурация:** `pixelCount` — количество LED в ленте (UI-поле + хранится в NVS).
Практический потолок: ~300–500 LED (show() блокирует ~30 мкс × N; 300 LED = 9 мс).
Лимит в UI: 2 neopixel периферии на устройство.

### ✅ VL53 — РЕАЛИЗОВАН (L0X и L1X)

Два явных типа в UI: `vl53l0` и `vl53l1` — пользователь выбирает нужный чип.
**Не использовать Adafruit_VL53L0X** — вызывает Wire.begin() внутри begin(), ломает I2C на ESP32-C6. Использовать Pololu (`pololu/vl53l0x-arduino`, `pololu/vl53l1x-arduino`).
L0X и L1X живут на одном адресе 0x29 — подключать по одному.

**VL53L1X нюансы:**
- **Питание: модуль с onboard AMS1117-3.3 требует +5V** (не 3.3V от ESP) — AMS1117 нужен dropout ≥1В, от 3.3V показания плавают. Проверено: с 5V стабильность ±5мм.
- После ESP.restart() нужен soft-reset сенсора (0x0000 ← 0x00, затем 0x01) перед init(), иначе MODEL_ID = 0x00
- `Wire.setClock(400000)` обязателен — без него L1X работает нестабильно
- Бюджет 50мс → SignalFail на дистанции 1.5м+; использовать 200мс + startContinuous(210)
- Чтение: `read(true)` блокирующий, фильтр: `!timeoutOccurred() && mm > 0 && mm < 8190`

### OTA via MQTT (ждём сервер)

Сервер пришлёт `{"cmd":"ota","url":"https://..."}` через MQTT.
`HTTPClient` скачивает `.bin`, `Update.h` прошивает.

### ✅ C6 Zigbee — РАБОТАЕТ

`c6-zigbee/` — ESP32-C6 Zigbee End Device + BLE конфигуратор.

- **Транспорт:** `MpcbZigbeeCore : public ITransport` → PeriphManager работает без изменений
- **Эндпоинты:** создаются из NVS при старте (`_createEndpoints()`); relay → `ZigbeeLight`, temp → `ZigbeeTempSensor`
- **BLE:** конфигуратор + OTA; коэкзистенция с Zigbee через `updateConnParams(handle, 80, 160, 0, 600)` (100–200ms интервал, 6s timeout)
- **Ключи периферии:** если приложение не передаёт `key` — деривируется из `label` через `_sanitize` (ArduinoJson 7 возвращает строку `"null"` для отсутствующего поля)
- **LED статус:** зелёный = Zigbee joined + в сети Z2M, синий мигает = ищет сеть
- **Сброс Zigbee:** удержать BOOT (GPIO9) 3 секунды → `Zigbee.factoryReset()` → устройство выходит из сети и ищет новую

**Партиции:** `c6-zigbee/partitions_8mb_zb.csv` — слоты по 1.75MB (firmware ~1.6MB).

### Новые устройства

| Устройство | Трудозатраты | Основная работа |
|------------|-------------|-----------------|
| C3 (WiFi) | ~полдня | Пиноут C3 в ConfigServer |
| H2 (battery) | ~3 нед. | PowerManager deep sleep + MpcbZigbeeCore |

### Flutter app (`e:/Projects/mpcb-app`)
- GPIO экран: тумблеры реле, значения датчиков
- Rules экран
- Маркер PS→MC

## Zigbee2MQTT — добавление новых устройств

### Инфраструктура

- **Z2M на сервере:** Docker, Portainer, `/home/vit/zigbee2mqtt/data` → `/app/data` внутри контейнера
- **Донгл:** вставлен в сервер (не в локальную машину)
- **Разрешить сопряжение:** Z2M UI → "Разрешить подключение" (permit join)
- **Сброс устройства:** удержать BOOT 3 сек → `Zigbee.factoryReset()`, потом permit join

### Конвертеры Z2M

Каждое новое устройство с уникальным `modelID` / `manufacturerName` нужно добавить в Z2M, иначе показывает "не поддерживается: generated".

**Путь к внешним конвертерам на сервере:** `/home/vit/zigbee2mqtt/data/external_converters/`

**Формат файла** (`.mjs`):
```javascript
import {onOff} from 'zigbee-herdsman-converters/lib/modernExtend';

export default {
    zigbeeModel: ['mpcb-relay'],   // modelID из прошивки (setManufacturerAndModel)
    model: 'mpcb-relay',
    vendor: 'mpcbstudio',
    description: 'MPCB Smart Relay',
    extend: [onOff({powerOnBehavior: false})],
};
```

**Готовые конвертеры:** `D:\Projects\mpcb-app\zigbee2mqtt\`
- `mpcb_relay.mjs` — реле (OnOff)

**Деплой нового конвертера:**
```bash
# На сервере по SSH:
mkdir -p /home/vit/zigbee2mqtt/data/external_converters
cp mpcb_relay.mjs /home/vit/zigbee2mqtt/data/external_converters/
docker restart zigbee2mqtt
# Проверить: в логах Z2M появится "Loaded external converters: mpcb_relay.mjs"
```

### Официальный PR в zigbee-herdsman-converters

Все устройства mpcbstudio добавлены в официальный репозиторий:
- **Файл:** `src/devices/mpcbstudio.ts` (TypeScript, `DefinitionWithExtend[]`, fingerprint по modelID+manufacturerName)
- **Форк:** `github.com/remixvit/zigbee-herdsman-converters`, ветка `feat/add-mpcbstudio-devices`
- **PR:** `https://github.com/Koenkk/zigbee-herdsman-converters/pull/12277`

**Как добавить новое устройство в PR:**
1. Открыть форк: `https://github.com/remixvit/zigbee-herdsman-converters`
2. Обновить `src/devices/mpcbstudio.ts` — добавить новый блок в `definitions[]`
3. `modernExtend` модули: `m.onOff()`, `m.temperature()`, `m.humidity()`, `m.illuminance()` и др.
4. Создать новый PR или обновить существующий

**Доступные modernExtend модули** (из `zigbee-herdsman-converters/lib/modernExtend`):
| Функция | Кластер | Применение |
|---------|---------|-----------|
| `m.onOff()` | OnOff | реле |
| `m.temperature()` | Temp Measurement | датчик температуры |
| `m.humidity()` | Relative Humidity | датчик влажности |
| `m.illuminance()` | Illuminance Measurement | датчик освещённости |
| `m.occupancy()` | Occupancy Sensing | датчик движения |

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
