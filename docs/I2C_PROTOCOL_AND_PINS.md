# STM32F411 Black Pill Expander - Pins and I2C Protocol

Firmware: `stm32f411-blackpill-expander` (HAL, I2C slave, SPI master, RTC, IWDG).

---

## English (default)

### Physical I2C Interface

| Signal | MCU pin | Note |
|--------|---------|------|
| SCL | **PB6** | I2C1 |
| SDA | **PB7** | I2C1 |
| GND | — | shared ground with master |
| 3.3 V | — | **3.3 V logic only** |

Mode: 7-bit address; HAL uses 8-bit form `0x30 << 1`.

| Parameter | Value |
|-----------|-------|
| 7-bit SLA | **0x30** |
| Slave init speed | **100 kHz** (clock driven by master) |
| STM32 role | **I2C slave** |

After each write/read transaction, the next read may return a prepared response. By default, a read of 1 byte returns legacy lower-8 output mask (`g_outputMask`).

ESP32 `Wire` note: multi-byte commands are received byte-by-byte (`I2C_FIRST_FRAME` / `NEXT_FRAME` / `LAST_FRAME`) for better compatibility.

### Full BlackPill Pin Table (usage in this firmware)

| Pin (MCU) | Firmware role | Details |
|---|---|---|
| **PA0** | not used | free |
| **PA1** | universal GPIO **U8** | `0x40/0x41/0x42/0x43` |
| **PA2** | universal GPIO **U9** | `0x40/0x41/0x42/0x43` |
| **PA3** | universal GPIO **U10** | `0x40/0x41/0x42/0x43` |
| **PA4** | universal GPIO **U11** | check board-specific conflicts on some variants |
| **PA5** | **SPI1 SCK** | MCP41010 |
| **PA6** | **SPI1 MISO (AF)** | MCP41010 has no MISO; kept in AF due to 2LINES mode |
| **PA7** | **SPI1 MOSI** | MCP41010 |
| **PA8** | universal GPIO **U0** | affected by legacy `0x01/0x02` |
| **PA9** | universal GPIO **U1** | affected by legacy `0x01/0x02` |
| **PA10** | universal GPIO **U2** | affected by legacy `0x01/0x02` |
| **PA11** | universal GPIO **U3** | affected by legacy `0x01/0x02` |
| **PA12** | universal GPIO **U4** | affected by legacy `0x01/0x02` |
| **PA13** | **SWDIO** | debug |
| **PA14** | **SWCLK** | debug |
| **PA15** | not used | usually kept free |
| **PB0** | **CS pot0** | MCP41010 chip select |
| **PB1** | **CS pot1** | MCP41010 chip select |
| **PB2** | **CS pot2** | MCP41010 chip select |
| **PB3** | universal GPIO **U12** | may conflict with JTAG in some setups |
| **PB4** | universal GPIO **U13** | may conflict with JTAG in some setups |
| **PB5** | universal GPIO **U14** | may conflict with JTAG in some setups |
| **PB6** | **I2C1 SCL** | slave `0x30` |
| **PB7** | **I2C1 SDA** | slave `0x30` |
| **PB8** | universal GPIO **U15** | `0x40/0x41/0x42/0x43` |
| **PB9** | not used | free |
| **PB10** | **CS pot3** | MCP41010 chip select |
| **PB11** | not used | free |
| **PB12** | universal GPIO **U5** | part of legacy lower 8 outputs |
| **PB13** | universal GPIO **U6** | part of legacy lower 8 outputs |
| **PB14** | universal GPIO **U7** | part of legacy lower 8 outputs |
| **PB15** | not used | free |
| **PC13** | **status LED** | `0x10`, often active-low |
| **PC14** | RTC **LSE** (optional) | if 32.768 kHz crystal is fitted |
| **PC15** | RTC **LSE** (optional) | if 32.768 kHz crystal is fitted |
| **NRST** | reset | hardware reset |
| **BOOT0** | boot strap | boot mode select |
| **VBAT** | RTC backup supply | optional battery/backup domain supply |
| **3V3** | power | 3.3 V logic rail |
| **5V** | power | board power input (board-dependent) |
| **GND** | ground | common with I2C master |

