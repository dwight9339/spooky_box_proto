#include "isr_timing.h"

#include "main.h"
#include "usb_test.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ISR_VECTORS        166U /* 16 system exceptions + 150 STM32H755 IRQs */
#define ISR_FIRST_TIMED    15U  /* SysTick; faults, SVC and PendSV stay direct */
#define ISR_MAX_DEPTH      17U  /* 16 priority levels plus the foreground */
#define ISR_BURST_GAP_US   2U
#define ISR_BURST_BINS     7U

typedef void (*IsrHandler)(void);

typedef struct IsrStat
{
  uint32_t count;
  uint32_t max_cycles;      /* Inclusive of nested handlers. */
  uint32_t max_excl_cycles; /* Nested handlers subtracted. */
  uint32_t ge100us;         /* Exclusive durations of at least 100 us. */
  uint32_t ge500us;
  uint64_t total_excl_cycles;
} IsrStat;

static IsrHandler ram_vectors[ISR_VECTORS] __attribute__((aligned(1024)));
static const IsrHandler *flash_vectors;
static IsrStat stats[ISR_VECTORS];
static uint32_t depth;
static uint32_t child_cycles[ISR_MAX_DEPTH + 1U];
static uint32_t gap_cycles;
static uint32_t cycles_100us;
static uint32_t cycles_500us;

/* Burst: outermost handlers separated by less than ISR_BURST_GAP_US. */
static uint32_t last_outer_exit;
static uint32_t burst_start;
static uint32_t burst_isrs;
static uint32_t burst_top_exc;
static uint32_t burst_top_cycles;
static uint32_t burst_last_cycles;
static uint32_t burst_count;
static uint32_t burst_max_cycles;
static uint32_t burst_max_isrs;
static uint32_t burst_max_top_exc;
static uint32_t burst_max_top_cycles;
static uint32_t burst_max_tick;
/* Finished-burst bins: <50, <100, <250, <500, <1000, <2000 and >=2000 us. */
static const uint32_t burst_limit_us[ISR_BURST_BINS - 1U] = {50U, 100U, 250U, 500U, 1000U, 2000U};
static uint32_t burst_limit_cycles[ISR_BURST_BINS - 1U];
static uint32_t burst_hist[ISR_BURST_BINS];

static uint32_t CyclesToMicros(uint32_t cycles)
{
  return (uint32_t)(((uint64_t)cycles * 1000000U) / SystemCoreClock);
}

static uint32_t MicrosToCycles(uint32_t micros)
{
  return (uint32_t)(((uint64_t)SystemCoreClock * micros) / 1000000U);
}

static void BurstFinish(void)
{
  uint32_t bin = 0U;

  if (burst_isrs == 0U)
  {
    return;
  }
  while ((bin < (ISR_BURST_BINS - 1U)) && (burst_last_cycles >= burst_limit_cycles[bin]))
  {
    ++bin;
  }
  ++burst_hist[bin];
  ++burst_count;
}

static void IsrTiming_Trampoline(void)
{
  const uint32_t exc = __get_IPSR() & 0x1FFU;
  uint32_t primask = __get_PRIMASK();
  uint32_t level;
  uint32_t start;
  uint32_t end;
  uint32_t inclusive;
  uint32_t exclusive;
  IsrStat *stat;

  __disable_irq();
  start = DWT->CYCCNT;
  level = ++depth;
  if (level == 1U)
  {
    if ((start - last_outer_exit) >= gap_cycles)
    {
      BurstFinish();
      burst_start = start;
      burst_isrs = 0U;
      burst_top_cycles = 0U;
    }
  }
  if (level <= ISR_MAX_DEPTH)
  {
    child_cycles[level] = 0U;
  }
  __set_PRIMASK(primask);

  if (exc < ISR_VECTORS)
  {
    flash_vectors[exc]();
  }

  __disable_irq();
  end = DWT->CYCCNT;
  inclusive = end - start;
  exclusive = inclusive - ((level <= ISR_MAX_DEPTH) ? child_cycles[level] : 0U);
  --depth;
  if ((depth >= 1U) && (depth <= ISR_MAX_DEPTH))
  {
    child_cycles[depth] += inclusive;
  }
  if (exc < ISR_VECTORS)
  {
    stat = &stats[exc];
    ++stat->count;
    stat->total_excl_cycles += exclusive;
    if (inclusive > stat->max_cycles) stat->max_cycles = inclusive;
    if (exclusive > stat->max_excl_cycles) stat->max_excl_cycles = exclusive;
    if (exclusive >= cycles_100us) ++stat->ge100us;
    if (exclusive >= cycles_500us) ++stat->ge500us;
  }
  ++burst_isrs;
  if (exclusive > burst_top_cycles)
  {
    burst_top_cycles = exclusive;
    burst_top_exc = exc;
  }
  if (depth == 0U)
  {
    last_outer_exit = end;
    burst_last_cycles = end - burst_start;
    if (burst_last_cycles > burst_max_cycles)
    {
      burst_max_cycles = burst_last_cycles;
      burst_max_isrs = burst_isrs;
      burst_max_top_exc = burst_top_exc;
      burst_max_top_cycles = burst_top_cycles;
      burst_max_tick = HAL_GetTick();
    }
  }
  __set_PRIMASK(primask);
}

