#include "diagnostics.h"
#include "target_logger.h"
#include "usb_test.h"
#include <stdio.h>
#include <string.h>
#if defined(SPOOKY_IPC_SMOKE)
#include "ipc_smoke.h"
#endif

typedef enum {REPLY_IDLE, REPLY_STATUS, REPLY_LOG, REPLY_LAST,
  REPLY_DUMP_HEADER, REPLY_DUMP_ROWS, REPLY_DUMP_END, REPLY_HELP} ReplyState;
static DiagHistory history;
static ReplyState reply;
static uint32_t dump_sequence;
static uint32_t dump_remaining;
static uint32_t dump_count;
static uint32_t dump_gaps;
static uint32_t last_send_ms;
static uint32_t help_index;
static uint32_t last_service_ms;
static uint32_t last_log_sample_ms;
static uint32_t last_log_drops;
static uint32_t last_log_errors;
static bool service_started;
#if defined(SPOOKY_IPC_SMOKE)
static uint32_t last_ipc_link = UINT32_MAX;
#endif

static const char *const help_lines[] = {
  "OK RADIO BAND [FM|AM|SW|LW] | TUNE <kHz> | UP | DOWN | STATUS\r\n",
  "OK VOLUME READ | BATTERY READ | CHARGE STATUS | SLEEP START\r\n",
  "OK IPC STATUS | LOG STATUS | DIAG STATUS|LAST|DUMP|STOP\r\n",
  "OK MAG READ|STATUS|STREAM START [ms]|STOP\r\n",
  "OK EMF READ|STATUS|ZERO|STREAM START [ms]|STOP\r\n",
  "OK RECORD STATUS|START [seconds]|STOP\r\n",
  "OK WAV FETCH REC###.WAV|ABORT (binary protocol v1)\r\n",
  "OK SD STATUS|REINIT|STRESS [size-MiB] [passes]|STRESS STOP|CLEAN\r\n",
  "OK UI STATUS|WATCH START|WATCH STOP|LEDS|MATRIX PROBE|MATRIX ANIMATE|"
    "DISPLAY TEST [0|2]|DISPLAY OFF|OFF\r\n"
};

void Diagnostics_Init(void)
{
  DiagHistory_Init(&history); /* Before runtime IRQ producers are enabled. */
  reply = REPLY_IDLE;
  service_started = false;
  last_log_drops = 0U;
  last_log_errors = 0U;
#if defined(SPOOKY_IPC_SMOKE)
  last_ipc_link = UINT32_MAX;
#endif
  last_log_sample_ms = HAL_GetTick();
  Diagnostics_Record(DIAG_BOOT, 1U, 7U); /* Diagnostic schema v1, core M7. */
}

void Diagnostics_Record(DiagEventType type, uint32_t arg0, uint32_t arg1)
{
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  DiagHistory_Add(&history, HAL_GetTick(), type, arg0, arg1);
  __set_PRIMASK(mask);
}

static DiagSummary Summary(void)
{
  DiagSummary out;
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  out = history.summary;
  __set_PRIMASK(mask);
  return out;
}

bool Diagnostics_HandleCommand(const char *command)
{
  DiagSummary summary;
  bool help = strcmp(command, "HELP") == 0;
  bool diag = (strncmp(command, "DIAG", 4U) == 0) &&
    ((command[4] == '\0') || (command[4] == ' ') || (command[4] == '\t'));
  bool log = (strncmp(command, "LOG", 3U) == 0) &&
    ((command[3] == '\0') || (command[3] == ' ') || (command[3] == '\t'));
  if (!help && !diag && !log) return false;
  if (strcmp(command, "DIAG STOP") == 0)
  {
    reply = REPLY_IDLE;
    (void)UsbTest_SendText("OK DIAG STOP\r\n");
    return true;
  }
  if (reply != REPLY_IDLE)
  {
    (void)UsbTest_SendText("ERR DIAG reply active; use DIAG STOP\r\n");
    return true;
  }
  if (help) { help_index = 0U; reply = REPLY_HELP; }
  else if (strcmp(command, "LOG STATUS") == 0) reply = REPLY_LOG;
  else if (strcmp(command, "DIAG STATUS") == 0) reply = REPLY_STATUS;
  else if (strcmp(command, "DIAG LAST") == 0) reply = REPLY_LAST;
  else if (strcmp(command, "DIAG DUMP") == 0)
  {
    summary = Summary();
    dump_sequence = summary.oldest_sequence;
    dump_remaining = dump_count = summary.count;
    dump_gaps = 0U;
    reply = REPLY_DUMP_HEADER;
  }
  else
  {
    (void)UsbTest_SendText("ERR usage: LOG STATUS | DIAG STATUS|LAST|DUMP|STOP\r\n");
    return true;
  }
  last_send_ms = HAL_GetTick();
  return true;
}

static void FormatEvent(char *line, size_t size, const char *prefix, const DiagEvent *e)
{
  (void)snprintf(line, size, "%s SEQ=%lu MS=%lu EVENT=%s A=%lu B=%lu\r\n",
    prefix, (unsigned long)e->sequence, (unsigned long)e->tick_ms,
    DiagHistory_Name(e->type), (unsigned long)e->arg0, (unsigned long)e->arg1);
}

