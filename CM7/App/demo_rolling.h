#ifndef SPOOKY_DEMO_ROLLING_H
#define SPOOKY_DEMO_ROLLING_H

/*
 * Demo-only SD-backed rolling capture (decision 0011 item 14, following decision
 * 0010's shape; full_spooky_proto-p04.5; builds only with SPOOKY_DEMO).
 *
 * Outside a session the recorder streams its three-channel blocks here instead of
 * into a session file. Blocks are written once into fixed-length WAV segment
 * files (ROLL/SLOTnn.WAV, preallocated and reused in place); rolling_catalog.c
 * decides which slot each segment uses and keeps at least the newest 60.07 s.
 * A save (C-009) resolves at the next block boundary: it closes the segment,
 * pins the newest segments covering the window, continues the stream in a free
 * slot without a gap, renames the pinned segments into CAPS/CnnnSmm.WAV and
 * commits a small CAPS/Cnnn.TXT descriptor. Only the committed descriptor makes
 * it saved. Both folders are made when rolling starts, before capture.
 *
 * Every FatFs call runs in the recorder's foreground service, one bounded
 * operation per call to DemoRolling_Step, below block writes. No FatFs call is
 * made from an interrupt. Not part of the hpq.3 format: no recovery, no shared
 * blocks; a session turns rolling capture off (decision 0011 item 14).
 */

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  DEMO_ROLL_OFF = 0,     /* turned off with ROLL OFF */
  DEMO_ROLL_WAITING,     /* not running: no card, a session, or a retry pending */
  DEMO_ROLL_RUNNING,
  DEMO_ROLL_FAULT        /* stopped by a storage or capture fault; retries */
} DemoRollState;

/* Save outcomes (decision 0010 item 8, decision 0011 item 14). */
typedef enum
{
  DEMO_SAVE_NONE = 0,
  DEMO_SAVE_WRITING,
  DEMO_SAVE_SAVED,
  DEMO_SAVE_BUSY,
  DEMO_SAVE_UNAVAILABLE,
  DEMO_SAVE_FAILED
} DemoSaveOutcome;

typedef struct
{
  uint8_t state;            /* DemoRollState */
  uint8_t save;             /* DemoSaveOutcome of the latest request */
  bool save_active;         /* a save is between its request and its outcome */
  uint32_t retained_ms;     /* window held in the ring */
  uint32_t segments;        /* segments started */
  uint32_t rotations;
  uint32_t rotate_ms_max;   /* close plus open of a segment, across passes */
  uint32_t step_ms_max;     /* longest single FatFs step */
  uint32_t saves;           /* committed */
  uint32_t saves_failed;
  uint32_t saves_busy;
  uint32_t saves_unavailable;
  uint32_t save_ms_max;     /* request to descriptor commit */
  uint32_t faults;
  uint32_t reclaimed;
  uint32_t allocation_failures;
  uint32_t last_capture;    /* number of the newest committed CAPnnn, 0 for none */
  uint32_t last_capture_frames;
} DemoRollStatus;

/* --- Recorder-facing (radio_recorder.c), foreground only -------------------- */

/* Takes the storage lease, mounts and opens the first segment. False with the
 * reason logged; the recorder retries later. */
bool DemoRolling_Begin(void);
/* A segment is open and nothing must run before the next block is written. */
bool DemoRolling_ReadyForBlock(void);
/* Writes one recorder block; false on a write failure (call DemoRolling_Fault). */
bool DemoRolling_WriteBlock(const void *block, uint32_t bytes, uint32_t *write_ms);
/* One pending FatFs operation, if any: segment close or open, or a save step.
 * False if it failed (call DemoRolling_Fault). */
bool DemoRolling_Step(void);
/* Rolling stops cleanly (a session starts, ROLL OFF): the current segment is
 * closed and the lease released. Refused (false) while a save is in progress. */
bool DemoRolling_Stop(void);
/* Rolling stops after a fault: best-effort close, a save in progress fails. */
void DemoRolling_Fault(const char *reason);
bool DemoRolling_SaveActive(void);
/* Recorder state for the status: off, waiting or running. */
void DemoRolling_SetState(DemoRollState state);

/* --- Control-facing (demo_field.c, CLI) ------------------------------------- */

/* C-009. Answers at once with WRITING, BUSY or UNAVAILABLE; the final SAVED or
 * FAILED follows through DemoField_OnSaveOutcome. */
DemoSaveOutcome DemoRolling_RequestSave(void);
void DemoRolling_GetStatus(DemoRollStatus *status);
const char *DemoRolling_SaveName(uint8_t outcome);
const char *DemoRolling_StateName(uint8_t state);

#endif /* SPOOKY_DEMO_ROLLING_H */
