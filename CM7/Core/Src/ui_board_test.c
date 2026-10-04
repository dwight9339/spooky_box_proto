#include "ui_board_test.h"

#include "main.h"
#include "matrix_writer.h"
#include "ui_input_service.h"
#include "ui_render_service.h"
#include "usb_test.h"
#if defined(SPOOKY_DEMO)
#include "demo_field.h"
#endif

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define UI_MATRIX_ADDRESS_HAL        (0x30U << 1U)
#define UI_MATRIX_STARTUP_MS         10U
#define UI_I2C_TIMEOUT_MS            50U
#define UI_MATRIX_COMMAND_REG        0xFDU
#define UI_MATRIX_COMMAND_LOCK_REG   0xFEU
#define UI_MATRIX_ID_REG             0xFCU
#define UI_MATRIX_CONFIG_REG         0x00U
#define UI_MATRIX_GLOBAL_CURRENT_REG 0x01U
#define UI_MATRIX_RESET_REG          0x3FU
#define UI_MATRIX_EXPECTED_ID        0x60U
#define UI_MATRIX_COLUMN_OFFSET      2U
#define UI_MATRIX_GLOBAL_CURRENT     0x40U
#define UI_MESSAGE_QUEUE_DEPTH       8U
#define UI_MESSAGE_SIZE              224U
#define UI_DISPLAY_WIDTH             128U
#define UI_DISPLAY_HEIGHT            64U
#define UI_DISPLAY_PAGES             (UI_DISPLAY_HEIGHT / 8U)
#define UI_DISPLAY_SPI_TIMEOUT_MS    100U
#define UI_DISPLAY_DEFAULT_OFFSET    0U

typedef struct
{
  const char *name;
  GPIO_TypeDef *port;
  uint16_t pin;
  bool active_high;
} UiSwitch;

typedef struct
{
  const char *name;
  GPIO_TypeDef *a_port;
  uint16_t a_pin;
  GPIO_TypeDef *b_port;
  uint16_t b_pin;
} UiEncoder;

typedef struct
{
  const char *name;
  GPIO_TypeDef *port;
  uint16_t pin;
  bool chase;
} UiLed;

enum
{
  UI_SWITCH_BTN0 = 0,
  UI_SWITCH_BTN1,
  UI_SWITCH_ENC0,
  UI_SWITCH_ENC1,
  UI_SWITCH_ENC2,
  UI_SWITCH_ENC3,
  UI_SWITCH_COUNT
};

static UiSwitch ui_switches[UI_SWITCH_COUNT] =
{
  {"BTN0",     GPIOA, GPIO_PIN_4,  false},
  {"BTN1",     GPIOC, GPIO_PIN_4,  false},
  {"ENC0_BTN", GPIOD, GPIO_PIN_3,  true},
  {"ENC1_BTN", GPIOD, GPIO_PIN_6,  true},
  {"ENC2_BTN", GPIOE, GPIO_PIN_12, true},
  {"ENC3_BTN", GPIOG, GPIO_PIN_9,  true}
};

static UiEncoder ui_encoders[] =
{
  {"ENC0", GPIOD, GPIO_PIN_0, GPIOD, GPIO_PIN_1},
  {"ENC1", GPIOD, GPIO_PIN_4, GPIOD, GPIO_PIN_5},
  {"ENC2", GPIOD, GPIO_PIN_7, GPIOE, GPIO_PIN_10},
  {"ENC3", GPIOF, GPIO_PIN_11, GPIOG, GPIO_PIN_14}
};

/*
 * Every UI LED is driven through a TBD62083 low-side array, so a high MCU
 * output turns the LED on.  The chase covers the two standalone button LEDs
 * first, followed by the twelve encoder RGB channels.
 */
static const UiLed ui_leds[] =
{
  {"BTN0",   GPIOF, GPIO_PIN_6,  true},
  {"BTN1",   GPIOF, GPIO_PIN_7,  true},
  {"ENC0_R", GPIOA, GPIO_PIN_2,  true},
  {"ENC0_G", GPIOF, GPIO_PIN_8,  true},
  {"ENC0_B", GPIOF, GPIO_PIN_9,  true},
  {"ENC1_R", GPIOA, GPIO_PIN_0,  true},
  {"ENC1_G", GPIOA, GPIO_PIN_1,  true},
  {"ENC1_B", GPIOB, GPIO_PIN_15, true},
  {"ENC2_R", GPIOD, GPIO_PIN_14, true},
  {"ENC2_G", GPIOD, GPIO_PIN_15, true},
  {"ENC2_B", GPIOE, GPIO_PIN_14, true},
  {"ENC3_R", GPIOE, GPIO_PIN_9,  true},
  {"ENC3_G", GPIOE, GPIO_PIN_11, true},
  {"ENC3_B", GPIOE, GPIO_PIN_13, true}
};

static I2C_HandleTypeDef *ui_i2c;
static SPI_HandleTypeDef *ui_display_spi;
static volatile bool ui_ready;
static bool ui_watch_enabled;
static bool ui_matrix_enabled;
static bool ui_matrix_animation_active;
/* The matrix feedback service owns the matrix; test commands keep off it. */
static bool ui_matrix_held;
static uint8_t ui_matrix_page;
static bool ui_led_chase_active;
static uint8_t ui_led_chase_index;
static bool ui_display_spi_ready;
static bool ui_display_on;
static char ui_messages[UI_MESSAGE_QUEUE_DEPTH][UI_MESSAGE_SIZE];
static uint8_t ui_message_head;
static uint8_t ui_message_tail;
static uint32_t ui_dropped_messages;

#define UI_ENCODER_COUNT \
  ((uint32_t)(sizeof(ui_encoders) / sizeof(ui_encoders[0])))
#define UI_LED_COUNT \
  ((uint32_t)(sizeof(ui_leds) / sizeof(ui_leds[0])))

