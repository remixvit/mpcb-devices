# esp32-mpcb — Ворота цех

Прошивка для ESP32-C6 Super Mini на платформе mpcbstudio.
Построена на библиотеке [mpcb-iot-core](https://github.com/remixvit/mpcb-iot-core).

---

## Железо

| Компонент      | Пин   | Описание                          |
|----------------|-------|-----------------------------------|
| WS2812 RGB LED | GPIO8 | Встроенный статусный светодиод    |
| Реле (Gate)    | GPIO4 | Реле ворот, управляется по MQTT   |
| Кнопка         | GPIO3 | INPUT_PULLUP, дебаунс 10 мс       |

**Чип:** ESP32-C6FH4 (4 МБ flash)  
**MAC:** `58:E6:C5:18:FC:C8`  
**Device ID:** `esp32-FCC8`  
**IP:** `192.168.1.41`  
**mDNS:** `http://mpcb-FCC8.local`

---

## Индикация RGB LED

| Цвет     | Состояние                        |
|----------|----------------------------------|
| Синий    | Загрузка / подключение WiFi      |
| Жёлтый   | AP portal (нет WiFi)             |
| Голубой  | WiFi подключён, MQTT не готов    |
| 2× Зелёный пульс | MQTT подключён (RUNNING) |
| Зелёный мигает 1/3с | Heartbeat — всё ОК    |

---

## Настройка через веб-интерфейс

После первой прошивки (или если нет WiFi) — устройство поднимает AP `mpcb-FCC8`.
Подключись и открой `http://192.168.4.1`:

- `/` — имя устройства, статус
- `/wifi` — подключить к домашней сети
- `/mqtt` — настроить брокер
- `/gpio` — добавить/удалить периферию (relay, button, pwm, …)
- `/logs` — лог устройства в браузере
- `/ota` — обновление прошивки по воздуху

---

## Периферия (сохранено в NVS)

| Ключ     | Тип    | Пин | Label  | MQTT топик (set)                        |
|----------|--------|-----|--------|-----------------------------------------|
| `gate`   | relay  | 4   | Gate   | `mpcb/devices/esp32-FCC8/gate/set`      |
| `button` | button | 3   | Button | —                                       |

---

## Сценарии (Rules)

| Триггер | Событие  | Действие | Цель   | Параметр   |
|---------|----------|----------|--------|------------|
| button  | pressed  | pulse    | gate   | 500 мс     |

Настраиваются через `/gpio` → вкладка Сценарии, или через BLE-приложение.

---

## MQTT протокол (mpcbstudio)

**Брокер:** `mqtt.mpcbstudio.com:8883` (TLS)  
**Username / Password:** из личного кабинета → раздел MQTT  
**Credentials в NVS:** `user_4` (обновить через `/mqtt` или BLE, если изменится)

| Топик | Направление | Retain | Описание |
|-------|-------------|--------|----------|
| `mpcb/devices/esp32-FCC8/announce` | publish | ✓ | `{"online":true,"ip":"..."}` при connect |
| `mpcb/devices/esp32-FCC8/config`   | publish | ✓ | конфигурация устройства + peripherals |
| `mpcb/devices/esp32-FCC8/+/state`  | publish | ✓ | состояние каждой периферии |
| `mpcb/devices/esp32-FCC8/+/set`    | subscribe | — | команды от сервера |

**Последовательность при старте:**
```
announce → subscribe +/set → config → gate/state → button/state
```

**Форматы команд:**
```json
relay:  {"on": true} | {"on": false} | {"pulse": 500}
pwm:    {"duty": 128}
```

---

## Прошивка

```bash
cd e:/Projects/esp32-mpcb
pio run --target upload
```

COM порт определяется автоматически (COM10 обычно).  
NVS (WiFi, MQTT, периферия, сценарии) при прошивке **не стирается**.

Если нужно сбросить NVS полностью:
```bash
pio run --target erase
```

---

## Известные проблемы

| Проблема | Статус | Решение |
|----------|--------|---------|
| Дублирование устройства на сервере | Серверный баг — `INSERT` вместо `UPSERT` | Ждём фикса на сервере: `update_or_create(device_id=...)` |
| Capabilities пустые в кабинете | Сервер ещё не читает `peripherals` из config | Ждём фичи авто-заполнения на сервере |
| MQTT credentials в NVS (старые) | Если `mqtt.host` уже есть — дефолты не применяются | Обновить через `http://192.168.1.41/mqtt` |

---

## План развития

### Приоритет 1 — текущая сессия
- [x] MQTT spec compliance: LWT, announce, config, retain=true, pulse
- [x] PeriphManager: wildcard subscribe `+/set`, правильный порядок publish
- [x] Фикс дублирования: `_setState(RUNNING)` только при первом подключении
- [x] Фикс deviceId empty bug (загружать внутри RUNNING callback)
- [x] Дебаунс кнопки 10 мс
- [x] Rules: sanitize ключей trigger/target при загрузке из NVS

### Приоритет 2 — следующая сессия
- [ ] **Flutter app (mpcb-app):** маркер PS→MC на карте, GPIO экран, Rules экран
- [ ] **DHT22 полная поддержка:** подключить `adafruit/DHT sensor library`, публикация каждые 30 с
- [ ] **DS18B20 полная поддержка:** подключить `milesburton/DallasTemperature`, публикация каждые 30 с
- [ ] **Обновление credentials без перепрошивки** — через BLE или /mqtt страницу (NVS уже перезаписывается при сохранении)

### Приоритет 3 — будущее
- [ ] MpcbZigbeeCore (c6-zigbee, h2 чипы)
- [ ] PowerManager deep sleep (c3, h2 на батарейках)
- [ ] Web debug tool (web-bluetooth) — уже сделан в `mpcb-app/web-debug/index.html`
