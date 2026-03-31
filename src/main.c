#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <string.h>

// ----------------- Protocol -----------------
// I2C slave address: 0x30
// Commands (master -> slave):
//   0x01 [mask]            - set 8 digital outputs
//   0x02 [ch] [0|1]        - set one digital output
//   0x10 [0|1]             - set status LED
//   0x20 [pot] [value]     - set MCP41010 pot (0..3, 0..255)
//   0x21                  - prepare next read: 4 bytes pot values (P0..P3)
//   0x30                  - prepare next read: 6 bytes RTC time (YY MM DD HH MM SS; year=2000+YY)
//   0x31 [YY MM DD HH MM SS] - set RTC time
//   0x40 [dirL dirH]       - set 16-pin direction bitmask (1=output, 0=input)
//   0x41 [outL outH]       - set 16-pin outputs (only applied to pins configured as output)
//   0x42 [idx] [0|1]       - set one pin output level (also forces that pin to output)
//   0x43                  - prepare next read: 2 bytes pin levels (inL inH) for 16 pins
// Read (master <- slave), after a write command:
//   default: 1 byte current output mask
//   after 0x21: 4 bytes pot values (P0..P3)
//   after 0x30: 6 bytes RTC time
//   after 0x43: 2 bytes pin levels (16 pins)
//
// Hardware mapping (Blackpill F411CE):
// I2C1: PB6=SCL, PB7=SDA
// SPI1 (for MCP41010): PA5=SCK, PA6=MISO (unused, pull — not on MCP41010), PA7=MOSI
// MCP41010 CS: PB0, PB1, PB2, PB10
// 8 digital outs: PA8..PA12 + PB12..PB14 (never PA13/14 — those are SWD)
// Status LED: PC13 (active low on many Blackpill boards)

#define I2C_SLAVE_ADDR_7BIT   0x30U
#define CMD_SET_MASK          0x01U
#define CMD_SET_CHANNEL       0x02U
#define CMD_SET_LED           0x10U
#define CMD_SET_POT           0x20U
#define CMD_GET_POTS          0x21U
#define CMD_RTC_GET           0x30U
#define CMD_RTC_SET           0x31U
#define CMD_GPIO_DIR          0x40U
#define CMD_GPIO_WRITE        0x41U
#define CMD_GPIO_WRITE1       0x42U
#define CMD_GPIO_READ         0x43U
#define POT_STARTUP_VALUE     200U
#define I2C_RX_BUF_SIZE       8U

I2C_HandleTypeDef hi2c1;
SPI_HandleTypeDef hspi1;
DMA_HandleTypeDef hdma_spi1_tx;
RTC_HandleTypeDef hrtc;
IWDG_HandleTypeDef hiwdg;

static volatile uint8_t g_outputMask = 0;  // legacy lower 8 outputs
static volatile uint8_t g_ledState = 0;
static volatile bool g_spiBusy = false;
static volatile bool g_i2cRxActive = false;
static volatile uint32_t g_lastI2cMs = 0;

static uint8_t g_i2cRxBuf[I2C_RX_BUF_SIZE];
static uint8_t g_i2cTxBuf[8];
static uint8_t g_i2cTxLen = 0;
static uint8_t g_i2cTxIdx = 0;
static uint8_t g_i2cCmd = 0;
static uint8_t g_i2cExpected = 0;
static uint8_t g_i2cReceived = 0;
static volatile bool g_sendPotsOnNextRead = false;
static volatile uint8_t g_sendRtcOnNextRead = 0;
static volatile uint8_t g_sendGpioOnNextRead = 0;

static volatile bool g_potPending[4] = {false, false, false, false};
static volatile uint8_t g_potValue[4] = {
    POT_STARTUP_VALUE, POT_STARTUP_VALUE, POT_STARTUP_VALUE, POT_STARTUP_VALUE};
static volatile uint8_t g_activePot = 0xFF;
static uint8_t g_spiTxBuf[2] = {0x11, POT_STARTUP_VALUE};  // 0x11=write pot0