static void ResetStats(void)
{
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  (void)memset(stats, 0, sizeof(stats));
  burst_isrs = 0U;
  burst_count = 0U;
  burst_max_cycles = 0U;
  burst_max_isrs = 0U;
  burst_max_top_exc = 0U;
  burst_max_top_cycles = 0U;
  burst_max_tick = 0U;
  (void)memset(burst_hist, 0, sizeof(burst_hist));
  /* Thresholds follow the current core clock. */
  gap_cycles = MicrosToCycles(ISR_BURST_GAP_US);
  cycles_100us = MicrosToCycles(100U);
  cycles_500us = MicrosToCycles(500U);
  for (uint32_t i = 0U; i < (ISR_BURST_BINS - 1U); ++i)
  {
    burst_limit_cycles[i] = MicrosToCycles(burst_limit_us[i]);
  }
  __set_PRIMASK(primask);
}

void IsrTiming_Install(void)
{
  uint32_t primask = __get_PRIMASK();

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  ResetStats();
  __disable_irq();
  flash_vectors = (const IsrHandler *)SCB->VTOR;
  for (uint32_t i = 0U; i < ISR_VECTORS; ++i)
  {
    ram_vectors[i] = (i >= ISR_FIRST_TIMED) ? IsrTiming_Trampoline : flash_vectors[i];
  }
  __DSB();
  SCB->VTOR = (uint32_t)ram_vectors;
  __DSB();
  __ISB();
  __set_PRIMASK(primask);
}

static void SendList(void)
{
  char response[256];
  int length = snprintf(response, sizeof(response), "OK ISR LIST EXC=");

  for (uint32_t i = 0U; (i < ISR_VECTORS) && (length > 0) &&
                        ((size_t)length < (sizeof(response) - 8U)); ++i)
  {
    if (stats[i].count != 0U)
    {
      length += snprintf(&response[length], sizeof(response) - (size_t)length,
                         "%lu,", (unsigned long)i);
    }
  }
  if ((length > 0) && (response[length - 1] == ','))
  {
    --length;
  }
  (void)snprintf(&response[length], sizeof(response) - (size_t)length,
                 " CLOCK_HZ=%lu\r\n", (unsigned long)SystemCoreClock);
  (void)UsbTest_SendText(response);
}

static void SendStat(uint32_t exc)
{
  char response[256];
  IsrStat stat;
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  stat = stats[exc];
  __set_PRIMASK(primask);
  (void)snprintf(response, sizeof(response),
    "OK ISR EXC=%lu IRQ=%ld COUNT=%lu MAX_US=%lu MAX_EXCL_US=%lu "
    "MAX_EXCL_CYC=%lu TOTAL_EXCL_US=%lu GE100US=%lu GE500US=%lu\r\n",
    (unsigned long)exc, (long)exc - 16L, (unsigned long)stat.count,
    (unsigned long)CyclesToMicros(stat.max_cycles),
    (unsigned long)CyclesToMicros(stat.max_excl_cycles),
    (unsigned long)stat.max_excl_cycles,
    (unsigned long)((stat.total_excl_cycles * 1000000U) / SystemCoreClock),
    (unsigned long)stat.ge100us, (unsigned long)stat.ge500us);
  (void)UsbTest_SendText(response);
}

static void SendBurst(void)
{
  char response[256];

  (void)snprintf(response, sizeof(response),
    "OK ISR BURST MAX_US=%lu MAX_ISRS=%lu MAX_TOP_EXC=%lu MAX_TOP_US=%lu "
    "MAX_AT_MS=%lu BURSTS=%lu LT50US=%lu LT100US=%lu LT250US=%lu LT500US=%lu "
    "LT1000US=%lu LT2000US=%lu GE2000US=%lu\r\n",
    (unsigned long)CyclesToMicros(burst_max_cycles), (unsigned long)burst_max_isrs,
    (unsigned long)burst_max_top_exc, (unsigned long)CyclesToMicros(burst_max_top_cycles),
    (unsigned long)burst_max_tick, (unsigned long)burst_count,
    (unsigned long)burst_hist[0], (unsigned long)burst_hist[1],
    (unsigned long)burst_hist[2], (unsigned long)burst_hist[3],
    (unsigned long)burst_hist[4], (unsigned long)burst_hist[5],
    (unsigned long)burst_hist[6]);
  (void)UsbTest_SendText(response);
}

bool IsrTiming_HandleCommand(const char *command)
{
  char *end = NULL;
  unsigned long exc;

  if ((command == NULL) || (strncmp(command, "ISR", 3U) != 0) ||
      ((command[3] != '\0') && (command[3] != ' ')))
  {
    return false;
  }
  if (strcmp(command, "ISR RESET") == 0)
  {
    ResetStats();
    (void)UsbTest_SendText("OK ISR RESET\r\n");
  }
  else if (strcmp(command, "ISR LIST") == 0)
  {
    SendList();
  }
  else if (strcmp(command, "ISR BURST") == 0)
  {
    SendBurst();
  }
  else if (strncmp(command, "ISR GET ", 8U) == 0)
  {
    exc = strtoul(&command[8], &end, 10);
    if ((end == &command[8]) || (*end != '\0') || (exc >= ISR_VECTORS))
    {
      (void)UsbTest_SendText("ERR ISR GET exception must be 0..165\r\n");
    }
    else
    {
      SendStat((uint32_t)exc);
    }
  }
  else
  {
    (void)UsbTest_SendText("ERR usage: ISR LIST|GET <exception>|BURST|RESET\r\n");
  }
  return true;
}
