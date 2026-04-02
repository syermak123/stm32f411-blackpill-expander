# STM32F411 BlackPill I2C Expander

## English (default)

Firmware for `STM32F411CE BlackPill` operating as an **I2C slave** for ESP32/CYD:
- 16 universal GPIOs (input/output)
- control of 4x MCP41010 digital potentiometers (SPI)
- RTC read/set over I2C
- watchdog reset when I2C activity is lost

### Main Parameters

- Board: `blackpill_f411ce`
- Framework: `stm32cube` (PlatformIO)
- I2C address (7-bit): `0x30`
- I2C pins: `PB6=SCL`, `PB7=SDA`
- I2C speed (slave init): `100 kHz` (clock is driven by master)

### I2C Commands

| Cmd | Description | Data after cmd |
|---|---|---|
| `0x01` | Legacy set mask (U0..U7) | `mask` |
| `0x02` | Legacy set bit (U0..U7) | `ch`, `val` |
| `0x10` | LED (PC13) | `on` |
| `0x20` | Set MCP41010 | `pot(0..3)`, `value(0..255)` |
| `0x21` | Prepare read pots | — |
| `0x30` | Prepare read RTC | — |
| `0x31` | Set RTC | `YY,MM,DD,HH,MM,SS` |
| `0x40` | Set GPIO direction 16-bit | `dirL`, `dirH` |
| `0x41` | Set GPIO output 16-bit | `outL`, `outH` |
| `0x42` | Set one GPIO bit | `idx(0..15)`, `val` |
| `0x43` | Prepare read GPIO levels | — |

### Read Responses (after write)

- default: `1` byte (`g_outputMask`, U0..U7)
- after `0x21`: `4` bytes (`P0..P3`)
- after `0x30`: `6` bytes (`YY MM DD HH MM SS`)
- after `0x43`: `2` bytes (`levelsL levelsH`)

### Pin Usage in Firmware

- `PA5/PA7` — SPI1 SCK/MOSI to MCP41010
- `PA6` — SPI1 MISO (AF mode; not used by MCP41010)
- `PB0/PB1/PB2/PB10` — CS for pot0..pot3
- `PC13` — status LED
- `PA13/PA14` — SWD
- `PC14/PC15` — LSE (optional, for RTC)
- U0..U15: `PA8,PA9,PA10,PA11,PA12,PB12,PB13,PB14,PA1,PA2,PA3,PA4,PB3,PB4,PB5,PB8`

### Build and Flash

```bash
pio run
pio run -t upload
```

For serial monitor:

```bash
pio device monitor
```

### Documentation

Detailed protocol/pinout docs and examples:
- `docs/I2C_PROTOCOL_AND_PINS.md`
- `docs/I2C_PROTOCOL_AND_PINS.pdf`
- `docs/ESP32_ARDUINO_EXAMPLE.md`
- `examples/esp32-arduino/esp32_i2c_expander.ino`

### Reliability Notes

- I2C RX is handled byte-by-byte in `Seq` mode for better ESP32 `Wire` compatibility.
- I2C RX buffer has headroom for the longest command (`0x31` RTC set).
- IWDG is refreshed only if recent I2C master activity is detected (auto-recovery on link stall).

---

## Українська

Прошивка для `STM32F411CE BlackPill`, яка працює як **I2C slave** для ESP32/CYD:
- 16 універсальних GPIO (input/output)
- керування 4x MCP41010 (SPI)
- RTC read/set через I2C
- watchdog reset при втраті I2C-активності

### Основні параметри

- Плата: `blackpill_f411ce`
- Фреймворк: `stm32cube` (PlatformIO)
- I2C адреса (7-bit): `0x30`
- I2C пін-аут: `PB6=SCL`, `PB7=SDA`
- Швидкість I2C (ініціалізація): `100 kHz` (такт задає master)

### I2C команди

| Cmd | Опис | Дані після cmd |
|---|---|---|
| `0x01` | Legacy set mask (U0..U7) | `mask` |
| `0x02` | Legacy set bit (U0..U7) | `ch`, `val` |
| `0x10` | LED (PC13) | `on` |
| `0x20` | Set MCP41010 | `pot(0..3)`, `value(0..255)` |
| `0x21` | Prepare read pots | — |
| `0x30` | Prepare read RTC | — |
| `0x31` | Set RTC | `YY,MM,DD,HH,MM,SS` |
| `0x40` | Set GPIO direction 16-bit | `dirL`, `dirH` |
| `0x41` | Set GPIO output 16-bit | `outL`, `outH` |
| `0x42` | Set one GPIO bit | `idx(0..15)`, `val` |
| `0x43` | Prepare read GPIO levels | — |

### Read-відповіді (після write)

- default: `1` байт (`g_outputMask`, U0..U7)
- після `0x21`: `4` байти (`P0..P3`)
- після `0x30`: `6` байтів (`YY MM DD HH MM SS`)
- після `0x43`: `2` байти (`levelsL levelsH`)

### Піни в прошивці

- `PA5/PA7` — SPI1 SCK/MOSI до MCP41010
- `PA6` — SPI1 MISO (AF, MCP не використовує)
- `PB0/PB1/PB2/PB10` — CS для pot0..pot3
- `PC13` — status LED
- `PA13/PA14` — SWD
- `PC14/PC15` — LSE (опційно для RTC)
- U0..U15: `PA8,PA9,PA10,PA11,PA12,PB12,PB13,PB14,PA1,PA2,PA3,PA4,PB3,PB4,PB5,PB8`

### Збірка і прошивка

```bash
pio run
pio run -t upload
```

Для відлагодження/монітору:

```bash
pio device monitor
```

### Документація

Детальна документація по пін-аутах, протоколу і прикладах:
- `docs/I2C_PROTOCOL_AND_PINS.md`
- `docs/I2C_PROTOCOL_AND_PINS.pdf`
- `docs/ESP32_ARDUINO_EXAMPLE.md`
- `examples/esp32-arduino/esp32_i2c_expander.ino`

### Нотатки по надійності

- Прийом I2C зроблено по 1 байту в `Seq`-режимі для кращої сумісності з ESP32 `Wire`.
- RX-буфер I2C має запас для найдовшої команди RTC set (`0x31`).
- IWDG оновлюється лише якщо I2C master спілкувався нещодавно (ціль: автоскидання при зависанні лінку).