// 16 universal pins: can be input or output.
// Avoid SWD (PA13/PA14), I2C (PB6/PB7), SPI1 (PA5/PA6/PA7), and CS pins (PB0/PB1/PB2/PB10).
static const uint16_t U_PINS[16] = {
    // U0..U7 (keeps old mapping for outputs)
    GPIO_PIN_8,   // U0: PA8
    GPIO_PIN_9,   // U1: PA9
    GPIO_PIN_10,  // U2: PA10
    GPIO_PIN_11,  // U3: PA11
    GPIO_PIN_12,  // U4: PA12
    GPIO_PIN_12,  // U5: PB12
    GPIO_PIN_13,  // U6: PB13
    GPIO_PIN_14,  // U7: PB14
    // U8..U15 (extra 8 pins)
    GPIO_PIN_1,   // U8:  PA1
    GPIO_PIN_2,   // U9:  PA2
    GPIO_PIN_3,   // U10: PA3
    GPIO_PIN_4,   // U11: PA4
    GPIO_PIN_3,   // U12: PB3
    GPIO_PIN_4,   // U13: PB4
    GPIO_PIN_5,   // U14: PB5
    GPIO_PIN_8,   // U15: PB8
};

static GPIO_TypeDef *const U_PORTS[16] = {
    GPIOA, GPIOA, GPIOA, GPIOA, GPIOA, GPIOB, GPIOB, GPIOB,
    GPIOA, GPIOA, GPIOA, GPIOA, GPIOB, GPIOB, GPIOB, GPIOB
};

static volatile uint16_t g_gpioDir = 0x00U;   // 1=output
static volatile uint16_t g_gpioOut = 0x0000U; // output shadow

static const uint16_t POT_CS_PINS[4] = {
    GPIO_PIN_0,   // PB0
    GPIO_PIN_1,   // PB1
    GPIO_PIN_2,   // PB2
    GPIO_PIN_10   // PB10
};

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_I2C1_Init(void);
static void MX_SPI1_Init(void);
static void MX_RTC_Init(void);
static void MX_IWDG_Init(void);
static void Error_Handler(void);

static void SetStatusLed(uint8_t on) {
  g_ledState = on ? 1 : 0;
  // PC13 is often active low.
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void ApplyGpioConfig(void) {
  for (uint8_t i = 0; i < 16; i++) {
    GPIO_InitTypeDef io = {0};
    io.Pin = U_PINS[i];
    if ((g_gpioDir >> i) & 0x1U) {
      io.Mode = GPIO_MODE_OUTPUT_PP;
      io.Pull = GPIO_NOPULL;
      io.Speed = GPIO_SPEED_FREQ_LOW;
      HAL_GPIO_Init(U_PORTS[i], &io);
      HAL_GPIO_WritePin(U_PORTS[i], U_PINS[i], ((g_gpioOut >> i) & 0x1U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    } else {
      io.Mode = GPIO_MODE_INPUT;
      io.Pull = GPIO_PULLUP;
      HAL_GPIO_Init(U_PORTS[i], &io);
    }
  }
}

static inline uint16_t ReadGpioLevels(void) {
  uint16_t m = 0;
  for (uint8_t i = 0; i < 16; i++) {
    GPIO_PinState st = HAL_GPIO_ReadPin(U_PORTS[i], U_PINS[i]);
    if (st == GPIO_PIN_SET) m |= (uint16_t)(1U << i);
  }
  return m;
}

static void ApplyLegacyMask(uint8_t mask) {
  g_outputMask = mask;
  // Legacy outputs are U0..U7
  g_gpioDir |= 0x00FFU;
  g_gpioOut = (uint16_t)((g_gpioOut & 0xFF00U) | (uint16_t)mask);
  ApplyGpioConfig();
}

static void PotSelect(uint8_t pot, GPIO_PinState level) {
  if (pot < 4) {
    HAL_GPIO_WritePin(GPIOB, POT_CS_PINS[pot], level);
  }
}

static void StartPotWriteDMA(uint8_t pot, uint8_t value) {
  if (pot > 3 || g_spiBusy) return;
  g_activePot = pot;
  g_spiTxBuf[0] = 0x11U;
  g_spiTxBuf[1] = value;
  g_spiBusy = true;

  PotSelect(pot, GPIO_PIN_RESET);
  /* Short setup so CS and SI are stable before first SCK (datasheet t_CSS) */
  for (volatile uint32_t d = 0; d < 80; d++) {
    __NOP();
  }
  if (HAL_SPI_Transmit_DMA(&hspi1, g_spiTxBuf, 2) != HAL_OK) {
    PotSelect(pot, GPIO_PIN_SET);
    g_spiBusy = false;
    g_activePot = 0xFF;
  }
}

static void PumpPotQueue(void) {
  if (g_spiBusy) return;
  for (uint8_t i = 0; i < 4; i++) {
    if (g_potPending[i]) {
      g_potPending[i] = false;
      StartPotWriteDMA(i, g_potValue[i]);
      return;
    }
  }
}

static uint8_t CommandExpectedLen(uint8_t cmd) {
  switch (cmd) {
    case CMD_SET_MASK: return 2;
    case CMD_SET_CHANNEL: return 3;
    case CMD_SET_LED: return 2;
    case CMD_SET_POT: return 3;
    case CMD_GET_POTS: return 1;
    case CMD_RTC_GET: return 1;
    case CMD_RTC_SET: return 7;
    case CMD_GPIO_DIR: return 3;
    case CMD_GPIO_WRITE: return 3;
    case CMD_GPIO_WRITE1: return 3;
    case CMD_GPIO_READ: return 1;
    default: return 1;
  }
}

static inline void ResetI2cSessionState(void) {
  g_i2cRxActive = false;
  g_i2cTxLen = 0;
  g_i2cTxIdx = 0;
}

static inline uint8_t bcd2bin(uint8_t v) { return (uint8_t)(((v >> 4) * 10U) + (v & 0x0FU)); }
static inline uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10U) << 4) | (v % 10U)); }