static void SendOneLine(uint32_t now)
{
  char line[256]; /* USB CDC's existing maximum; each response is one line. */
  bool gap = false;
  if (reply == REPLY_IDLE) return;
  if ((now - last_send_ms) >= 5000U) { reply = REPLY_IDLE; return; }
  if (reply == REPLY_STATUS)
  {
    DiagSummary s = Summary();
    (void)snprintf(line, sizeof(line),
      "OK DIAG V=1 CORE=7 COUNT=%lu OVERWRITTEN=%lu SD_MAX_MS=%lu "
      "LOOP_MAX_MS=%lu RADIO_OVR=%lu PDM_OVR=%lu SD_ERR=%lu AUDIO_ERR=%lu "
      "HAS_FAULT=%u\r\n", (unsigned long)s.count, (unsigned long)s.overwritten,
      (unsigned long)s.max_sd_write_ms, (unsigned long)s.max_loop_gap_ms,
      (unsigned long)s.totals[DIAG_RADIO_OVERRUN],
      (unsigned long)s.totals[DIAG_PDM_OVERRUN], (unsigned long)s.totals[DIAG_SD_ERROR],
      (unsigned long)s.totals[DIAG_AUDIO_ERROR], (unsigned int)s.have_fault);
  }
  else if (reply == REPLY_LOG)
  {
    TargetLoggerStats s;
    TargetLogger_GetStats(&s);
    (void)snprintf(line, sizeof(line),
      "OK LOG QUEUED=%lu PEAK=%lu DROP_WRITES=%lu DROP_BYTES=%lu "
      "TX_LOST=%lu TX_BYTES=%lu TX_ERRORS=%lu CONTEXT=%lu FLIGHT=%lu\r\n",
      (unsigned long)s.queued_bytes, (unsigned long)s.high_water,
      (unsigned long)s.dropped_writes, (unsigned long)s.dropped_bytes,
      (unsigned long)s.transport_dropped_bytes, (unsigned long)s.transmitted_bytes,
      (unsigned long)s.transport_errors, (unsigned long)s.rejected_context,
      (unsigned long)s.in_flight);
  }
  else if (reply == REPLY_LAST)
  {
    DiagSummary s = Summary();
    if (s.have_fault) FormatEvent(line, sizeof(line), "OK DIAG LAST", &s.last_fault);
    else (void)snprintf(line, sizeof(line), "OK DIAG LAST NONE\r\n");
  }
  else if (reply == REPLY_DUMP_HEADER)
    (void)snprintf(line, sizeof(line), "OK DIAG DUMP V=1 CORE=7 FIRST=%lu COUNT=%lu\r\n",
      (unsigned long)dump_sequence, (unsigned long)dump_count);
  else if (reply == REPLY_DUMP_ROWS)
  {
    DiagEvent event;
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    gap = !DiagHistory_Get(&history, dump_sequence, &event);
    __set_PRIMASK(mask);
    if (gap)
      (void)snprintf(line, sizeof(line), "DIAG GAP SEQ=%lu\r\n", (unsigned long)dump_sequence);
    else FormatEvent(line, sizeof(line), "DIAG EVENT", &event);
  }
  else if (reply == REPLY_HELP)
    (void)snprintf(line, sizeof(line), "%s", help_lines[help_index]);
  else
    (void)snprintf(line, sizeof(line), "OK DIAG END COUNT=%lu GAPS=%lu\r\n",
      (unsigned long)dump_count, (unsigned long)dump_gaps);

  if (!UsbTest_SendText(line)) return; /* Preserve cursor until accepted. */
  last_send_ms = now;
  if (reply == REPLY_DUMP_HEADER)
    reply = dump_remaining != 0U ? REPLY_DUMP_ROWS : REPLY_DUMP_END;
  else if (reply == REPLY_DUMP_ROWS)
  {
    if (gap) ++dump_gaps;
    ++dump_sequence;
    if (--dump_remaining == 0U) reply = REPLY_DUMP_END;
  }
  else if (reply == REPLY_HELP)
  {
    if (++help_index == sizeof(help_lines) / sizeof(help_lines[0])) reply = REPLY_IDLE;
  }
  else reply = REPLY_IDLE;
}

void Diagnostics_Service(void)
{
  uint32_t now = HAL_GetTick();
  if (service_started && ((now - last_service_ms) >= 50U))
    Diagnostics_Record(DIAG_LOOP_STALL, now - last_service_ms, 0U);
  last_service_ms = now;
  service_started = true;
#if defined(SPOOKY_IPC_SMOKE)
  const IpcSmokeDiagnostics *ipc = IpcSmoke_GetDiagnostics();
  uint32_t link = (uint32_t)IpcSmoke_Link(&ipc->health, now);
  if (link != last_ipc_link)
  {
    Diagnostics_Record(DIAG_IPC_LINK, link, (uint32_t)ipc->health.error);
    last_ipc_link = link;
  }
#endif
  if ((now - last_log_sample_ms) >= 1000U)
  {
    TargetLoggerStats s;
    TargetLogger_GetStats(&s);
    if (s.dropped_writes != last_log_drops)
      Diagnostics_Record(DIAG_LOG_LOSS, s.dropped_writes, s.dropped_bytes);
    if (s.transport_errors != last_log_errors)
      Diagnostics_Record(DIAG_LOG_ERROR, s.transport_errors, s.transport_dropped_bytes);
    last_log_drops = s.dropped_writes;
    last_log_errors = s.transport_errors;
    last_log_sample_ms = now;
  }
  SendOneLine(now);
}
