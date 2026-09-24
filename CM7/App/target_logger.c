#include "target_logger.h"
#include "log_buffer.h"
#include <errno.h>

#define LOG_TX_CHUNK 128U
#define LOG_TX_TIMEOUT_MS 250U

static LogBuffer buffer;
static UART_HandleTypeDef *log_uart;
static uint32_t in_flight;
static uint32_t tx_start_ms;
static uint32_t transmitted_bytes;
static uint32_t transport_dropped_bytes;
static uint32_t transport_errors;
static uint32_t rejected_context;

static uint32_t EnterCritical(void)
{
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  return mask;
}

static void ExitCritical(uint32_t mask)
{
  __set_PRIMASK(mask);
}

/* Caller holds the short IRQ critical section. No waiting, formatting or
 * allocation. The buffer cannot recycle this span until the TX callback. */
static void Kick(void)
{
  uint32_t length;
  const uint8_t *bytes;
  if ((log_uart == NULL) || (in_flight != 0U)) return;
  bytes = LogBuffer_Peek(&buffer, LOG_TX_CHUNK, &length);
  if (length == 0U) return;
  in_flight = length;
  tx_start_ms = HAL_GetTick();
  if (HAL_UART_Transmit_IT(log_uart, bytes, (uint16_t)length) != HAL_OK)
  {
    /* Our UART has a single owner. An unexpected busy/error is a transport
     * failure, not a reason to spin or hold the producer. Retry next service. */
    (void)HAL_UART_AbortTransmit(log_uart);
    ++transport_errors;
    transport_dropped_bytes += in_flight;
    LogBuffer_Consume(&buffer, in_flight);
    in_flight = 0U;
  }
}

void TargetLogger_Init(UART_HandleTypeDef *uart)
{
  if ((uart == NULL) || (log_uart != NULL)) return;
  log_uart = uart;
  HAL_NVIC_SetPriority(UART7_IRQn, 15U, 0U);
  HAL_NVIC_ClearPendingIRQ(UART7_IRQn);
  HAL_NVIC_EnableIRQ(UART7_IRQn);
}

bool TargetLogger_Write(const void *data, uint32_t length)
{
  uint32_t mask;
  bool accepted;
  bool invalid_context = (__get_IPSR() != 0U) || (__get_PRIMASK() != 0U);
  if (length == 0U) return true;
  mask = EnterCritical();
  if (invalid_context || (log_uart == NULL))
  {
    LogBuffer_Reject(&buffer, length);
    if (invalid_context) ++rejected_context;
    ExitCritical(mask);
    return false;
  }
  accepted = LogBuffer_Write(&buffer, data, length);
  Kick();
  ExitCritical(mask);
  return accepted;
}

void TargetLogger_Service(void)
{
  uint32_t mask = EnterCritical();
  if ((in_flight != 0U) && ((HAL_GetTick() - tx_start_ms) >= LOG_TX_TIMEOUT_MS))
  {
    (void)HAL_UART_AbortTransmit(log_uart);
    ++transport_errors;
    transport_dropped_bytes += in_flight;
    LogBuffer_Consume(&buffer, in_flight);
    in_flight = 0U;
  }
  Kick();
  ExitCritical(mask);
}

void TargetLogger_GetStats(TargetLoggerStats *out)
{
  uint32_t mask = EnterCritical();
  *out = (TargetLoggerStats){buffer.count, buffer.high_water,
    buffer.dropped_writes, buffer.dropped_bytes, transport_dropped_bytes,
    transmitted_bytes, transport_errors, rejected_context, in_flight};
  ExitCritical(mask);
}

bool TargetLogger_Quiesce(uint32_t timeout_ms)
{
  uint32_t start;
  uint32_t mask;
  TargetLoggerStats stats;
  if ((__get_IPSR() != 0U) || (__get_PRIMASK() != 0U) ||
      ((SysTick->CTRL & SysTick_CTRL_TICKINT_Msk) == 0U)) return false;
  start = HAL_GetTick();
  for (;;)
  {
    TargetLogger_GetStats(&stats);
    if (stats.queued_bytes == 0U) return true;
    if ((HAL_GetTick() - start) >= timeout_ms) break;
    TargetLogger_Service();
    HAL_Delay(1U);
  }
  mask = EnterCritical();
  if (log_uart != NULL) (void)HAL_UART_AbortTransmit(log_uart);
  transport_dropped_bytes += buffer.count;
  if (buffer.count != 0U) ++transport_errors;
  LogBuffer_Consume(&buffer, buffer.count);
  in_flight = 0U;
  ExitCritical(mask);
  return false;
}

void UART7_IRQHandler(void)
{
  if (log_uart != NULL) HAL_UART_IRQHandler(log_uart);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{
  uint32_t mask;
  if (uart != log_uart) return;
  mask = EnterCritical();
  transmitted_bytes += in_flight;
  LogBuffer_Consume(&buffer, in_flight);
  in_flight = 0U;
  Kick();
  ExitCritical(mask);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
  uint32_t mask;
  if (uart != log_uart) return;
  mask = EnterCritical();
  (void)HAL_UART_AbortTransmit(log_uart);
  ++transport_errors;
  transport_dropped_bytes += in_flight;
  LogBuffer_Consume(&buffer, in_flight);
  in_flight = 0U;
  /* Recovery is deferred to foreground service, never an IRQ retry loop. */
  ExitCritical(mask);
}

#if !defined(SPOOKY_LOGGER_HOST_TEST)
/* Strong override of the generated weak syscall. Best-effort loss is counted
 * rather than returned as EAGAIN, which can poison subsequent stdio output.
 * One write call is not necessarily one printf invocation or complete line. */
int _write(int file, char *ptr, int len)
{
  if ((file != 1) && (file != 2)) { errno = EBADF; return -1; }
  if (len <= 0) return 0;
  (void)TargetLogger_Write(ptr, (uint32_t)len);
  return len;
}

int __io_putchar(int ch)
{
  uint8_t byte = (uint8_t)ch;
  (void)TargetLogger_Write(&byte, 1U);
  return ch;
}
#endif
