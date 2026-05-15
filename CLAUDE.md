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

## Сборка и прошивка

```powershell
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

## ESP32-C6 Super Mini — пины

| Категория | Пины | Причина |
|-----------|------|---------|
| ❌ Forbidden | 12 (USB−), 13 (USB+), 18 (Flash), 19 (Flash) | Нельзя использовать |
| ⚠ Warn | 4–7 (JTAG), 8 (WS2812), 9 (BOOT), 15 (LED) | Осторожно |
| ✅ Safe | 0, 1, 2, 3 (ADC), 14, 20 (RX), 21 (TX), 22 (SDA), 23 (SCL) | Свободно |

**I2C шина:** SDA=GPIO22, SCL=GPIO23 (фиксировано — `Wire.begin(22, 23)`)

## Библиотека mpcb-iot-core

Подключена как git submodule в `lib/mpcb-iot-core`.
`lib_extra_dirs = lib` в platformio.ini — изменения в библиотеке видны сразу на следующем билде, без `pio pkg update`.

Ключевые файлы:
- `lib/mpcb-iot-core/src/MpcbIotCore.h/.cpp` — WiFi, MQTT, BLE, FSM
- `lib/mpcb-iot-core/src/peripheral/PeriphManager.h/.cpp` — GPIO, rules, MQTT
- `lib/mpcb-iot-core/src/web/ConfigServer.cpp` — веб-интерфейс (тёмная тема, #7b2feb accent)
- `lib/mpcb-iot-core/src/storage/ConfigStorage.h/.cpp` — NVS хранилище
- `lib/mpcb-iot-core/src/log/RingLog.h/.cpp` — кольцевой лог (60 записей, /api/log-text)

## Что реализовано в mpcb-iot-core ✅

- WiFi connect + AP portal (captive, no-reboot flow)
- MQTT: LWT, announce, config, state publish, +/set subscribe
- BLE provisioning (NimBLE-Arduino v2): WiFi, MQTT, GPIO, Rules
- OTA через BLE и через веб (HTTP upload .bin)
- ConfigServer (веб UI): Device, WiFi, MQTT, GPIO, Logs, OTA
- mDNS `http://mpcb-XXXX.local` + `/api/reboot`
- **PeriphManager — типы периферии:**
  - `relay` — цифровой выход ON/OFF/pulse
  - `button` — цифровой вход (30мс debounce)
  - `analog` — ADC, публикует каждые 10с
  - `pwm` — ШИМ 0–255
  - `neopixel` — WS2812 ack-only
  - `dht22` — temp+humidity каждые 30с (auto-detect `__has_include<DHT.h>`)
  - `ds18b20` — температура каждые 30с (auto-detect `__has_include<DallasTemperature.h>`)
  - `aht10` — I2C temp+humidity каждые 30с (auto-detect `__has_include<Adafruit_AHTX0.h>`)
    адреса 0x38/0x39
  - `vl53` — ToF дистанция мм каждые 500мс (auto-detect L0X и/или L1X)
    Если оба в lib_deps — runtime detection: сначала пробует L1X, fallback L0X
    Публикует `{"distance": mm}`, правила `above`/`below`, адрес 0x29
- **Rules engine:**
  - Кнопки: `pressed` / `released` / `any` → `on` / `off` / `toggle` / `pulse`
  - Датчики: `temp_above` / `temp_below` / `hum_above` / `hum_below` / `above` / `below`
    с порогом (float) и гистерезисным лэтчем
- **GPIO конструктор веб UI:**
  - Dropdown пинов ESP32-C6 Super Mini (forbidden/warn/safe)
  - I2C типы: per-type адреса (aht10: 0x38/0x39, vl53: 0x29, pcf8574: 0x20-0x23) + подсказка `SDA→22 SCL→23`
  - Автопереключение pin↔addr при смене типа (в т.ч. I2C→I2C)
  - Per-type limits: relay:8, button:8, analog:4, pwm:4, neopixel:2, dht22:2, ds18b20:2, aht10:2, vl53:1, pcf8574:2
- **Rules UI:** trigger=входы (button/analog/dht22/ds18b20/aht10/vl53), target=выходы (relay/pwm/neopixel/pcf8574)
- Нормализация ключей: `_sanitize()` оставляет только `[a-z0-9]`

## MQTT протокол

**Брокер:** `mqtt.mpcbstudio.com:8883` TLS
**Спецификация:** `github.com/remixvit/mpcbstudio-api` → `FIRMWARE_SPEC.md`

## Flash/RAM бюджет (c6-wifi, актуально на 2026-05)

Замеры: WiFi+BLE+MQTT+AHT10+VL53L0X+VL53L1X+rules
```
Flash: 87.2%  (1563 КБ из 1835 КБ)  — свободно ~272 КБ
RAM:   18.3%  (60 КБ из 320 КБ)     — свободно ~267 КБ
```
**Критическая отметка — 90% (1651 КБ).** До неё ~88 КБ.

После PCF8574 (~8 КБ) ожидаемо ~87.7%. Запас есть.
Gzip для CSS+JS (~10 КБ экономии) — откладываем до 89%+.

## Роадмап

### Следующий — PCF8574

`pcf_relay` и `pcf_button` — каждый пин отдельная запись в плоском списке.
Адреса 0x20-0x27, channel 0-7 в struct Peripheral. MAX_PERIPHERALS 12→24.
Подробная архитектура — в старом CLAUDE.md из esp32-mpcb.

### После PCF8574

- **Аналоговая калибровка**: calMode 0=raw, 1=linear, 2=thermistor NTC
- **ITransport абстракция**: разделить MpcbIotCore от PeriphManager для Zigbee
- **OTA via MQTT**: сервер пришлёт `{"cmd":"ota","url":"..."}` через MQTT
- **c3**: PowerManager deep sleep, тот же набор сенсоров
- **c6-zigbee / h2**: ESP Zigbee SDK + ITransport

## Как обновить submodule (библиотеку)

```powershell
# Правим библиотеку напрямую в lib/mpcb-iot-core, затем:
cd lib/mpcb-iot-core
git add -A; git commit -m "feat: ..."; git push

# Обновляем указатель в monorepo
cd ../..
git add lib/mpcb-iot-core
git commit -m "chore: update mpcb-iot-core"
git push
```

> Правки в `lib/mpcb-iot-core` сразу видны в билде — не нужно пушить и обновлять.
> Пуш нужен только чтобы зафиксировать коммит в GitHub и обновить указатель в monorepo.

## Известные проблемы сервера

| Проблема | Статус |
|----------|--------|
| Дублирующиеся устройства | Серверный баг: INSERT вместо UPSERT. Ждём фикса. |
| Capabilities пустые | Сервер добавит автозаполнение из peripherals[]. |