_Static_assert(UI_SWITCH_COUNT == UI_INPUT_SWITCH_COUNT,
               "UI switch mapping must match input service");
_Static_assert(UI_ENCODER_COUNT == UI_INPUT_ENCODER_COUNT,
               "UI encoder mapping must match input service");
_Static_assert(UI_LED_COUNT == UI_RENDER_LED_COUNT,
               "UI LED mapping must match render service");

static bool UiReadPin(GPIO_TypeDef *port, uint16_t pin)
{
  return HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET;
}

static uint8_t UiReadEncoder(const UiEncoder *encoder)
{
  return (uint8_t)((UiReadPin(encoder->a_port, encoder->a_pin) ? 2U : 0U) |
                   (UiReadPin(encoder->b_port, encoder->b_pin) ? 1U : 0U));
}

static void UiSampleSwitches(bool raw_switches[UI_SWITCH_COUNT])
{
  uint32_t index;

  for (index = 0U; index < UI_SWITCH_COUNT; ++index)
  {
    raw_switches[index] = UiReadPin(ui_switches[index].port,
                                   ui_switches[index].pin);
  }
}

static void UiSampleEncoders(uint8_t encoder_ab[UI_ENCODER_COUNT])
{
  uint32_t index;

  for (index = 0U; index < UI_ENCODER_COUNT; ++index)
  {
    encoder_ab[index] = UiReadEncoder(&ui_encoders[index]);
  }
}

static uint32_t UiEnterCritical(void)
{
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  return primask;
}

static void UiExitCritical(uint32_t primask)
{
  if (primask == 0U)
  {
    __enable_irq();
  }
}

static void UiQueueMessage(const char *format, ...)
{
  uint8_t next_head;
  va_list arguments;

  next_head = (uint8_t)((ui_message_head + 1U) % UI_MESSAGE_QUEUE_DEPTH);
  if (next_head == ui_message_tail)
  {
    ++ui_dropped_messages;
    return;
  }

  va_start(arguments, format);
  (void)vsnprintf(ui_messages[ui_message_head], UI_MESSAGE_SIZE,
                  format, arguments);
  va_end(arguments);
  printf("[ui] %s", ui_messages[ui_message_head]);
  ui_message_head = next_head;
}

static void UiFlushOneMessage(void)
{
  if ((ui_message_tail != ui_message_head) &&
      UsbTest_SendText(ui_messages[ui_message_tail]))
  {
    ui_message_tail = (uint8_t)((ui_message_tail + 1U) %
                                UI_MESSAGE_QUEUE_DEPTH);
  }
}

static void UiEnableGpioClocks(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
}

static void UiAllLedsOff(void)
{
  uint32_t index;

  for (index = 0U; index < UI_LED_COUNT; ++index)
  {
    HAL_GPIO_WritePin(ui_leds[index].port, ui_leds[index].pin,
                      GPIO_PIN_RESET);
  }
}

static uint32_t UiChaseLedCount(void)
{
  uint32_t count = 0U;
  uint32_t index;

  for (index = 0U; index < UI_LED_COUNT; ++index)
  {
    if (ui_leds[index].chase)
    {
      ++count;
    }
  }
  return count;
}

static uint8_t UiFirstChaseLed(void)
{
  uint8_t index;

  for (index = 0U; index < (uint8_t)UI_LED_COUNT; ++index)
  {
    if (ui_leds[index].chase)
    {
      return index;
    }
  }
  return (uint8_t)UI_LED_COUNT;
}

static uint32_t UiChasePosition(uint8_t led_index)
{
  uint32_t position = 0U;
  uint32_t index;

  for (index = 0U; (index <= led_index) && (index < UI_LED_COUNT); ++index)
  {
    if (ui_leds[index].chase)
    {
      ++position;
    }
  }
  return position;
}

static void UiSetChaseLed(uint8_t index)
{
  UiAllLedsOff();
  if ((index < UI_LED_COUNT) && ui_leds[index].chase)
  {
    HAL_GPIO_WritePin(ui_leds[index].port, ui_leds[index].pin,
                      GPIO_PIN_SET);
    UiQueueMessage("UI LED %s ON %lu/%lu\r\n", ui_leds[index].name,
                   (unsigned long)UiChasePosition(index),
                   (unsigned long)UiChaseLedCount());
  }
}

static void UiSendStatus(void)
{
  uint8_t encoder_ab[UI_ENCODER_COUNT];
  UiInputStatus status;
  uint32_t index;
  uint32_t primask;

  for (index = 0U; index < UI_ENCODER_COUNT; ++index)
  {
    encoder_ab[index] = UiReadEncoder(&ui_encoders[index]);
  }
  primask = UiEnterCritical();
  (void)UiInputService_GetStatus(encoder_ab, &status);
  UiExitCritical(primask);

  UiQueueMessage(
    "OK UI RAW BTN0=%u BTN1=%u "
    "ENC0=A%uB%uS%u/%ld ENC1=A%uB%uS%u/%ld "
    "ENC2=A%uB%uS%u/%ld ENC3=A%uB%uS%u/%ld "
    "WATCH=%u LEDS=%s MATRIX_EN=%u DISPLAY=%s DROPPED=%lu\r\n",
    UiReadPin(ui_switches[UI_SWITCH_BTN0].port,
              ui_switches[UI_SWITCH_BTN0].pin),
    UiReadPin(ui_switches[UI_SWITCH_BTN1].port,
              ui_switches[UI_SWITCH_BTN1].pin),
    (encoder_ab[0] >> 1U) & 1U, encoder_ab[0] & 1U,
    UiReadPin(ui_switches[UI_SWITCH_ENC0].port,
              ui_switches[UI_SWITCH_ENC0].pin),
    (long)status.encoder_count[0],
    (encoder_ab[1] >> 1U) & 1U, encoder_ab[1] & 1U,
    UiReadPin(ui_switches[UI_SWITCH_ENC1].port,
              ui_switches[UI_SWITCH_ENC1].pin),
    (long)status.encoder_count[1],
    (encoder_ab[2] >> 1U) & 1U, encoder_ab[2] & 1U,
    UiReadPin(ui_switches[UI_SWITCH_ENC2].port,
              ui_switches[UI_SWITCH_ENC2].pin),
    (long)status.encoder_count[2],
    (encoder_ab[3] >> 1U) & 1U, encoder_ab[3] & 1U,
    UiReadPin(ui_switches[UI_SWITCH_ENC3].port,
              ui_switches[UI_SWITCH_ENC3].pin),
    (long)status.encoder_count[3],
    ui_watch_enabled, ui_led_chase_active ? "RUN" : "IDLE",
    ui_matrix_enabled, ui_display_on ? "ON" : "OFF",
    (unsigned long)ui_dropped_messages);
}

