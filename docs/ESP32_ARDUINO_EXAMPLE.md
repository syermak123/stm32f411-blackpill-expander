# ESP32 Arduino Example (DevKit and CYD)

This example uses the Arduino `Wire` library and works for both:
- ESP32 DevKit
- CYD (ESP32-2432S028 and similar ESP32-based boards)

The protocol is identical on both boards. Only hardware SDA/SCL pins may differ by board wiring.

## Wiring

- ESP32 `SDA` -> STM32 `PB7` (SDA)
- ESP32 `SCL` -> STM32 `PB6` (SCL)
- ESP32 `GND` -> STM32 `GND`
- Logic level: **3.3V only**

STM32 slave address (7-bit): `0x30`

## Full Example Sketch

```cpp
#include <Arduino.h>
#include <Wire.h>

static const uint8_t STM_ADDR = 0x30;

// Change these if your board uses different I2C pins.
// Common ESP32 defaults are SDA=21, SCL=22.
static const int I2C_SDA = 21;
static const int I2C_SCL = 22;

static bool writeCmd(const uint8_t *buf, size_t len, bool sendStop = true) {
  Wire.beginTransmission(STM_ADDR);
  for (size_t i = 0; i < len; i++) {
    Wire.write(buf[i]);
  }
  return Wire.endTransmission(sendStop) == 0;
}

static bool readBytes(uint8_t *out, size_t len) {
  size_t got = Wire.requestFrom((int)STM_ADDR, (int)len);
  if (got != len) return false;
  for (size_t i = 0; i < len; i++) out[i] = Wire.read();
  return true;
}

static void setLed(bool on) {
  uint8_t cmd[2] = {0x10, (uint8_t)(on ? 1 : 0)};
  writeCmd(cmd, sizeof(cmd));
}

static void setOneGpio(uint8_t idx, bool high) {
  uint8_t cmd[3] = {0x42, idx, (uint8_t)(high ? 1 : 0)};
  writeCmd(cmd, sizeof(cmd));
}

static uint16_t readGpioLevels() {
  uint8_t prep = 0x43;
  uint8_t in[2] = {0, 0};
  // Repeated START between write(prepare) and read
  if (!writeCmd(&prep, 1, false)) return 0;
  if (!readBytes(in, 2)) return 0;
  return (uint16_t)in[0] | ((uint16_t)in[1] << 8);
}

static void setRtc_2026_03_31_12_00_00() {
  // 0x31 YY MM DD HH MM SS, year = 2000 + YY
  uint8_t cmd[7] = {0x31, 26, 3, 31, 12, 0, 0};
  writeCmd(cmd, sizeof(cmd));
}

static bool readRtc(uint8_t out6[6]) {
  uint8_t prep = 0x30;
  if (!writeCmd(&prep, 1, false)) return false;
  return readBytes(out6, 6);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Wire.begin(I2C_SDA, I2C_SCL, 100000);  // 100kHz
  delay(50);

  Serial.println("STM32 I2C expander demo");

  setLed(true);
  delay(200);
  setLed(false);

  // Set U0 HIGH, U1 LOW
  setOneGpio(0, true);
  setOneGpio(1, false);

  uint16_t levels = readGpioLevels();
  Serial.printf("GPIO levels mask: 0x%04X\n", levels);

  setRtc_2026_03_31_12_00_00();
}

void loop() {
  uint8_t rtc[6];
  if (readRtc(rtc)) {
    Serial.printf("RTC: 20%02u-%02u-%02u %02u:%02u:%02u\n",
                  rtc[0], rtc[1], rtc[2], rtc[3], rtc[4], rtc[5]);
  } else {
    Serial.println("RTC read failed");
  }
  delay(1000);
}
```

## Notes

- If your board has different I2C pins, change `I2C_SDA`/`I2C_SCL`.
- `writeCmd(..., false)` is used for prepare-then-read commands (`0x30`, `0x43`) to keep a repeated start.
- For CYD and DevKit, code is the same at protocol level.