static void RtcReadYmdHms(uint8_t out6[6]) {
  RTC_TimeTypeDef t = {0};
  RTC_DateTypeDef d = {0};
  (void)HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BCD);
  (void)HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BCD);  // must read date after time
  out6[0] = bcd2bin(d.Year);
  out6[1] = bcd2bin(d.Month);
  out6[2] = bcd2bin(d.Date);
  out6[3] = bcd2bin(t.Hours);
  out6[4] = bcd2bin(t.Minutes);
  out6[5] = bcd2bin(t.Seconds);
}

static void RtcSetYmdHms(const uint8_t in6[6]) {
  RTC_TimeTypeDef t = {0};
  RTC_DateTypeDef d = {0};
  d.Year = bin2bcd(in6[0]);
  d.Month = bin2bcd(in6[1]);
  d.Date = bin2bcd(in6[2]);
  d.WeekDay = RTC_WEEKDAY_MONDAY;
  t.Hours = bin2bcd(in6[3]);
  t.Minutes = bin2bcd(in6[4]);
  t.Seconds = bin2bcd(in6[5]);
  t.TimeFormat = RTC_HOURFORMAT12_AM;
  t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  t.StoreOperation = RTC_STOREOPERATION_RESET;
  (void)HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BCD);
  (void)HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BCD);
  (void)HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, 0xA5A5U);
}