static void UiServiceSwitches(uint32_t now_ms)
{
  bool raw_switches[UI_SWITCH_COUNT];
  UiInputSwitchEvents events;
  uint32_t index;

  for (index = 0U; index < UI_SWITCH_COUNT; ++index)
  {
    raw_switches[index] = UiReadPin(ui_switches[index].port,
                                   ui_switches[index].pin);
  }
  UiInputService_UpdateSwitches(raw_switches, now_ms, &events);
#if defined(SPOOKY_DEMO)
  for (index = 0U; index < UI_SWITCH_COUNT; ++index)
  {
    const uint8_t mask = (uint8_t)(1U << index);

    if (((events.high_mask | events.low_mask) & mask) != 0U)
    {
      const bool raw = (events.high_mask & mask) != 0U;

      DemoField_OnControl((uint8_t)index,
                          ui_switches[index].active_high ? raw : !raw, now_ms);
    }
  }
#endif
  if (!ui_watch_enabled)
  {
    return;
  }
  for (index = 0U; index < UI_SWITCH_COUNT; ++index)
  {
    const uint8_t mask = (uint8_t)(1U << index);
    const bool went_high = (events.high_mask & mask) != 0U;
    const bool went_low = (events.low_mask & mask) != 0U;

    if (went_high || went_low)
    {
      const bool raw = went_high;
      const bool pressed = ui_switches[index].active_high ? raw : !raw;

      UiQueueMessage("UI EVENT %s %s RAW=%u\r\n", ui_switches[index].name,
                     pressed ? "PRESSED" : "RELEASED", raw);
    }
  }
}

static void UiServiceEncoderEvents(void)
{
  UiInputEncoderEvents events[UI_ENCODER_COUNT];
  uint32_t index;
  uint32_t primask = UiEnterCritical();

  UiInputService_TakeEncoderEvents(events);
  UiExitCritical(primask);

  for (index = 0U; index < UI_ENCODER_COUNT; ++index)
  {
    UiEncoder *encoder = &ui_encoders[index];
    uint8_t current_ab;

#if defined(SPOOKY_DEMO)
    if (events[index].clockwise != events[index].counterclockwise)
    {
      DemoField_OnDetents((uint8_t)index,
                          (int32_t)events[index].clockwise -
                          (int32_t)events[index].counterclockwise, HAL_GetTick());
    }
#endif

    if (!ui_watch_enabled ||
        ((events[index].clockwise == 0U) &&
         (events[index].counterclockwise == 0U)))
    {
      continue;
    }

    current_ab = UiReadEncoder(encoder);
    if (events[index].clockwise != 0U)
    {
      UiQueueMessage("UI EVENT %s DIR=CW STEPS=%lu COUNT=%ld AB=%u%u\r\n",
                     encoder->name, (unsigned long)events[index].clockwise,
                     (long)events[index].count,
                     (current_ab >> 1U) & 1U, current_ab & 1U);
    }
    if (events[index].counterclockwise != 0U)
    {
      UiQueueMessage("UI EVENT %s DIR=CCW STEPS=%lu COUNT=%ld AB=%u%u\r\n",
                     encoder->name,
                     (unsigned long)events[index].counterclockwise,
                     (long)events[index].count,
                     (current_ab >> 1U) & 1U, current_ab & 1U);
    }
  }
}

static bool UiMatrixWrite(uint8_t reg, uint8_t *data, uint16_t length)
{
  return HAL_I2C_Mem_Write(ui_i2c, UI_MATRIX_ADDRESS_HAL, reg,
                           I2C_MEMADD_SIZE_8BIT, data, length,
                           UI_I2C_TIMEOUT_MS) == HAL_OK;
}

static bool UiMatrixSelectPage(uint8_t page)
{
  uint8_t value;

  if (ui_matrix_page == page)
  {
    return true;
  }

  value = 0xC5U;
  if (!UiMatrixWrite(UI_MATRIX_COMMAND_LOCK_REG, &value, 1U))
  {
    return false;
  }
  value = page;
  if (!UiMatrixWrite(UI_MATRIX_COMMAND_REG, &value, 1U))
  {
    return false;
  }
  ui_matrix_page = page;
  return true;
}

static bool UiMatrixWriteRegister(uint8_t page, uint8_t reg, uint8_t value)
{
  return UiMatrixSelectPage(page) && UiMatrixWrite(reg, &value, 1U);
}

static bool UiMatrixFillPage(uint8_t page, uint16_t page_length,
                             uint8_t value)
{
  uint8_t values[32];
  uint16_t offset = 0U;

  (void)memset(values, value, sizeof(values));
  if (!UiMatrixSelectPage(page))
  {
    return false;
  }
  while (offset < page_length)
  {
    uint16_t remaining = page_length - offset;
    uint16_t chunk = (remaining < sizeof(values))
      ? remaining : (uint16_t)sizeof(values);

    if (!UiMatrixWrite((uint8_t)offset, values, chunk))
    {
      return false;
    }
    offset += chunk;
  }
  return true;
}

