#include "log_buffer.h"
#include "diag_history.h"
#include "target_logger.h"
#include "diagnostics.h"
#include "app_events.h"
#include "fake_hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
  exit(EXIT_FAILURE); } } while (0)

static bool usb_busy;
static char usb_lines[300][256];
static uint32_t usb_count;
bool UsbTest_SendText(const char *text)
{
  CHECK(strlen(text) < 256U);
  CHECK(strlen(text) >= 2U);
  CHECK(strcmp(text + strlen(text) - 2U, "\r\n") == 0);
  if (usb_busy) return false;
  CHECK(usb_count < 300U);
  strcpy(usb_lines[usb_count++], text);
  return true;
}

static void test_buffer(void)
{
  LogBuffer b = {0};
  uint8_t data[512];
  uint32_t length;
  memset(data, 0xA5, sizeof(data));
  CHECK(LogBuffer_Write(&b, NULL, 0U));
  CHECK(!LogBuffer_Write(&b, data, 513U));
  for (uint32_t i = 0; i < 8U; ++i) CHECK(LogBuffer_Write(&b, data, 512U));
  CHECK(!LogBuffer_Write(&b, data, 1U));
  CHECK(b.count == 4096U && b.high_water == 4096U && b.dropped_bytes == 514U);
  LogBuffer_Consume(&b, 4000U);
  memset(data, 0x5A, sizeof(data));
  CHECK(LogBuffer_Write(&b, data, 512U));
  const uint8_t *p = LogBuffer_Peek(&b, 512U, &length);
  CHECK(length == 96U && p[0] == 0xA5 && p[95] == 0xA5);
  LogBuffer_Consume(&b, length);
  p = LogBuffer_Peek(&b, 512U, &length);
  CHECK(length == 512U && memcmp(p, data, 512U) == 0);
  LogBuffer_Consume(&b, UINT32_MAX);
  CHECK(b.count == 0U);
}

static void test_history(void)
{
  DiagHistory h;
  DiagEvent e;
  DiagHistory_Init(&h);
  CHECK(!DiagHistory_Get(&h, 0U, &e));
  DiagHistory_Add(&h, 10U, DIAG_SD_ERROR, 1U, 0U);
  for (uint32_t i = 0; i < 140U; ++i)
    DiagHistory_Add(&h, i, DIAG_SD_WRITE, i, 0U);
  CHECK(h.summary.count == 128U && h.summary.overwritten == 13U);
  CHECK(!DiagHistory_Get(&h, 1U, &e));
  CHECK(h.summary.last_fault.sequence == 1U && h.summary.have_fault);
  CHECK(h.summary.max_sd_write_ms == 139U && h.summary.totals[DIAG_SD_WRITE] == 140U);
  CHECK(DiagHistory_Get(&h, 141U, &e) && e.arg0 == 139U);
  DiagHistory_Init(&h);
  h.next_sequence = UINT32_MAX;
  DiagHistory_Add(&h, UINT32_MAX, DIAG_BOOT, 1U, 7U);
  DiagHistory_Add(&h, 0U, DIAG_LOOP_STALL, 75U, 0U);
  CHECK(DiagHistory_Get(&h, UINT32_MAX, &e) && e.type == DIAG_BOOT);
  CHECK(DiagHistory_Get(&h, 0U, &e) && e.type == DIAG_LOOP_STALL);
  CHECK(!DiagHistory_Get(&h, 1U, &e));
  CHECK(h.summary.max_loop_gap_ms == 75U);
  DiagHistory_Add(&h, 1U, DIAG_FOREGROUND_BUDGET,
                  FOREGROUND_SERVICE_USB, 11U);
  CHECK(h.summary.have_fault);
  CHECK(h.summary.last_fault.type == DIAG_FOREGROUND_BUDGET);
}