static void ProcessI2cCommand(const uint8_t *buf, uint8_t len) {
  if (len == 0) return;
  uint8_t cmd = buf[0];
  if (cmd == CMD_SET_MASK && len >= 2) {
    ApplyLegacyMask(buf[1]);
    return;
  }
  if (cmd == CMD_SET_CHANNEL && len >= 3) {
    uint8_t ch = buf[1];
    uint8_t val = buf[2];
    if (ch < 8) {
      uint8_t m = g_outputMask;
      if (val) m |= (1U << ch);
      else m &= (uint8_t)~(1U << ch);
      ApplyLegacyMask(m);
    }
    return;
  }
  if (cmd == CMD_SET_LED && len >= 2) {
    SetStatusLed(buf[1] ? 1U : 0U);
    return;
  }
  if (cmd == CMD_SET_POT && len >= 3) {
    uint8_t pot = buf[1];
    uint8_t val = buf[2];
    if (pot < 4) {
      g_potValue[pot] = val;
      g_potPending[pot] = true;
    }
    return;
  }
  if (cmd == CMD_GET_POTS && len >= 1) {
    // Next I2C read will return 4 bytes: P0..P3.
    g_sendPotsOnNextRead = true;
    return;
  }
  if (cmd == CMD_RTC_GET && len >= 1) {
    g_sendRtcOnNextRead = 1U;
    return;
  }
  if (cmd == CMD_RTC_SET && len >= 7) {
    RtcSetYmdHms(&buf[1]);
    return;
  }
  if (cmd == CMD_GPIO_DIR && len >= 3) {
    g_gpioDir = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
    ApplyGpioConfig();
    return;
  }
  if (cmd == CMD_GPIO_WRITE && len >= 3) {
    uint16_t v = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
    g_gpioOut = v;
    // keep legacy mirror
    g_outputMask = (uint8_t)(g_gpioOut & 0xFFU);
    ApplyGpioConfig();
    return;
  }
  if (cmd == CMD_GPIO_WRITE1 && len >= 3) {
    uint8_t idx = buf[1];
    uint8_t v = buf[2] ? 1U : 0U;
    if (idx < 16) {
      g_gpioDir |= (uint16_t)(1U << idx);
      if (v) g_gpioOut |= (uint16_t)(1U << idx);
      else g_gpioOut &= (uint16_t)~(1U << idx);
      g_outputMask = (uint8_t)(g_gpioOut & 0xFFU);
      ApplyGpioConfig();
    }
    return;
  }
  if (cmd == CMD_GPIO_READ && len >= 1) {
    g_sendGpioOnNextRead = 1U;
    return;
  }
}

int main(void) {
  HAL_Init();
  SystemClock_Config();

  // Ensure HAL_GetTick() is real milliseconds after we changed clocks.
  SystemCoreClockUpdate();
  HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);
  HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq() / 1000U);
  HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);

  MX_GPIO_Init();
  MX_DMA_Init();
  MX_SPI1_Init();
  MX_RTC_Init();
  MX_I2C1_Init();
  MX_IWDG_Init();

  ApplyLegacyMask(0x00U);
  SetStatusLed(0);
  for (uint8_t i = 0; i < 4; i++) {
    PotSelect(i, GPIO_PIN_SET);
    g_potPending[i] = true;
    g_potValue[i] = POT_STARTUP_VALUE;
  }
  // Default all universal pins to input (pull-up). Legacy outputs remain off until commanded.
  g_gpioDir = 0x0000U;
  g_gpioOut = 0x0000U;
  ApplyGpioConfig();

  if (HAL_I2C_EnableListen_IT(&hi2c1) != HAL_OK) {
    Error_Handler();
  }
  g_lastI2cMs = HAL_GetTick();

  while (1) {
    PumpPotQueue();

    // Feed watchdog only if CYD (ESP32) has talked to us recently.
    // If I2C stops for >15s, we intentionally let IWDG reset the MCU.
    uint32_t now = HAL_GetTick();
    if ((now - g_lastI2cMs) < 15000U) {
      (void)HAL_IWDG_Refresh(&hiwdg);
    }
  }
}