static void UiMatrixHardwareOff(void)
{
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
  ui_matrix_enabled = false;
  ui_matrix_animation_active = false;
  UiRenderService_StopMatrixAnimation();
  ui_matrix_page = 0xFFU;
}

static bool UiMatrixBlankAndDisable(void)
{
  bool ok = UiMatrixFillPage(0U, 180U, 0U) &&
            UiMatrixFillPage(1U, 171U, 0U) &&
            UiMatrixWriteRegister(4U, UI_MATRIX_CONFIG_REG, 0U);

  UiMatrixHardwareOff();
  return ok;
}

static bool UiMatrixInitialize(uint8_t *device_id)
{
  uint8_t reset_value = 0xAEU;

  *device_id = 0U;
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
  ui_matrix_enabled = true;
  ui_matrix_page = 0xFFU;
  HAL_Delay(UI_MATRIX_STARTUP_MS);

  if (HAL_I2C_IsDeviceReady(ui_i2c, UI_MATRIX_ADDRESS_HAL, 2U,
                            UI_I2C_TIMEOUT_MS) != HAL_OK)
  {
    return false;
  }
  if (HAL_I2C_Mem_Read(ui_i2c, UI_MATRIX_ADDRESS_HAL, UI_MATRIX_ID_REG,
                       I2C_MEMADD_SIZE_8BIT, device_id, 1U,
                       UI_I2C_TIMEOUT_MS) != HAL_OK)
  {
    return false;
  }
  if (*device_id != UI_MATRIX_EXPECTED_ID)
  {
    return false;
  }

  if (!UiMatrixSelectPage(4U) ||
      !UiMatrixWrite(UI_MATRIX_RESET_REG, &reset_value, 1U) ||
      !UiMatrixFillPage(0U, 180U, 0U) ||
      !UiMatrixFillPage(1U, 171U, 0U) ||
      !UiMatrixFillPage(2U, 180U, 0xFFU) ||
      !UiMatrixFillPage(3U, 171U, 0xFFU) ||
      !UiMatrixWriteRegister(4U, UI_MATRIX_GLOBAL_CURRENT_REG,
                             UI_MATRIX_GLOBAL_CURRENT) ||
      !UiMatrixWriteRegister(4U, UI_MATRIX_CONFIG_REG, 1U))
  {
    return false;
  }
  return true;
}

static bool UiMatrixSetPixel(uint8_t logical_x, uint8_t logical_y,
                             uint8_t red, uint8_t green, uint8_t blue)
{
  const MatrixFeedbackRgb colour = {red, green, blue};
  MatrixWriterRun run;

  return MatrixWriter_PixelRun(logical_x, logical_y, colour, &run) &&
         UiMatrixSelectPage(run.page) && UiMatrixWrite(run.reg, run.bytes, run.length);
}

static void UiServiceMatrixAnimation(uint32_t now_ms)
{
  UiRenderMatrixFrame frame;
  uint8_t index;
  bool ok = true;

  if (!ui_matrix_animation_active ||
      !UiRenderService_PrepareMatrixFrame(now_ms, &frame))
  {
    return;
  }

  for (index = 0U; ok && (index < frame.pixel_count); ++index)
  {
    const UiRenderPixel *pixel = &frame.pixels[index];

    ok = UiMatrixSetPixel(pixel->x, pixel->y, pixel->red,
                          pixel->green, pixel->blue);
  }
  UiRenderService_CommitMatrixFrame(now_ms, ok);

  if (!ok)
  {
    uint32_t hal_error = HAL_I2C_GetError(ui_i2c);

    UiMatrixHardwareOff();
    UiQueueMessage("ERR UI MATRIX ANIMATE frame=%u hal=0x%08lX disabled=1\r\n",
                   (unsigned int)frame.phase, (unsigned long)hal_error);
    return;
  }

  if (frame.final_frame)
  {
    bool blanked = UiMatrixBlankAndDisable();

    if (blanked)
    {
      UiQueueMessage(
        "OK UI MATRIX ANIMATE PASS pixels=81 logical=9x9 blanked=1 EN=0\r\n");
    }
    else
    {
      UiQueueMessage(
        "ERR UI MATRIX ANIMATE cleanup hal=0x%08lX EN=0\r\n",
        (unsigned long)HAL_I2C_GetError(ui_i2c));
    }
    return;
  }
}

static void UiServiceLedChase(uint32_t now_ms)
{
  uint8_t next_led;
  bool finished;

  if (!ui_led_chase_active ||
      !UiRenderService_NextLed(now_ms, &next_led, &finished))
  {
    return;
  }

  if (finished)
  {
    ui_led_chase_active = false;
    UiAllLedsOff();
    UiQueueMessage(
      "OK UI LEDS PASS channels=%lu all-off=1\r\n",
      (unsigned long)UiChaseLedCount());
    return;
  }

  ui_led_chase_index = next_led;
  UiSetChaseLed(ui_led_chase_index);
}

static bool UiDisplayConfigureSpi(SPI_HandleTypeDef *spi)
{
  if (spi == NULL)
  {
    return false;
  }

  /*
   * SPI6 is still provisional in the CubeMX project.  Override it here for
   * the SSD1309 bring-up: mode 0, 8-bit, transmit-only, 32 MHz / 4 = 8 MHz.
   */
  spi->Instance = SPI6;
  spi->Init.Mode = SPI_MODE_MASTER;
  spi->Init.Direction = SPI_DIRECTION_2LINES_TXONLY;
  spi->Init.DataSize = SPI_DATASIZE_8BIT;
  spi->Init.CLKPolarity = SPI_POLARITY_LOW;
  spi->Init.CLKPhase = SPI_PHASE_1EDGE;
  spi->Init.NSS = SPI_NSS_SOFT;
  spi->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
  spi->Init.FirstBit = SPI_FIRSTBIT_MSB;
  spi->Init.TIMode = SPI_TIMODE_DISABLE;
  spi->Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  spi->Init.CRCPolynomial = 7U;
  spi->Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  spi->Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  spi->Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  spi->Init.TxCRCInitializationPattern =
    SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  spi->Init.RxCRCInitializationPattern =
    SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  spi->Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  spi->Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  spi->Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  spi->Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;
  spi->Init.IOSwap = SPI_IO_SWAP_DISABLE;
  return HAL_SPI_Init(spi) == HAL_OK;
}