### 16 Universal GPIO Mapping (U0..U15)

Direction mask: `1=output`, `0=input`. Input mode uses internal pull-up; output mode is push-pull.

| U# | MCU pin | Bit in 16-bit mask |
|----|---------|--------------------|
| U0 | PA8 | 0 |
| U1 | PA9 | 1 |
| U2 | PA10 | 2 |
| U3 | PA11 | 3 |
| U4 | PA12 | 4 |
| U5 | PB12 | 5 |
| U6 | PB13 | 6 |
| U7 | PB14 | 7 |
| U8 | PA1 | 8 |
| U9 | PA2 | 9 |
| U10 | PA3 | 10 |
| U11 | PA4 | 11 |
| U12 | PB3 | 12 |
| U13 | PB4 | 13 |
| U14 | PB5 | 14 |
| U15 | PB8 | 15 |

### I2C Commands (master -> slave)

| Cmd | Name | Master data (after cmd) | Write length |
|-----|------|--------------------------|--------------|
| **0x01** | Legacy set mask | `mask` (U0..U7) | 2 |
| **0x02** | Legacy set bit | `ch` (0..7), `val` (0/1) | 3 |
| **0x10** | LED | `on` (0/1) | 2 |
| **0x20** | Set pot | `pot` (0..3), `value` (0..255) | 3 |
| **0x21** | Prepare read pots | — | 1 |
| **0x30** | Prepare read RTC | — | 1 |
| **0x31** | Set RTC | `YY,MM,DD,HH,MM,SS` (year = 2000+YY) | 7 |
| **0x40** | GPIO direction | `dirL`, `dirH` (little-endian 16-bit) | 3 |
| **0x41** | GPIO write | `outL`, `outH` (little-endian 16-bit) | 3 |
| **0x42** | GPIO write one | `idx` (0..15), `val` (0/1) | 3 |
| **0x43** | Prepare read GPIO | — | 1 |

### I2C Read Responses (slave -> master)

| After command | Read bytes | Payload |
|---------------|------------|---------|
| default | **1** | `g_outputMask` (U0..U7) |
| **0x21** | **4** | P0, P1, P2, P3 |
| **0x30** | **6** | YY, MM, DD, HH, MM, SS |
| **0x43** | **2** | `levelsL`, `levelsH` (physical logic levels) |

Only the latest "prepare read" command is retained before the next read.

### ESP32 `Wire` Example

```cpp
Wire.beginTransmission(0x30);
Wire.write(0x43);
Wire.endTransmission(false);
Wire.requestFrom(0x30, 2);
uint8_t lo = Wire.read();
uint8_t hi = Wire.read();
uint16_t levels = (uint16_t)lo | ((uint16_t)hi << 8);
```

### Other Firmware Mechanics

- IWDG is refreshed only if recent I2C activity was detected; after about 15 seconds without I2C traffic, MCU reset is intentional.
- RTC clock source prefers LSE, falls back to LSI if LSE is not ready.

---

## Українська версія

Прошивка: `stm32f411-blackpill-expander` (HAL, I2C slave, SPI master, RTC, IWDG).

---

## Фізичний інтерфейс I2C

| Сигнал | Вивід MCU | Примітка |
|--------|-----------|----------|
| SCL | **PB6** | I2C1 |
| SDA | **PB7** | I2C1 |
| GND | — | спільна земля з master |
| 3.3 V | — | логіка **тільки 3.3 V** |

**Режим:** 7-bit адреса, 8-bit адреса в HAL задається як `0x30 << 1`.