void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, uint8_t TransferDirection,
                          uint16_t AddrMatchCode) {
  (void)AddrMatchCode;
  if (hi2c != &hi2c1) return;
  g_lastI2cMs = HAL_GetTick();

  if (TransferDirection == I2C_DIRECTION_TRANSMIT) {
    g_i2cRxActive = true;
    g_i2cCmd = 0;
    g_i2cExpected = 1;
    g_i2cReceived = 0;
    HAL_I2C_Slave_Seq_Receive_IT(&hi2c1, &g_i2cRxBuf[0], 1, I2C_FIRST_FRAME);
  } else {
    // Prepare response for this read.
    g_i2cTxIdx = 0;
    if (g_sendRtcOnNextRead) {
      g_sendRtcOnNextRead = 0;
      g_i2cTxLen = 6;
      RtcReadYmdHms(&g_i2cTxBuf[0]);
    } else if (g_sendGpioOnNextRead) {
      g_sendGpioOnNextRead = 0;
      uint16_t lvl = ReadGpioLevels();
      g_i2cTxLen = 2;
      g_i2cTxBuf[0] = (uint8_t)(lvl & 0xFFU);
      g_i2cTxBuf[1] = (uint8_t)((lvl >> 8) & 0xFFU);
    } else if (g_sendPotsOnNextRead) {
      g_sendPotsOnNextRead = false;
      g_i2cTxLen = 4;
      g_i2cTxBuf[0] = g_potValue[0];
      g_i2cTxBuf[1] = g_potValue[1];
      g_i2cTxBuf[2] = g_potValue[2];
      g_i2cTxBuf[3] = g_potValue[3];
    } else {
      g_i2cTxLen = 1;
      g_i2cTxBuf[0] = g_outputMask;
    }

    uint32_t opt = (g_i2cTxLen == 1) ? I2C_LAST_FRAME : I2C_FIRST_AND_NEXT_FRAME;
    HAL_I2C_Slave_Seq_Transmit_IT(&hi2c1, &g_i2cTxBuf[0], 1, opt);
  }
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c) {
  if (hi2c != &hi2c1 || !g_i2cRxActive) return;
  g_lastI2cMs = HAL_GetTick();

  g_i2cReceived++;
  if (g_i2cReceived == 1) {
    g_i2cCmd = g_i2cRxBuf[0];
    g_i2cExpected = CommandExpectedLen(g_i2cCmd);
    if (g_i2cExpected > I2C_RX_BUF_SIZE) {
      /* Defensive guard in case protocol table changes in future. */
      ResetI2cSessionState();
      return;
    }
    if (g_i2cExpected == 1) {
      ProcessI2cCommand(g_i2cRxBuf, 1);
      ResetI2cSessionState();
      return;
    }
    /* One byte per Seq step: multi-byte LAST_FRAME alone is flaky with some I2C masters (e.g. ESP32). */
    HAL_I2C_Slave_Seq_Receive_IT(
        &hi2c1, &g_i2cRxBuf[1], 1,
        (g_i2cExpected == 2) ? I2C_LAST_FRAME : I2C_NEXT_FRAME);
    return;
  }

  if (g_i2cReceived < g_i2cExpected) {
    HAL_I2C_Slave_Seq_Receive_IT(
        &hi2c1, &g_i2cRxBuf[g_i2cReceived], 1,
        (g_i2cReceived + 1U == g_i2cExpected) ? I2C_LAST_FRAME : I2C_NEXT_FRAME);
    return;
  }

  ProcessI2cCommand(g_i2cRxBuf, g_i2cExpected);
  g_i2cRxActive = false;
}

void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c) {
  if (hi2c != &hi2c1) return;
  g_lastI2cMs = HAL_GetTick();
  if (g_i2cTxLen <= 1) return;

  g_i2cTxIdx++;
  if (g_i2cTxIdx >= g_i2cTxLen) return;

  uint32_t opt = (g_i2cTxIdx + 1U == g_i2cTxLen) ? I2C_LAST_FRAME : I2C_NEXT_FRAME;
  HAL_I2C_Slave_Seq_Transmit_IT(&hi2c1, &g_i2cTxBuf[g_i2cTxIdx], 1, opt);
}

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c) {
  if (hi2c != &hi2c1) return;
  ResetI2cSessionState();
  HAL_I2C_EnableListen_IT(&hi2c1);
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c) {
  if (hi2c != &hi2c1) return;
  ResetI2cSessionState();
  HAL_I2C_EnableListen_IT(&hi2c1);
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi) {
  if (hspi != &hspi1) return;
  if (g_activePot < 4) {
    PotSelect(g_activePot, GPIO_PIN_SET);
  }
  g_activePot = 0xFF;
  g_spiBusy = false;
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi) {
  if (hspi != &hspi1) return;
  if (g_activePot < 4) {
    PotSelect(g_activePot, GPIO_PIN_SET);
  }
  g_activePot = 0xFF;
  g_spiBusy = false;
}

void HAL_I2C_MspInit(I2C_HandleTypeDef *i2cHandle) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if (i2cHandle->Instance == I2C1) {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(I2C1_EV_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_SetPriority(I2C1_ER_IRQn, 1, 1);
    HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
  }
}