static void test_logger(void)
{
  static UART_HandleTypeDef uart;
  UART_HandleTypeDef other = {2U};
  TargetLoggerStats s;
  uint8_t data[512];
  CHECK(!TargetLogger_Write("early", 5U));
  TargetLogger_Init(&uart);
  for (uint32_t i = 0; i < 8U; ++i)
  {
    memset(data, (int)i, sizeof(data));
    CHECK(TargetLogger_Write(data, sizeof(data)));
  }
  CHECK(!TargetLogger_Write("full", 4U));
  HAL_UART_TxCpltCallback(&other);
  TargetLogger_GetStats(&s);
  CHECK(s.queued_bytes == 4096U && s.in_flight == 128U);
  /* Refill freed space while UART still owns another span. */
  for (uint32_t i = 0; i < 4U; ++i) TestComplete();
  memset(data, 8, sizeof(data));
  CHECK(TargetLogger_Write(data, sizeof(data)));
  while (test_tx_length != 0U) TestComplete();
  CHECK(test_output_length == 4608U);
  for (uint32_t i = 0; i < 4608U; ++i)
    CHECK(test_output[i] == (uint8_t)(i / 512U));
  TargetLogger_GetStats(&s);
  CHECK(s.transmitted_bytes == 4608U && s.queued_bytes == 0U);
  CHECK(s.dropped_writes == 2U && s.dropped_bytes == 9U);

  test_ipsr = 10U;
  CHECK(!TargetLogger_Write("irq", 3U));
  test_ipsr = 0U;
  test_primask = 1U;
  CHECK(!TargetLogger_Write("masked", 6U));
  CHECK(test_primask == 1U);
  TargetLogger_GetStats(&s);
  CHECK(test_primask == 1U && s.rejected_context == 2U);
  CHECK(!TargetLogger_Quiesce(1U));
  test_primask = 0U;

  test_tx_result = HAL_BUSY;
  CHECK(TargetLogger_Write("busy", 4U));
  TargetLogger_GetStats(&s);
  CHECK(s.transport_errors == 1U && s.transport_dropped_bytes == 4U);
  test_tx_result = HAL_OK;
  test_tick = UINT32_MAX - 99U;
  CHECK(TargetLogger_Write(data, 512U));
  test_tick += 249U;
  TargetLogger_Service();
  TargetLogger_GetStats(&s);
  CHECK(s.transport_errors == 1U);
  ++test_tick;
  TargetLogger_Service();
  TargetLogger_GetStats(&s);
  CHECK(s.transport_errors == 2U && s.queued_bytes == 384U && s.in_flight == 128U);
  HAL_UART_ErrorCallback(&uart);
  CHECK(test_tx_length == 0U);
  TargetLogger_GetStats(&s);
  CHECK(s.transport_errors == 3U && s.queued_bytes == 256U);
  TargetLogger_Service();
  test_auto_complete = true;
  CHECK(TargetLogger_Quiesce(20U));
  test_auto_complete = false;
  CHECK(TargetLogger_Write("abort", 5U));
  CHECK(!TargetLogger_Quiesce(2U));
  TargetLogger_GetStats(&s);
  CHECK(s.queued_bytes == 0U && s.in_flight == 0U && test_tx_length == 0U);
  CHECK(s.transport_errors == 4U && s.transport_dropped_bytes == 265U);
  test_systick.CTRL = 0U;
  CHECK(!TargetLogger_Quiesce(10U));
  test_systick.CTRL = SysTick_CTRL_TICKINT_Msk;
}

static void cli_reset(void)
{
  usb_count = 0U;
  usb_busy = false;
  test_tick = 0U;
  Diagnostics_Init();
}

