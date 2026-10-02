#ifndef SPOOKY_DIAG_HISTORY_H
#define SPOOKY_DIAG_HISTORY_H
#include <stdbool.h>
#include <stdint.h>

#define DIAG_HISTORY_CAPACITY 128U
typedef enum
{
  DIAG_BOOT = 1,
  DIAG_RECORD_START,
  DIAG_RECORD_END,
  DIAG_SD_WRITE,
  DIAG_SD_ERROR,
  DIAG_RADIO_OVERRUN,
  DIAG_PDM_OVERRUN,
  DIAG_AUDIO_ERROR,
  DIAG_IPC_LINK,
  DIAG_LOOP_STALL,
  DIAG_LOG_LOSS,
  DIAG_LOG_ERROR,
  DIAG_SLEEP,
  DIAG_EVENT_QUEUE_LOSS,
  DIAG_SESSION_MISMATCH,
  DIAG_COMMAND_REJECTED,
  DIAG_FOREGROUND_BUDGET,
  DIAG_USB_BACKPRESSURE,
  DIAG_STORAGE_MARGIN, /* Decision 0014 warning; not a fault. */
  DIAG_EVENT_LIMIT
} DiagEventType;

typedef struct
{
  uint32_t sequence;
  uint32_t tick_ms;
  uint32_t type;
  uint32_t arg0;
  uint32_t arg1;
} DiagEvent;

typedef struct
{
  uint32_t count;
  uint32_t oldest_sequence;
  uint32_t overwritten;
  uint32_t totals[DIAG_EVENT_LIMIT];
  uint32_t max_sd_write_ms;
  uint32_t max_loop_gap_ms;
  bool have_fault;
  DiagEvent last_fault;
} DiagSummary;

/* RAM-only history: overwrite oldest, preserve cumulative counters and the
 * latest fault separately. Caller serializes all accesses (including IRQs).
 * Sequence and tick are modulo 2^32; sequence zero is valid after wrap. */
typedef struct
{
  DiagEvent events[DIAG_HISTORY_CAPACITY];
  DiagSummary summary;
  uint32_t head;
  uint32_t next_sequence;
} DiagHistory;

void DiagHistory_Init(DiagHistory *history);
void DiagHistory_Add(DiagHistory *history, uint32_t tick_ms, DiagEventType type,
                     uint32_t arg0, uint32_t arg1);
bool DiagHistory_Get(const DiagHistory *history, uint32_t sequence, DiagEvent *out);
const char *DiagHistory_Name(uint32_t type);
#endif