void HAL_SPI_MspInit(SPI_HandleTypeDef *spiHandle) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if (spiHandle->Instance == SPI1) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    /* 2LINES mode: MISO must be AF even if MCP41010 has no MISO (tie/pull in HW optional). */
    GPIO_InitStruct.Pin = GPIO_PIN_6;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    __HAL_RCC_DMA2_CLK_ENABLE();
    hdma_spi1_tx.Instance = DMA2_Stream3;
    hdma_spi1_tx.Init.Channel = DMA_CHANNEL_3;
    hdma_spi1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_spi1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_spi1_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_spi1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_spi1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_spi1_tx.Init.Mode = DMA_NORMAL;
    hdma_spi1_tx.Init.Priority = DMA_PRIORITY_HIGH;
    hdma_spi1_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_spi1_tx) != HAL_OK) {
      Error_Handler();
    }
    __HAL_LINKDMA(spiHandle, hdmatx, hdma_spi1_tx);

    HAL_NVIC_SetPriority(DMA2_Stream3_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);
  }
}

void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType =
      RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 |
      RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
    Error_Handler();
  }
}

static void MX_I2C1_Init(void) {
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = (I2C_SLAVE_ADDR_7BIT << 1);
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
    Error_Handler();
  }
}

static void MX_SPI1_Init(void) {
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  /* MCP41010 needs standard 4-wire SPI: SCK + MOSI + CS (MISO unused). */
  /* SPI_DIRECTION_1LINE is half-duplex and often breaks TX-only usage on F4. */
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;  // slow for margin
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK) {
    Error_Handler();
  }
}

static void MX_DMA_Init(void) {
  __HAL_RCC_DMA2_CLK_ENABLE();
}

static void MX_RTC_Init(void) {
  // Prefer LSE (external 32.768kHz crystal) for accuracy.
  // If LSE is absent or fails to start, fall back to LSI.
  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();

  __HAL_RCC_RTC_ENABLE();

  RCC_OscInitTypeDef osc = {0};
  osc.OscillatorType = RCC_OSCILLATORTYPE_LSE | RCC_OSCILLATORTYPE_LSI;
  osc.LSEState = RCC_LSE_ON;
  osc.LSIState = RCC_LSI_ON;
  osc.PLL.PLLState = RCC_PLL_NONE;
  (void)HAL_RCC_OscConfig(&osc);

  RCC_PeriphCLKInitTypeDef pclk = {0};
  pclk.PeriphClockSelection = RCC_PERIPHCLK_RTC;
  // Decide source based on LSE readiness
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) != RESET) {
    pclk.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
  } else {
    pclk.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
  }
  if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
    Error_Handler();
  }

  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  // 32.768kHz -> (127+1)*(255+1)=32768 -> 1Hz
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK) {
    Error_Handler();
  }

  uint32_t magic = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0);
  if (magic != 0xA5A5U) {
    // Default time if never set: 2026-01-01 00:00:00
    uint8_t def[6] = {26, 1, 1, 0, 0, 0};
    RtcSetYmdHms(def);
  }
}

static void MX_IWDG_Init(void) {
  // Independent watchdog from LSI (~32kHz). Target timeout ~15 seconds.
  // With prescaler 256: counter clock ~LSI/256 ≈ 128Hz -> 15s ≈ 1920 ticks.
  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
  hiwdg.Init.Reload = 1920;
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK) {
    Error_Handler();
  }
}

static void MX_GPIO_Init(void) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  // Status LED (PC13)
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

  /* PA8..PA12 only — PA13/PA14 are SWD; PA15 can interfere with JTAG fuse options. */
  GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 |
                        GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPIOA, GPIO_InitStruct.Pin, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPIOB, GPIO_InitStruct.Pin, GPIO_PIN_RESET);

  // MCP41010 CS pins
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPIOB, GPIO_InitStruct.Pin, GPIO_PIN_SET);
}

void DMA2_Stream3_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_spi1_tx); }
void I2C1_EV_IRQHandler(void) { HAL_I2C_EV_IRQHandler(&hi2c1); }
void I2C1_ER_IRQHandler(void) { HAL_I2C_ER_IRQHandler(&hi2c1); }
void SysTick_Handler(void) {
  HAL_IncTick();
  HAL_SYSTICK_IRQHandler();
}

static void Error_Handler(void) {
  // Blink LED rapidly on fatal error.
  while (1) {
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    HAL_Delay(80);
  }
}
