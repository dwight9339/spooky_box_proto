#include "logger_load.h"

#include "main.h"
#include "target_logger.h"
#include "usb_test.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOAD_LINE_BYTES      64U
#define LOAD_MAX_PASS_BYTES  2048U  /* Bounds one service pass's own cost. */
#define LOAD_MAX_RATE_BPS    200000U
#define LOAD_MAX_SECONDS     3600U

static bool load_active;
static uint32_t load_rate_bps;
static uint32_t load_seconds;
static uint32_t load_started_ms;
static uint64_t load_produced_bytes;
static uint32_t load_writes;
static uint32_t load_rejected_writes;
static uint32_t load_rejected_bytes;
static uint32_t load_passes;
static uint32_t load_max_write_cycles;
static uint32_t load_max_pass_cycles;
static uint32_t load_faults;
static uint32_t load_fault_idle;
/* Write latency bins: <10, <50, <100, <500, <1000 and >=1000 us. */
static const uint32_t load_hist_limit_us[] = {10U, 50U, 100U, 500U, 1000U};
static uint32_t load_hist[6];

static uint32_t CyclesToMicros(uint32_t cycles)
{
  return (uint32_t)(((uint64_t)cycles * 1000000U) / SystemCoreClock);
}

static void SendStatus(const char *prefix)
{
  char response[256];

  (void)snprintf(response, sizeof(response),
    "%s ACTIVE=%u RATE=%lu ELAPSED_MS=%lu WRITES=%lu BYTES=%lu "
    "REJ_WRITES=%lu REJ_BYTES=%lu MAX_WRITE_US=%lu MAX_PASS_US=%lu "
    "PASSES=%lu FAULTS=%lu FAULT_IDLE=%lu\r\n", prefix,
    load_active ? 1U : 0U, (unsigned long)load_rate_bps,
    (unsigned long)(load_active ? (HAL_GetTick() - load_started_ms) : 0U),
    (unsigned long)load_writes, (unsigned long)load_produced_bytes,
    (unsigned long)load_rejected_writes, (unsigned long)load_rejected_bytes,
    (unsigned long)CyclesToMicros(load_max_write_cycles),
    (unsigned long)CyclesToMicros(load_max_pass_cycles),
    (unsigned long)load_passes, (unsigned long)load_faults,
    (unsigned long)load_fault_idle);
  (void)UsbTest_SendText(response);
}

static void SendHistogram(void)
{
  char response[160];

  (void)snprintf(response, sizeof(response),
    "OK LOG LOAD HIST LT10US=%lu LT50US=%lu LT100US=%lu LT500US=%lu "
    "LT1000US=%lu GE1000US=%lu\r\n", (unsigned long)load_hist[0],
    (unsigned long)load_hist[1], (unsigned long)load_hist[2],
    (unsigned long)load_hist[3], (unsigned long)load_hist[4],
    (unsigned long)load_hist[5]);
  (void)UsbTest_SendText(response);
}

static void StartLoad(uint32_t rate_bps, uint32_t seconds)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  load_rate_bps = rate_bps;
  load_seconds = seconds;
  load_started_ms = HAL_GetTick();
  load_produced_bytes = 0U;
  load_writes = 0U;
  load_rejected_writes = 0U;
  load_rejected_bytes = 0U;
  load_passes = 0U;
  load_max_write_cycles = 0U;
  load_max_pass_cycles = 0U;
  (void)memset(load_hist, 0, sizeof(load_hist));
  load_active = true;
}

/*
 * Stall the in-flight logger chunk by masking UART7's transmit interrupts. The
 * logger's 250 ms foreground watchdog must then abort it and count the loss,
 * exactly as for a transmitter that stops completing.
 */
static bool InjectFault(void)
{
  uint32_t mask = __get_PRIMASK();
  bool injected = false;

  __disable_irq();
  if ((UART7->CR1 & (USART_CR1_TXEIE_TXFNFIE | USART_CR1_TCIE)) != 0U)
  {
    CLEAR_BIT(UART7->CR1, USART_CR1_TXEIE_TXFNFIE | USART_CR1_TCIE);
    injected = true;
  }
  __set_PRIMASK(mask);
  return injected;
}