| Параметр | Значення |
|----------|----------|
| 7-bit SLA | **0x30** |
| Швидкість (ініціалізація slave) | **100 kHz** (такт задає master) |
| Роль STM32 | **I2C slave** |

Після кожного **write** (або **read**) на наступний **read** може бути підготовлена особлива “відповідь” (див. нижче). За замовчуванням **read 1 байт** повертає legacy-маску нижніх 8 виходів (`g_outputMask`).

**Примітка для ESP32 `Wire`:** багатобайтові команди приймаються на slave послідовно **по одному байту** (`I2C_FIRST_FRAME` / `NEXT_FRAME` / `LAST_FRAME`), щоб надійно працювати з master ESP32.

---

## Таблиця пінів MCU (функції в прошивці)

### Повна таблиця пінів BlackPill (що використано в цій прошивці)

Тут зведено **всі піни**, які зазвичай виведені на гребінки BlackPill (STM32F411CE), і їх роль **саме в цій прошивці**.

| Пін (MCU) | Роль у прошивці | Деталі / примітки |
|---|---|---|
| **PA0** | не використовується | (вільний) |
| **PA1** | універсальний GPIO **U8** | `0x40/0x41/0x42/0x43`, input pull-up або output PP |
| **PA2** | універсальний GPIO **U9** | `0x40/0x41/0x42/0x43` |
| **PA3** | універсальний GPIO **U10** | `0x40/0x41/0x42/0x43` |
| **PA4** | універсальний GPIO **U11** | `0x40/0x41/0x42/0x43`; на деяких платах може конфліктувати (перевір залізо) |
| **PA5** | **SPI1 SCK** | MCP41010 |
| **PA6** | **SPI1 MISO (AF)** | MCP41010 MISO не має; пін все одно налаштований як AF (2LINES), `pull-up` |
| **PA7** | **SPI1 MOSI** | MCP41010 |
| **PA8** | універсальний GPIO **U0** | legacy `0x01/0x02` зачіпають U0…U7 |
| **PA9** | універсальний GPIO **U1** | legacy U0…U7 |
| **PA10** | універсальний GPIO **U2** | legacy U0…U7 |
| **PA11** | універсальний GPIO **U3** | legacy U0…U7 |
| **PA12** | універсальний GPIO **U4** | legacy U0…U7 |
| **PA13** | **SWDIO** | налагодження (не чіпати) |
| **PA14** | **SWCLK** | налагодження (не чіпати) |
| **PA15** | не використовується | (вільний; у багатьох проектах уникають через JTAG/alternate-функції) |
| **PB0** | **CS pot0** | MCP41010 chip select |
| **PB1** | **CS pot1** | MCP41010 chip select |
| **PB2** | **CS pot2** | MCP41010 chip select |
| **PB3** | універсальний GPIO **U12** | може бути пов’язаний з JTAG у деяких конфігураціях |
| **PB4** | універсальний GPIO **U13** | може бути пов’язаний з JTAG у деяких конфігураціях |
| **PB5** | універсальний GPIO **U14** | може бути пов’язаний з JTAG у деяких конфігураціях |
| **PB6** | **I2C1 SCL** | slave `0x30` |
| **PB7** | **I2C1 SDA** | slave `0x30` |
| **PB8** | універсальний GPIO **U15** | `0x40/0x41/0x42/0x43` |
| **PB9** | не використовується | (вільний) |
| **PB10** | **CS pot3** | MCP41010 chip select |
| **PB11** | не використовується | (вільний) |
| **PB12** | універсальний GPIO **U5** | legacy U0…U7 (як частина старих 8 виходів) |
| **PB13** | універсальний GPIO **U6** | legacy U0…U7 |
| **PB14** | універсальний GPIO **U7** | legacy U0…U7 |
| **PB15** | не використовується | (вільний) |
| **PC13** | **Status LED** | команда `0x10`; часто active-low на BlackPill |
| **PC14** | RTC **LSE** (опційно) | якщо є кварц 32.768 kHz; інакше RTC працює від LSI |
| **PC15** | RTC **LSE** (опційно) | якщо є кварц 32.768 kHz; інакше RTC працює від LSI |
| **NRST** | reset | апаратний reset |
| **BOOT0** | boot strap | вибір режиму завантаження |
| **VBAT** | живлення RTC (опц.) | живлення backup-домену (RTC) |
| **3V3** | живлення | логіка 3.3 V |
| **5V** | живлення | вхід 5 V (залежить від плати/підключення) |
| **GND** | земля | спільна земля з I2C master |

