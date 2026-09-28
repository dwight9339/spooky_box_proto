#include "diag_history.h"
#include <string.h>

void DiagHistory_Init(DiagHistory *history)
{
  memset(history, 0, sizeof(*history));
  history->next_sequence = 1U;
}

void DiagHistory_Add(DiagHistory *history, uint32_t tick_ms, DiagEventType type,
                     uint32_t arg0, uint32_t arg1)
{
  DiagEvent event;
  uint32_t index;
  bool fault;
  if ((type < DIAG_BOOT) || (type >= DIAG_EVENT_LIMIT)) return;
  event = (DiagEvent){history->next_sequence++, tick_ms, (uint32_t)type, arg0, arg1};
  if (history->summary.count == DIAG_HISTORY_CAPACITY)
  {
    history->head = (history->head + 1U) % DIAG_HISTORY_CAPACITY;
    ++history->summary.overwritten;
  }
  else ++history->summary.count;
  index = (history->head + history->summary.count - 1U) % DIAG_HISTORY_CAPACITY;
  history->events[index] = event;
  history->summary.oldest_sequence = history->events[history->head].sequence;
  ++history->summary.totals[type];
  if ((type == DIAG_SD_WRITE) && (arg0 > history->summary.max_sd_write_ms))
    history->summary.max_sd_write_ms = arg0;
  if ((type == DIAG_LOOP_STALL) && (arg0 > history->summary.max_loop_gap_ms))
    history->summary.max_loop_gap_ms = arg0;
  fault = (type == DIAG_SD_ERROR) || (type == DIAG_RADIO_OVERRUN) ||
          (type == DIAG_PDM_OVERRUN) || (type == DIAG_AUDIO_ERROR) ||
          (type == DIAG_LOG_ERROR) || (type == DIAG_EVENT_QUEUE_LOSS) ||
          ((type == DIAG_RECORD_END) && ((arg1 & 1U) != 0U)) ||
          ((type == DIAG_IPC_LINK) && (arg0 >= 2U));
  if (fault)
  {
    history->summary.have_fault = true;
    history->summary.last_fault = event;
  }
}

bool DiagHistory_Get(const DiagHistory *history, uint32_t sequence, DiagEvent *out)
{
  const uint32_t offset = sequence - history->summary.oldest_sequence;
  if (offset >= history->summary.count) return false;
  *out = history->events[(history->head + offset) % DIAG_HISTORY_CAPACITY];
  return true;
}

const char *DiagHistory_Name(uint32_t type)
{
  static const char *const names[] = {"UNKNOWN", "BOOT", "RECORD_START",
    "RECORD_END", "SD_WRITE", "SD_ERROR", "RADIO_OVERRUN", "PDM_OVERRUN",
    "AUDIO_ERROR", "IPC_LINK", "LOOP_STALL", "LOG_LOSS", "LOG_ERROR", "SLEEP",
    "EVENT_QUEUE_LOSS"};
  return type < DIAG_EVENT_LIMIT ? names[type] : names[0];
}