bool LoggerLoad_HandleCommand(const char *command)
{
  char *end;
  unsigned long rate;
  unsigned long seconds;

  if (strcmp(command, "LOG LOAD STATUS") == 0)
  {
    SendStatus("OK LOG LOAD");
    return true;
  }
  if (strcmp(command, "LOG LOAD HIST") == 0)
  {
    SendHistogram();
    return true;
  }
  if (strcmp(command, "LOG LOAD STOP") == 0)
  {
    load_active = false;
    SendStatus("OK LOG LOAD STOP");
    return true;
  }
  if (strcmp(command, "LOG FAULT") == 0)
  {
    if (InjectFault())
    {
      ++load_faults;
      (void)UsbTest_SendText("OK LOG FAULT injected; watchdog abort in 250 ms\r\n");
    }
    else
    {
      ++load_fault_idle;
      (void)UsbTest_SendText("ERR LOG FAULT no chunk in flight\r\n");
    }
    return true;
  }
  if (strncmp(command, "LOG LOAD ", 9U) != 0)
  {
    return false;
  }
  rate = strtoul(&command[9], &end, 10);
  seconds = (*end == ' ') ? strtoul(end + 1, &end, 10) : 0UL;
  if ((*end != '\0') || (rate == 0UL) || (rate > LOAD_MAX_RATE_BPS) ||
      (seconds == 0UL) || (seconds > LOAD_MAX_SECONDS))
  {
    (void)UsbTest_SendText("ERR usage: LOG LOAD <1..200000 B/s> <1..3600 s>"
                           "|STATUS|HIST|STOP | LOG FAULT\r\n");
    return true;
  }
  StartLoad((uint32_t)rate, (uint32_t)seconds);
  SendStatus("OK LOG LOAD START");
  return true;
}

void LoggerLoad_Service(void)
{
  char line[LOAD_LINE_BYTES + 1U];
  uint32_t now;
  uint32_t elapsed_ms;
  uint64_t due;
  uint32_t pass_bytes = 0U;
  uint32_t pass_start;

  if (!load_active)
  {
    return;
  }
  now = HAL_GetTick();
  elapsed_ms = now - load_started_ms;
  if (elapsed_ms >= (load_seconds * 1000U))
  {
    load_active = false;
    return;
  }
  due = ((uint64_t)load_rate_bps * elapsed_ms) / 1000U;
  pass_start = DWT->CYCCNT;
  while ((load_produced_bytes + LOAD_LINE_BYTES <= due) &&
         (pass_bytes < LOAD_MAX_PASS_BYTES))
  {
    uint32_t write_start;
    uint32_t write_cycles;
    uint32_t write_us;
    uint32_t bin = 0U;
    int length = snprintf(line, sizeof(line),
      "[load] seq=%010lu t=%010lu ", (unsigned long)load_writes,
      (unsigned long)now);

    (void)memset(&line[length], '.', LOAD_LINE_BYTES - 2U - (uint32_t)length);
    line[LOAD_LINE_BYTES - 2U] = '\r';
    line[LOAD_LINE_BYTES - 1U] = '\n';
    write_start = DWT->CYCCNT;
    if (!TargetLogger_Write(line, LOAD_LINE_BYTES))
    {
      ++load_rejected_writes;
      load_rejected_bytes += LOAD_LINE_BYTES;
    }
    write_cycles = DWT->CYCCNT - write_start;
    if (write_cycles > load_max_write_cycles)
    {
      load_max_write_cycles = write_cycles;
    }
    write_us = CyclesToMicros(write_cycles);
    while ((bin < 5U) && (write_us >= load_hist_limit_us[bin]))
    {
      ++bin;
    }
    ++load_hist[bin];
    ++load_writes;
    load_produced_bytes += LOAD_LINE_BYTES;
    pass_bytes += LOAD_LINE_BYTES;
  }
  if (pass_bytes != 0U)
  {
    uint32_t pass_cycles = DWT->CYCCNT - pass_start;

    ++load_passes;
    if (pass_cycles > load_max_pass_cycles)
    {
      load_max_pass_cycles = pass_cycles;
    }
  }
}