static void test_cli(void)
{
  cli_reset();
  CHECK(!Diagnostics_HandleCommand("DIAGNOSTIC"));
  CHECK(!Diagnostics_HandleCommand("LOGGER"));
  CHECK(Diagnostics_HandleCommand("DIAG LAST"));
  Diagnostics_Service();
  CHECK(strcmp(usb_lines[0], "OK DIAG LAST NONE\r\n") == 0);
  usb_count = 0U;
  CHECK(Diagnostics_HandleCommand("DIAG DUMP"));
  usb_busy = true;
  for (uint32_t i = 0; i < 5U; ++i) Diagnostics_Service();
  CHECK(usb_count == 0U);
  usb_busy = false;
  Diagnostics_Service();
  CHECK(strcmp(usb_lines[0], "OK DIAG DUMP V=1 CORE=7 FIRST=1 COUNT=1\r\n") == 0);
  Diagnostics_Record(DIAG_SD_ERROR, 1U, 0U);
  Diagnostics_Service();
  CHECK(strstr(usb_lines[1], "SEQ=1 MS=0 EVENT=BOOT A=1 B=7") != NULL);
  Diagnostics_Service();
  CHECK(strcmp(usb_lines[2], "OK DIAG END COUNT=1 GAPS=0\r\n") == 0);
  CHECK(Diagnostics_HandleCommand("DIAG LAST"));
  Diagnostics_Service();
  CHECK(strstr(usb_lines[3], "EVENT=SD_ERROR A=1 B=0") != NULL);

  cli_reset();
  CHECK(Diagnostics_HandleCommand("DIAG DUMP"));
  Diagnostics_Service();
  for (uint32_t i = 0; i < 128U; ++i) Diagnostics_Record(DIAG_SD_WRITE, i, 0U);
  Diagnostics_Service();
  Diagnostics_Service();
  CHECK(strcmp(usb_lines[1], "DIAG GAP SEQ=1\r\n") == 0);
  CHECK(strcmp(usb_lines[2], "OK DIAG END COUNT=1 GAPS=1\r\n") == 0);
  CHECK(Diagnostics_HandleCommand("DIAG STATUS"));
  Diagnostics_Service();
  CHECK(strstr(usb_lines[3], "COUNT=128 OVERWRITTEN=1 SD_MAX_MS=127") != NULL);

  cli_reset();
  CHECK(Diagnostics_HandleCommand("DIAG DUMP"));
  usb_busy = true;
  Diagnostics_Service();
  test_tick = 5000U;
  Diagnostics_Service();
  usb_busy = false;
  CHECK(Diagnostics_HandleCommand("DIAG LAST"));
  Diagnostics_Service();
  CHECK(strstr(usb_lines[0], "EVENT=LOG_ERROR A=4 B=265") != NULL);
  CHECK(Diagnostics_HandleCommand("DIAG DUMP"));
  CHECK(Diagnostics_HandleCommand("DIAG STATUS"));
  CHECK(strstr(usb_lines[1], "ERR DIAG reply active") != NULL);
  CHECK(Diagnostics_HandleCommand("DIAG STOP"));
  CHECK(strcmp(usb_lines[2], "OK DIAG STOP\r\n") == 0);
  Diagnostics_Service();
  CHECK(usb_count == 3U);

  cli_reset();
  CHECK(Diagnostics_HandleCommand("HELP"));
  for (uint32_t i = 0; i < 10U; ++i) Diagnostics_Service();
  CHECK(usb_count == 9U);
  CHECK(strstr(usb_lines[2], "LOG STATUS") != NULL);
  CHECK(strstr(usb_lines[6], "WAV FETCH") != NULL);
  CHECK(Diagnostics_HandleCommand("LOG STATUS"));
  Diagnostics_Service();
  CHECK(strstr(usb_lines[9], "TX_ERRORS=4") != NULL);
  CHECK(test_primask == 0U);
}