### Зайнято периферією (не використовувати як універсальні GPIO)

| Пін | Функція |
|-----|---------|
| **PA13** | SWDIO (налагодження) |
| **PA14** | SWCLK |
| **PA5** | SPI1_SCK → MCP41010 |
| **PA6** | SPI1_MISO (режим 2LINES; до MCP не підключено) |
| **PA7** | SPI1_MOSI → MCP41010 |
| **PB0, PB1, PB2, PB10** | CS0…CS3 цифропотенціометрів MCP41010 |
| **PB6, PB7** | I2C1 SCL / SDA |
| **PC13** | Статус LED на платі (часто active-low) |

### RTC (за потреби проектування)

| Пін | Призначення |
|-----|-------------|
| **PC14, PC15** | LSE (32.768 kHz), якщо кварц встановлений |

У прошивці RTC піднімається з **LSE**, якщо він готовий, інакше — з **LSI**.

### 16 універсальних пінів U0…U15

Кожен біт маски відповідає одному піну. **Напрям:** `dir` — 1 = output, 0 = input. **Input:** внутрішня підтяжка **pull-up**. **Output:** push-pull, без додаткової підтяжки.

| U# | MCU pin | Біт у 16-bit масці |
|----|---------|----------------------|
| U0 | PA8 | 0 |
| U1 | PA9 | 1 |
| U2 | PA10 | 2 |
| U3 | PA11 | 3 |
| U4 | PA12 | 4 |
| U5 | PB12 | 5 |
| U6 | PB13 | 6 |
| U7 | PB14 | 7 |
| U8 | PA1 | 8 |
| U9 | PA2 | 9 |
| U10 | PA3 | 10 |
| U11 | PA4 | 11 |
| U12 | PB3 | 12 |
| U13 | PB4 | 13 |
| U14 | PB5 | 14 |
| U15 | PB8 | 15 |

**Застереження:**

- **PB3/PB4/PB5** після ресету можуть бути пов’язані з JTAG; у типових «SWD only» проектах їх використовують як GPIO, але перевір схему/Option bytes.
- **PA4** може конфліктувати з іншою платою (наприклад, CS Flash на деяких ревізіях WeAct) — перевір свою плату.
- Піни **U0…U7** збігаються з колишніми 8 «цифровими виходами»; команди `0x01/0x02` примусово виставляють ці біти як **output** і оновлюють їхні рівні.

### MCP41010 (SPI)

| Сигнал | STM32 |
|--------|-------|
| SCK | PA5 |
| MOSI | PA7 |
| CS pot 0…3 | PB0, PB1, PB2, PB10 |

На кожен чіп подається команда SPI `0x11` + байт значення 0…255 (один канал на MCP41010).

---

## Команди I2C (master → slave)

Усі кадри починаються з **байта команди** `cmd`. Далі йдуть аргументи згідно з таблицею.