static bool UiDisplayTransfer(bool data, const uint8_t *bytes,
                              uint16_t length)
{
  HAL_StatusTypeDef status;

  if (!ui_display_spi_ready || (bytes == NULL) || (length == 0U))
  {
    return false;
  }
  HAL_GPIO_WritePin(DISP_DC_GPIO_Port, DISP_DC_Pin,
                    data ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(DISP_CS_GPIO_Port, DISP_CS_Pin, GPIO_PIN_RESET);
  status = HAL_SPI_Transmit(ui_display_spi, (uint8_t *)bytes, length,
                            UI_DISPLAY_SPI_TIMEOUT_MS);
  HAL_GPIO_WritePin(DISP_CS_GPIO_Port, DISP_CS_Pin, GPIO_PIN_SET);
  return status == HAL_OK;
}

static bool UiDisplayCommand(uint8_t command)
{
  return UiDisplayTransfer(false, &command, 1U);
}

static bool UiDisplayWriteBuffer(uint8_t column_offset,
                                 const uint8_t *display_buffer)
{
  uint8_t page;

  for (page = 0U; page < UI_DISPLAY_PAGES; ++page)
  {
    uint8_t commands[3] =
    {
      (uint8_t)(0xB0U | page),
      (uint8_t)(column_offset & 0x0FU),
      (uint8_t)(0x10U | (column_offset >> 4U))
    };

    if (!UiDisplayTransfer(false, commands, sizeof(commands)) ||
        !UiDisplayTransfer(true,
                           &display_buffer[(uint16_t)page * UI_DISPLAY_WIDTH],
                           UI_DISPLAY_WIDTH))
    {
      return false;
    }
  }
  return true;
}

static void UiDisplayHardwareOff(void)
{
  if (ui_display_spi_ready)
  {
    (void)UiDisplayCommand(0xAEU);
  }
  HAL_GPIO_WritePin(DISP_RST_GPIO_Port, DISP_RST_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(DISP_CS_GPIO_Port, DISP_CS_Pin, GPIO_PIN_SET);
  ui_display_on = false;
}

static const uint8_t ui_display_init_commands[] =
{
    0xAEU,             /* display off */
    0xD5U, 0xA0U,      /* clock divide / oscillator */
    0xA8U, 0x3FU,      /* 64-row multiplex */
    0xD3U, 0x00U,      /* display offset */
    0x40U,             /* display start line 0 */
    0x20U, 0x02U,      /* page addressing mode */
    0xA1U,             /* segment remap */
    0xC8U,             /* COM scan direction remap */
    0xDAU, 0x12U,      /* COM pin configuration */
    0x81U, 0x6FU,      /* contrast */
    0xD9U, 0xD3U,      /* pre-charge period */
    0xDBU, 0x20U,      /* VCOMH deselect level */
    0x2EU,             /* deactivate scroll */
    0xA4U,             /* show GDDRAM */
    0xA6U              /* normal (not inverted) display */
};

static bool UiDisplayRunTest(uint8_t column_offset)
{
  const uint8_t *display_buffer;

  UiDisplayHardwareOff();
  HAL_Delay(100U);
  HAL_GPIO_WritePin(DISP_RST_GPIO_Port, DISP_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(100U);

  display_buffer = UiRenderService_BuildDisplayTestPattern();
  if (!UiDisplayTransfer(false, ui_display_init_commands,
                         sizeof(ui_display_init_commands)) ||
      !UiDisplayWriteBuffer(column_offset, display_buffer) ||
      !UiDisplayCommand(0xAFU))
  {
    UiDisplayHardwareOff();
    return false;
  }
  ui_display_on = true;
  return true;
}

bool UiBoardTest_Start(I2C_HandleTypeDef *i2c,
                       SPI_HandleTypeDef *display_spi)
{
  GPIO_InitTypeDef gpio = {0};
  bool raw_switches[UI_SWITCH_COUNT];
  uint8_t encoder_ab[UI_ENCODER_COUNT];
  uint32_t index;
  uint32_t now_ms;

  ui_i2c = i2c;
  ui_display_spi = display_spi;
  ui_ready = false;
  ui_watch_enabled = false;
  ui_matrix_enabled = false;
  ui_matrix_animation_active = false;
  ui_matrix_page = 0xFFU;
  ui_led_chase_active = false;
  ui_display_spi_ready = false;
  ui_display_on = false;
  ui_message_head = 0U;
  ui_message_tail = 0U;
  ui_dropped_messages = 0U;
  UiRenderService_Init();

  UiEnableGpioClocks();

  /* Preload every low-side driver control latch low before output mode. */
  UiAllLedsOff();
  for (index = 0U; index < UI_LED_COUNT; ++index)
  {
    gpio.Pin = ui_leds[index].pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ui_leds[index].port, &gpio);
  }

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
  gpio.Pin = GPIO_PIN_5;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &gpio);

  /* Keep the OLED deselected and in reset until a display test is requested. */
  HAL_GPIO_WritePin(DISP_CS_GPIO_Port, DISP_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(DISP_DC_GPIO_Port, DISP_DC_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(DISP_RST_GPIO_Port, DISP_RST_Pin, GPIO_PIN_RESET);
  gpio.Pin = DISP_CS_Pin | DISP_DC_Pin;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOG, &gpio);
  gpio.Pin = DISP_RST_Pin;
  HAL_GPIO_Init(GPIOA, &gpio);
  ui_display_spi_ready = UiDisplayConfigureSpi(ui_display_spi);

  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  for (index = 0U; index < UI_SWITCH_COUNT; ++index)
  {
    gpio.Pin = ui_switches[index].pin;
    HAL_GPIO_Init(ui_switches[index].port, &gpio);
  }
  for (index = 0U; index < UI_ENCODER_COUNT; ++index)
  {
    gpio.Pin = ui_encoders[index].a_pin;
    HAL_GPIO_Init(ui_encoders[index].a_port, &gpio);
    gpio.Pin = ui_encoders[index].b_pin;
    HAL_GPIO_Init(ui_encoders[index].b_port, &gpio);
  }

  now_ms = HAL_GetTick();
  UiSampleSwitches(raw_switches);
  UiSampleEncoders(encoder_ab);
  if (!UiInputService_Init(raw_switches, encoder_ab, now_ms))
  {
    return false;
  }
  ui_ready = true;

  printf("\r\n[ui] Stage 1 UI-board bring-up ready on CM7\r\n");
  printf("[ui] inputs use external backplane pulls; JP9 must be fitted\r\n");
  printf("[ui] 14 LED channels enabled: 2 buttons plus 12 encoder RGB\r\n");
  printf("[ui] matrix uses a centered logical 9x9 area on physical columns 2..10\r\n");
  printf("[ui] SSD1309 display SPI6=%s, 8-bit mode 0 at 8 MHz\r\n",
         ui_display_spi_ready ? "ready" : "FAIL");
  return (ui_i2c != NULL) && ui_display_spi_ready;
}

bool UiBoardTest_HandleCommand(const char *command)
{
  HAL_StatusTypeDef probe_status;
  uint32_t hal_error;

  if (command == NULL)
  {
    return false;
  }
  if ((strncmp(command, "UI", 2U) != 0) ||
      ((command[2] != '\0') && (command[2] != ' ') &&
       (command[2] != '\t')))
  {
    return false;
  }
  if (!ui_ready)
  {
    UiQueueMessage("ERR UI test not initialized\r\n");
    return true;
  }

  if ((strcmp(command, "UI") == 0) ||
      (strcmp(command, "UI STATUS") == 0) ||
      (strcmp(command, "UI INPUTS") == 0))
  {
    UiSendStatus();
  }
  else if (strcmp(command, "UI WATCH START") == 0)
  {
    bool raw_switches[UI_SWITCH_COUNT];
    uint8_t encoder_ab[UI_ENCODER_COUNT];
    uint32_t now_ms = HAL_GetTick();
    uint32_t primask;

    UiSampleSwitches(raw_switches);
    primask = UiEnterCritical();
    UiSampleEncoders(encoder_ab);
    (void)UiInputService_Init(raw_switches, encoder_ab, now_ms);
    UiExitCritical(primask);
    ui_watch_enabled = true;
    UiQueueMessage(
      "OK UI WATCH START counts-reset=1 debounce=%lu-ms sample=1-kHz\r\n",
      (unsigned long)UI_INPUT_DEBOUNCE_MS);
  }
  else if (strcmp(command, "UI WATCH STOP") == 0)
  {
    ui_watch_enabled = false;
    UiQueueMessage("OK UI WATCH STOP\r\n");
  }
  else if ((strcmp(command, "UI LEDS") == 0) ||
           (strcmp(command, "UI LEDS START") == 0))
  {
    ui_led_chase_index = UiFirstChaseLed();
    UiAllLedsOff();
    if (ui_led_chase_index >= UI_LED_COUNT)
    {
      UiQueueMessage("ERR UI no chase-enabled LEDs\r\n");
      return true;
    }
    ui_led_chase_active = true;
    UiQueueMessage(
      "OK UI LEDS START channels=%lu step=%lu-ms\r\n",
      (unsigned long)UiChaseLedCount(),
      (unsigned long)UI_RENDER_LED_STEP_MS);
    UiSetChaseLed(ui_led_chase_index);
    UiRenderService_StartLedChase(HAL_GetTick());
  }
  else if ((strcmp(command, "UI MATRIX") == 0) ||
           (strcmp(command, "UI MATRIX PROBE") == 0))
  {
    if (ui_i2c == NULL)
    {
      UiQueueMessage("ERR UI MATRIX I2C2 unavailable\r\n");
      return true;
    }
    if (ui_matrix_animation_active || ui_matrix_held)
    {
      UiQueueMessage("ERR UI MATRIX busy animation-running=%u feedback=%u\r\n",
                     ui_matrix_animation_active ? 1U : 0U, ui_matrix_held ? 1U : 0U);
      return true;
    }
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
    ui_matrix_enabled = true;
    ui_matrix_page = 0xFFU;
    HAL_Delay(UI_MATRIX_STARTUP_MS);
    probe_status = HAL_I2C_IsDeviceReady(ui_i2c, UI_MATRIX_ADDRESS_HAL,
                                         2U, UI_I2C_TIMEOUT_MS);
    hal_error = HAL_I2C_GetError(ui_i2c);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
    ui_matrix_enabled = false;
    ui_matrix_page = 0xFFU;
    if (probe_status == HAL_OK)
    {
      UiQueueMessage(
        "OK UI MATRIX ACK address=0x30 EN-restored=0\r\n");
    }
    else
    {
      UiQueueMessage(
        "ERR UI MATRIX no-ACK address=0x30 status=%u hal=0x%08lX "
        "EN-restored=0\r\n",
        (unsigned int)probe_status, (unsigned long)hal_error);
    }
  }
  else if (strcmp(command, "UI MATRIX ORIENT") == 0)
  {
    uint8_t device_id;

    /* Three logical corners in distinct colours show how the matrix is
     * mounted: red top-left, green top-right, blue bottom-left. */
    if (ui_i2c == NULL)
    {
      UiQueueMessage("ERR UI MATRIX I2C2 unavailable\r\n");
      return true;
    }
    if (ui_matrix_animation_active || ui_matrix_held)
    {
      UiQueueMessage("ERR UI MATRIX busy animation-running=%u feedback=%u\r\n",
                     ui_matrix_animation_active ? 1U : 0U, ui_matrix_held ? 1U : 0U);
      return true;
    }
    if (!UiMatrixInitialize(&device_id) ||
        !UiMatrixSetPixel(0U, 0U, 255U, 0U, 0U) ||
        !UiMatrixSetPixel(8U, 0U, 0U, 255U, 0U) ||
        !UiMatrixSetPixel(0U, 8U, 0U, 0U, 255U))
    {
      hal_error = HAL_I2C_GetError(ui_i2c);
      UiMatrixHardwareOff();
      UiQueueMessage("ERR UI MATRIX ORIENT hal=0x%08lX EN=0\r\n",
                     (unsigned long)hal_error);
      return true;
    }
    UiQueueMessage("OK UI MATRIX ORIENT red=(0,0) green=(8,0) blue=(0,8); "
                   "UI OFF clears\r\n");
  }
  else if ((strcmp(command, "UI MATRIX ANIMATE") == 0) ||
           (strcmp(command, "UI MATRIX DEMO") == 0))
  {
    uint8_t device_id;

    if (ui_i2c == NULL)
    {
      UiQueueMessage("ERR UI MATRIX I2C2 unavailable\r\n");
      return true;
    }
    if (ui_matrix_animation_active || ui_matrix_held)
    {
      UiQueueMessage("ERR UI MATRIX busy animation-running=%u feedback=%u\r\n",
                     ui_matrix_animation_active ? 1U : 0U, ui_matrix_held ? 1U : 0U);
      return true;
    }
    if (!UiMatrixInitialize(&device_id))
    {
      hal_error = HAL_I2C_GetError(ui_i2c);
      UiMatrixHardwareOff();
      UiQueueMessage(
        "ERR UI MATRIX init id=0x%02X expected=0x%02X hal=0x%08lX EN=0\r\n",
        device_id, UI_MATRIX_EXPECTED_ID, (unsigned long)hal_error);
      return true;
    }
    UiRenderService_StartMatrixAnimation(HAL_GetTick());
    ui_matrix_animation_active = true;
    UiQueueMessage(
      "OK UI MATRIX ANIMATE START logical=9x9 physical-cols=2..10 "
      "step=%lu-ms current=0x%02X\r\n",
      (unsigned long)UI_RENDER_MATRIX_FRAME_MS, UI_MATRIX_GLOBAL_CURRENT);
  }
  else if ((strcmp(command, "UI DISPLAY") == 0) ||
           (strcmp(command, "UI DISPLAY TEST") == 0) ||
           (strcmp(command, "UI DISPLAY TEST 0") == 0) ||
           (strcmp(command, "UI DISPLAY TEST 2") == 0))
  {
    uint8_t column_offset = UI_DISPLAY_DEFAULT_OFFSET;

    if (strcmp(command, "UI DISPLAY TEST 0") == 0)
    {
      column_offset = 0U;
    }
    else if (strcmp(command, "UI DISPLAY TEST 2") == 0)
    {
      column_offset = 2U;
    }
    if (!ui_display_spi_ready)
    {
      UiQueueMessage("ERR UI DISPLAY SPI6 unavailable\r\n");
      return true;
    }
    if (UiDisplayRunTest(column_offset))
    {
      UiQueueMessage(
        "OK UI DISPLAY TEST PASS controller=SSD1309 resolution=128x64 "
        "offset=%u spi=8-MHz\r\n", column_offset);
    }
    else
    {
      UiQueueMessage(
        "ERR UI DISPLAY transfer status=%u hal=0x%08lX reset=0\r\n",
        (unsigned int)ui_display_spi->State,
        (unsigned long)HAL_SPI_GetError(ui_display_spi));
    }
  }
  else if (strcmp(command, "UI DISPLAY OFF") == 0)
  {
    UiDisplayHardwareOff();
    UiQueueMessage("OK UI DISPLAY OFF reset=0 cs=1\r\n");
  }
  else if (strcmp(command, "UI OFF") == 0)
  {
    UiBoardTest_SafeOff();
    UiQueueMessage("OK UI OFF watch=0 leds=0 matrix-en=0 display=off\r\n");
  }
  else
  {
    UiQueueMessage(
      "ERR usage: UI STATUS|WATCH START|WATCH STOP|LEDS|MATRIX PROBE|"
      "MATRIX ANIMATE|DISPLAY TEST [0|2]|DISPLAY OFF|OFF\r\n");
  }
  return true;
}

void UiBoardTest_Service(bool recording)
{
  uint32_t now_ms;

  if (!ui_ready)
  {
    return;
  }
  now_ms = HAL_GetTick();
  UiServiceSwitches(now_ms);
  UiServiceEncoderEvents();
  UiServiceLedChase(now_ms);
  if (recording && ui_matrix_animation_active)
  {
    UiMatrixHardwareOff();
    UiQueueMessage("WARN UI MATRIX ANIMATE suspended recording=1 EN=0\r\n");
  }
  else if (!recording)
  {
    UiServiceMatrixAnimation(now_ms);
  }
  UiFlushOneMessage();
}

void UiBoardTest_Tick1ms(void)
{
  uint8_t encoder_ab[UI_ENCODER_COUNT];

  if (!ui_ready)
  {
    return;
  }
  UiSampleEncoders(encoder_ab);
  UiInputService_Tick1ms(encoder_ab);
}

bool UiBoardTest_MatrixAcquire(void)
{
  uint8_t device_id;

  if (!ui_ready || (ui_i2c == NULL) || ui_matrix_animation_active)
  {
    return false;
  }
  if (!UiMatrixInitialize(&device_id))
  {
    UiMatrixHardwareOff();
    ui_matrix_held = false;
    return false;
  }
  ui_matrix_held = true;
  return true;
}

bool UiBoardTest_MatrixHeld(void)
{
  return ui_matrix_held;
}

bool UiBoardTest_MatrixWriteRun(uint8_t page, uint8_t reg, const uint8_t *bytes,
                                uint8_t length)
{
  uint8_t copy[MATRIX_WRITER_MAX_BYTES];

  if (!ui_matrix_held || !ui_matrix_enabled || (bytes == NULL) || (length == 0U) ||
      (length > sizeof(copy)))
  {
    return false;
  }
  (void)memcpy(copy, bytes, length);
  return UiMatrixSelectPage(page) && UiMatrixWrite(reg, copy, length);
}

void UiBoardTest_MatrixPowerOff(void)
{
  UiMatrixHardwareOff();
}

void UiBoardTest_MatrixRelease(bool blank)
{
  if (!ui_matrix_held)
  {
    return;
  }
  if (blank && ui_matrix_enabled)
  {
    (void)UiMatrixBlankAndDisable();
  }
  UiMatrixHardwareOff();
  ui_matrix_held = false;
}

void UiBoardTest_SafeOff(void)
{
  ui_watch_enabled = false;
  ui_matrix_held = false;
  ui_led_chase_active = false;
  UiRenderService_SafeOff();
  UiAllLedsOff();
  UiMatrixHardwareOff();
  UiDisplayHardwareOff();
}

#if defined(SPOOKY_DEMO)
/* --- Demo-only surfaces (decision 0011 item 12, p04.3) ----------------------- */

/* Button LED PWM: PF6 is TIM16_CH1 and PF7 TIM17_CH1 (AF1). The demo image
 * gives both timers to the M7; the M4 sleeps. Duty is in permille. */
#define UI_DEMO_PWM_PRESCALER 2U
#define UI_DEMO_PWM_PERIOD 1000U

static bool ui_demo_pwm_ready;

static void UiDemoPwmTimerStart(TIM_TypeDef *timer)
{
  timer->CR1 = 0U;
  timer->PSC = UI_DEMO_PWM_PRESCALER;
  timer->ARR = UI_DEMO_PWM_PERIOD - 1U;
  timer->CCR1 = 0U;
  timer->CCMR1 = TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1PE; /* PWM 1 */
  timer->CCER = TIM_CCER_CC1E;
  timer->BDTR = TIM_BDTR_MOE;
  timer->EGR = TIM_EGR_UG;
  timer->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
}

bool UiBoardTest_DemoLightsStart(void)
{
  GPIO_InitTypeDef gpio = {0};

  if (!ui_ready)
  {
    return false;
  }
  __HAL_RCC_TIM16_CLK_ENABLE();
  __HAL_RCC_TIM17_CLK_ENABLE();
  UiDemoPwmTimerStart(TIM16);
  UiDemoPwmTimerStart(TIM17);
  gpio.Pin = ui_leds[0].pin;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = GPIO_AF1_TIM16;
  HAL_GPIO_Init(ui_leds[0].port, &gpio);
  gpio.Pin = ui_leds[1].pin;
  gpio.Alternate = GPIO_AF1_TIM17;
  HAL_GPIO_Init(ui_leds[1].port, &gpio);
  ui_demo_pwm_ready = true;
  return true;
}

void UiBoardTest_DemoSetLights(const uint16_t button_duty[2], const uint8_t encoder_rgb[4])
{
  uint32_t encoder;

  if (!ui_demo_pwm_ready || (button_duty == NULL) || (encoder_rgb == NULL))
  {
    return;
  }
  TIM16->CCR1 = (button_duty[0] > UI_DEMO_PWM_PERIOD) ? UI_DEMO_PWM_PERIOD : button_duty[0];
  TIM17->CCR1 = (button_duty[1] > UI_DEMO_PWM_PERIOD) ? UI_DEMO_PWM_PERIOD : button_duty[1];
  /* ui_leds[2..13]: ENCn_R, _G, _B for encoders 0 to 3. */
  for (encoder = 0U; encoder < 4U; ++encoder)
  {
    uint32_t channel;

    for (channel = 0U; channel < 3U; ++channel)
    {
      const UiLed *led = &ui_leds[2U + (encoder * 3U) + channel];
      const bool on = (encoder_rgb[encoder] & (4U >> channel)) != 0U;

      HAL_GPIO_WritePin(led->port, led->pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
  }
}

bool UiBoardTest_DemoDisplayStart(void)
{
  static const uint8_t blank[UI_DISPLAY_WIDTH * UI_DISPLAY_PAGES];

  if (!ui_display_spi_ready)
  {
    return false;
  }
  UiDisplayHardwareOff();
  HAL_Delay(100U);
  HAL_GPIO_WritePin(DISP_RST_GPIO_Port, DISP_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(100U);
  if (!UiDisplayTransfer(false, ui_display_init_commands,
                         sizeof(ui_display_init_commands)) ||
      !UiDisplayWriteBuffer(UI_DISPLAY_DEFAULT_OFFSET, blank) ||
      !UiDisplayCommand(0xAFU))
  {
    UiDisplayHardwareOff();
    return false;
  }
  ui_display_on = true;
  return true;
}

bool UiBoardTest_DemoDisplayWritePage(uint8_t page, const uint8_t *bytes)
{
  const uint8_t commands[3] =
  {
    (uint8_t)(0xB0U | page),
    (uint8_t)(UI_DISPLAY_DEFAULT_OFFSET & 0x0FU),
    (uint8_t)(0x10U | (UI_DISPLAY_DEFAULT_OFFSET >> 4U))
  };

  if (!ui_display_on || (page >= UI_DISPLAY_PAGES) || (bytes == NULL))
  {
    return false;
  }
  return UiDisplayTransfer(false, commands, sizeof(commands)) &&
         UiDisplayTransfer(true, bytes, UI_DISPLAY_WIDTH);
}

bool UiBoardTest_DemoPressed(uint8_t control)
{
  return (control < UI_SWITCH_COUNT) &&
         (UiReadPin(ui_switches[control].port, ui_switches[control].pin) ==
          ui_switches[control].active_high);
}
#endif
