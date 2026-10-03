#include "fake_hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
TestSysTick test_systick = {SysTick_CTRL_TICKINT_Msk};
uint32_t test_tick, test_primask, test_ipsr;
HAL_StatusTypeDef test_tx_result = HAL_OK;
bool test_auto_complete;
uint8_t test_output[32768];
uint32_t test_output_length, test_abort_count;
const uint8_t *test_tx_bytes;
uint16_t test_tx_length;
static UART_HandleTypeDef *test_uart;
uint32_t __get_PRIMASK(void) { return test_primask; }
uint32_t __get_IPSR(void) { return test_ipsr; }
void __disable_irq(void) { test_primask = 1U; }
void __set_PRIMASK(uint32_t mask) { test_primask = mask; }
uint32_t HAL_GetTick(void) { return test_tick; }
void HAL_Delay(uint32_t ms)
{
  test_tick += ms;
  if (test_auto_complete) TestComplete();
}
void HAL_NVIC_SetPriority(int irq, uint32_t preempt, uint32_t sub)
{ (void)irq; (void)preempt; (void)sub; }
void HAL_NVIC_ClearPendingIRQ(int irq) { (void)irq; }
void HAL_NVIC_EnableIRQ(int irq) { (void)irq; }
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart,
                                     const uint8_t *bytes, uint16_t length)
{
  if (test_tx_result != HAL_OK) return test_tx_result;
  if (test_tx_length != 0U) return HAL_BUSY;
  test_uart = uart;
  test_tx_bytes = bytes;
  test_tx_length = length;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *uart)
{
  (void)uart;
  ++test_abort_count;
  test_tx_length = 0U;
  return HAL_OK;
}
void HAL_UART_IRQHandler(UART_HandleTypeDef *uart) { (void)uart; }
void TestComplete(void)
{
  uint32_t previous = test_ipsr;
  if (test_tx_length == 0U) return;
  /* Always on, unlike assert(): the copy below must never overrun. */
  if (test_output_length + test_tx_length > sizeof(test_output))
  {
    fprintf(stderr, "fake_hal: test_output overflow\n");
    abort();
  }
  memcpy(test_output + test_output_length, test_tx_bytes, test_tx_length);
  test_output_length += test_tx_length;
  test_tx_length = 0U;
  test_ipsr = 23U;
  HAL_UART_TxCpltCallback(test_uart);
  test_ipsr = previous;
}
