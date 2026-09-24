/* Extracted from the working M7 bring-up. Hardware settings are unchanged. */
#include "board_diagnostics.h"
#include "target_logger.h"
#include "main.h"
#include "fuel_gauge_test.h"
#include "usb_test.h"
#include <string.h>

bool BoardDiagnostics_StartConsole(void)
{
  GPIO_InitTypeDef gpio = {0};
  UART_HandleTypeDef *uart = &hcom_uart[COM1];

  /* Prototype Spooky Probe console:
   *   PE8 / UART7_TX -> Pico GP5 / UART_RX
   *   PE7 / UART7_RX <- Pico GP4 / UART_TX
   * The future application will move physical UART ownership to CM4, but the
   * current single-core bring-up keeps it on CM7 for probe validation. */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_UART7_CONFIG(RCC_UART7CLKSOURCE_D2PCLK1);
  __HAL_RCC_UART7_CLK_ENABLE();
  __HAL_RCC_UART7_FORCE_RESET();
  __HAL_RCC_UART7_RELEASE_RESET();

  gpio.Pin = DEBUG_UART_RX_Pin | DEBUG_UART_TX_Pin;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF7_UART7;
  HAL_GPIO_Init(DEBUG_UART_TX_GPIO_Port, &gpio);

  memset(uart, 0, sizeof(*uart));
  uart->Instance = UART7;
  uart->Init.BaudRate = 115200U;
  uart->Init.WordLength = UART_WORDLENGTH_8B;
  uart->Init.StopBits = UART_STOPBITS_1;
  uart->Init.Parity = UART_PARITY_NONE;
  uart->Init.Mode = UART_MODE_TX_RX;
  uart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
  uart->Init.OverSampling = UART_OVERSAMPLING_16;
  uart->Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  uart->Init.ClockPrescaler = UART_PRESCALER_DIV1;
  uart->AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if ((HAL_UART_Init(uart) != HAL_OK) ||
      (HAL_UARTEx_SetTxFifoThreshold(uart, UART_TXFIFO_THRESHOLD_1_8) !=
       HAL_OK) ||
      (HAL_UARTEx_SetRxFifoThreshold(uart, UART_RXFIFO_THRESHOLD_1_8) !=
       HAL_OK) ||
      (HAL_UARTEx_DisableFifoMode(uart) != HAL_OK))
  {
    return false;
  }
  TargetLogger_Init(uart);
  return true;
}

void BoardDiagnostics_SendBatteryStatus(bool charging_only)
{
  FuelGaugeTelemetry telemetry;
  char response[192];
  const char *state;

  if (!FuelGaugeTest_ReadTelemetry(&telemetry))
  {
    (void)UsbTest_SendText("ERR BATTERY telemetry unavailable\r\n");
    return;
  }

  if (telemetry.average_current_mA > 5)
  {
    state = "CHARGING";
  }
  else if (telemetry.average_current_mA < -5)
  {
    state = "DISCHARGING";
  }
  else
  {
    state = "IDLE";
  }

  if (charging_only)
  {
    (void)snprintf(response, sizeof(response),
                   "OK CHARGE VBUS=%u STATE=%s CURRENT=%d mA POWER=%d mW "
                   "FULL=%u\r\n",
                   HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_9) == GPIO_PIN_SET,
                   state, telemetry.average_current_mA,
                   telemetry.average_power_mW, telemetry.full);
  }
  else
  {
    (void)snprintf(response, sizeof(response),
                   "OK BATTERY PRESENT=%u SOC=%u%% VOLTAGE=%u mV "
                   "REMAINING=%u mAh FULL=%u mAh DESIGN=%u mAh "
                   "SOH=%u%% FLAGS=0x%04X\r\n",
                   telemetry.battery_present, telemetry.state_of_charge_pct,
                   telemetry.voltage_mV, telemetry.remaining_capacity_mAh,
                   telemetry.full_charge_capacity_mAh,
                   telemetry.design_capacity_mAh,
                   telemetry.state_of_health_pct, telemetry.flags);
  }
  (void)UsbTest_SendText(response);
}