| Cmd | Назва (умовна) | Дані master (після cmd) | Довжина write (всього) |
|-----|----------------|-------------------------|-------------------------|
| **0x01** | Legacy set mask | `mask` (U0…U7) | 2 |
| **0x02** | Legacy set bit | `ch` (0…7), `val` (0/1) | 3 |
| **0x10** | LED | `on` (0/1) | 2 |
| **0x20** | Set pot | `pot` (0…3), `value` (0…255) | 3 |
| **0x21** | Prepare read pots | — | 1 |
| **0x30** | Prepare read RTC | — | 1 |
| **0x31** | Set RTC | `YY,MM,DD,HH,MM,SS` (рік = 2000+YY) | 7 |
| **0x40** | GPIO direction | `dirL`, `dirH` (little-endian 16-bit) | 3 |
| **0x41** | GPIO write | `outL`, `outH` (little-endian 16-bit) | 3 |
| **0x42** | GPIO write one | `idx` (0…15), `val` (0/1) | 3 |
| **0x43** | Prepare read GPIO | — | 1 |

Невідома команда обробляється як довжина 1 (тільки `cmd`).

### Поведінка **0x01 / 0x02**

- Виставляють **маску нижніх 8 біт** `g_outputMask`.
- Примусово позначають **U0…U7 як OUTPUT** (`g_gpioDir |= 0x00FF`).
- Оновлюють `g_gpioOut` для цих бітів і викликають реконфігурацію портів.

### Поведінка **0x40 / 0x41 / 0x42**

- **0x40:** задає повну 16-бітну маску напрямку `g_gpioDir`.
- **0x41:** записує `g_gpioOut`; на пінах, які не output, рівень у тіньовому регістрі зберігається, але на ніжку не виходить; `g_outputMask = outL` (legacy-дзеркало).
- **0x42:** один пін output + рівень; біт у `g_gpioDir` встановлюється.

### Поведінка **0x20** (поти)

Черга на SPI; значення зберігаються у `g_potValue[]`.

### Поведінка **0x31** (RTC)

Пише дату/час у RTC (BCD) і маркер у backup register `RTC_BKP_DR0 = 0xA5A5`.

---

## Відповіді I2C (slave → master, read після write)

Після відповідної “підготовлювальної” команди наступний **I2C read** повертає:

| Після команди | Read: кількість байт | Вміст |
|---------------|----------------------|--------|
| (за замовчуванням) | **1** | `g_outputMask` (біти U0…U7) |
| **0x21** | **4** | P0, P1, P2, P3 (останні записані значення в прошивці) |
| **0x30** | **6** | YY, MM, DD, HH, MM, SS (рік 2000+YY) |
| **0x43** | **2** | `levelsL`, `levelsH` — поточні **фізичні** рівні 16 пінів (1 = HIGH) |

Якщо підряд відправити кілька “prepare …” команд, зберігається лише **остання** запрошена відповідь перед read.

---

## Приклади (ESP32 Arduino `Wire`)

Читати 16 GPIO після команди `0x43`:

```cpp
Wire.beginTransmission(0x30);
Wire.write(0x43);
Wire.endTransmission(false);
Wire.requestFrom(0x30, 2);
uint8_t lo = Wire.read();
uint8_t hi = Wire.read();
uint16_t levels = (uint16_t)lo | ((uint16_t)hi << 8);
```

Встановити U0 як output HIGH:

```cpp
Wire.beginTransmission(0x30);
Wire.write(0x42);
Wire.write(0);   // idx U0
Wire.write(1);   // HIGH
Wire.endTransmission();
```

---

## Інші механізми прошивки

### IWDG (~15 с без I2C)

Незалежний watchdog. **`HAL_IWDG_Refresh`** викликається лише якщо з останньої активності I2C минуло **менше 15 секунд**. Якщо master перестав размовляти з slave — через ~15 с відбудеться **скидання** STM32.

Активність оновлюється в обробниках I2C (address match, RX/TX complete).

### SysTick

Після `SystemClock_Config()` SysTick перевиставляється для коректного `HAL_GetTick()` у мілісекундах.

---

## Версія документа

Відповідає вихідному коду `main.c` у каталозі `stm32f411-blackpill-expander/src/` (команди та мапа U0…U15 — як у прошивці на момент генерації цього файлу).