static void test_foreground_latency(void)
{
  cli_reset();
  Diagnostics_ObserveForeground(FOREGROUND_SERVICE_USB, 99U, false);
  Diagnostics_ObserveForeground(FOREGROUND_SERVICE_USB, 10U, true);
  Diagnostics_ObserveForeground(FOREGROUND_SERVICE_USB, 11U, true);
  Diagnostics_ObserveForeground(FOREGROUND_SERVICE_RECORDER, 51U, true);
  Diagnostics_ObserveForeground(FOREGROUND_SERVICE_LOOP, 76U, true);
  CHECK(Diagnostics_HandleCommand("DIAG LATENCY"));
  for (uint32_t i = 0U; i < FOREGROUND_SERVICE_COUNT + 2U; ++i)
    Diagnostics_Service();
  CHECK(strstr(usb_lines[0], "BLOCK_MS=86 SERVICES=14 recording-only=1") != NULL);
  CHECK(strstr(usb_lines[1], "SERVICE=LOOP BUDGET_MS=75 MAX_MS=76 VIOLATIONS=1") != NULL);
  CHECK(strstr(usb_lines[4], "SERVICE=RECORDER BUDGET_MS=70 MAX_MS=51 VIOLATIONS=0") != NULL);
  CHECK(strstr(usb_lines[6], "SERVICE=USB BUDGET_MS=10 MAX_MS=11 VIOLATIONS=1") != NULL);
  CHECK(strcmp(usb_lines[FOREGROUND_SERVICE_COUNT + 1U],
               "OK DIAG LATENCY END\r\n") == 0);
  CHECK(Diagnostics_HandleCommand("DIAG LAST"));
  Diagnostics_Service();
  CHECK(strstr(usb_lines[FOREGROUND_SERVICE_COUNT + 2U],
               "EVENT=FOREGROUND_BUDGET A=0 B=76") != NULL);
}

static void count_dispatch(void *context, const EvqEvent *event)
{
  (void)event;
  ++*(uint32_t *)context;
}

/* Decision 0007 item 14: queue counters on one DIAG line; a rejected internal
 * event is recorded as a fault. Uses the firmware queue instance, so it runs last. */
static void test_queue_diag(void)
{
  uint32_t dispatched = 0U;
  cli_reset();
  CHECK(Diagnostics_HandleCommand("DIAG QUEUE"));
  Diagnostics_Service();
  CHECK(strcmp(usb_lines[0], "OK DIAG QUEUE CAP=32 RESERVE=8 COUNT=0 PEAK=0 POSTED=0 "
    "DISPATCHED=0 REJ_INPUT=0 REJ_CMD=0 REJ_INTERNAL=0 RECONCILES=0 MAX_WAIT_MS=0\r\n") == 0);
  test_tick = 10U;
  for (uint32_t i = 0; i < 33U; ++i) (void)AppEvents_Post(EVQ_CLASS_INTERNAL, 1U, i, 0U);
  CHECK(!AppEvents_Post(EVQ_CLASS_EXTERNAL_COMMAND, 2U, 0U, 0U));
  test_tick = 40U;
  CHECK(AppEvents_Service(count_dispatch, &dispatched) == 32U && dispatched == 32U);
  CHECK(Diagnostics_HandleCommand("DIAG QUEUE"));
  Diagnostics_Service();
  CHECK(strcmp(usb_lines[1], "OK DIAG QUEUE CAP=32 RESERVE=8 COUNT=0 PEAK=32 POSTED=32 "
    "DISPATCHED=32 REJ_INPUT=0 REJ_CMD=1 REJ_INTERNAL=1 RECONCILES=0 MAX_WAIT_MS=30\r\n") == 0);
  /* The loss is sampled with the logger counters, at most once a second. */
  test_tick = 1000U;
  Diagnostics_Service();
  CHECK(Diagnostics_HandleCommand("DIAG LAST"));
  Diagnostics_Service();
  CHECK(strstr(usb_lines[2], "EVENT=EVENT_QUEUE_LOSS A=1 B=32") != NULL);
  CHECK(Diagnostics_HandleCommand("DIAG STATUS"));
  Diagnostics_Service();
  CHECK(strstr(usb_lines[3], "HAS_FAULT=1") != NULL);
  test_tick = 2000U;
  Diagnostics_Service(); /* no new loss, no new event */
  CHECK(Diagnostics_HandleCommand("DIAG DUMP"));
  for (uint32_t i = 0; i < 10U; ++i) Diagnostics_Service();
  uint32_t losses = 0U;
  for (uint32_t i = 4U; i < usb_count; ++i)
    if (strstr(usb_lines[i], "EVENT=EVENT_QUEUE_LOSS") != NULL) ++losses;
  CHECK(losses == 1U);
}

int main(void)
{
  test_buffer();
  test_history();
  test_logger();
  test_cli();
  test_foreground_latency();
  test_queue_diag();
  puts("logger/diagnostics tests passed");
  return 0;
}
