#ifndef SPOOKY_CLASSIC_SERVICE_H
#define SPOOKY_CLASSIC_SERVICE_H

/*
 * Classic on the M7 (spec 001, decision 0016 items 3, 8 and 16 to 21;
 * full_spooky_proto-54w.33, 54w.32).
 * Portable: it runs the classic_scan core against the shared radio and session
 * state that the caller reads each pass, and calls out through the integration
 * functions below. CM7/App/classic_adapter.c provides them in firmware; host
 * tests provide fakes.
 *
 * - Jumps are radio tune commands, issued only when the shared command policy
 *   allows in-band tuning in the current session state and the radio is running.
 *   Otherwise Classic publishes the run state "unable to scan" with the reason
 *   and issues nothing until tuning is possible again.
 * - Classic commands are the Context commands of C-103 to C-106, C-110 and C-111;
 *   the CLI issues the same ones (Principle III).
 * - The activity hold runs on the radio onsets the caller passes in the world
 *   (decision 0013), only while their measurement is valid.
 * - Every change to the run state (with its reason), direction, rate, distance,
 *   edge behavior or hold time is published as one event, with the state after
 *   the change.
 *   Routine jumps change only the frequency and are not events (FR-029, FR-030).
 *
 * Not reentrant: call from the foreground loop and the dispatcher only.
 */

#include <stdbool.h>
#include <stdint.h>

#include "classic_scan.h"
#include "sm/context_port.h"

/* Published run state (FR-028). Unable to scan replaces running and holding
 * while Classic cannot retune; a paused Classic stays paused. */
typedef enum
{
  CLASSIC_STATE_RUNNING = 0,
  CLASSIC_STATE_PAUSED,
  CLASSIC_STATE_SWEEP_COMPLETE,
  CLASSIC_STATE_UNABLE,
  CLASSIC_STATE_HOLDING           /* running, staying on activity (FR-020) */
} ClassicPublishedRun;

typedef enum
{
  CLASSIC_UNABLE_NONE = 0,
  CLASSIC_UNABLE_RADIO_STOPPED,   /* the radio has not started */
  CLASSIC_UNABLE_RADIO_FAULTED,
  CLASSIC_UNABLE_SESSION          /* the policy rejects tuning in this session state */
} ClassicUnableReason;

/* Shared state the caller reads before each call. */
typedef struct
{
  uint8_t band;              /* RadioBand of the last completed tune */
  uint32_t frequency_khz;    /* last completed tune */
  bool tune_valid;           /* a tune has completed in that band */
  uint8_t radio_state;       /* RadState */
  /* Another radio command (CLI, later Manual) is queued or waiting and has not
   * been answered; a jump now would be computed from a frequency about to change. */
  bool radio_command_pending;
  uint8_t session_state;     /* SesState */
  bool active;               /* Classic is the active Field engine and Manual is not tuning */
  /* The radio onset detector measured the latest radio block: the radio runs
   * and the block is outside a retune interval (decision 0015). */
  bool activity_valid;
  /* Largest onset (RadioOnset) since the previous service pass; 0 outside
   * ClassicService_Service, which is the only call that takes onsets. */
  uint8_t onset;
} ClassicWorld;

typedef struct
{
  uint8_t run_state;         /* ClassicPublishedRun */
  uint8_t unable_reason;     /* ClassicUnableReason; also set while paused */
  uint8_t band;
  uint8_t edge;              /* ClassicEdge */
  bool direction_up;
  bool rate_limited;
  uint16_t rate_setting_per_min;
  uint16_t rate_per_min;     /* in effect in this band */
  uint16_t distance_channels;
  uint16_t channel_index;
  uint16_t channel_count;
  uint32_t frequency_khz;
  uint32_t distance_khz;
  uint16_t hold_seconds;     /* 0: the hold is off */
} ClassicState;

/* Domain events (presentation.md PRES-CLS-*). */
typedef enum
{
  CLASSIC_PUB_RUN_STATE = 0,
  CLASSIC_PUB_DIRECTION,
  CLASSIC_PUB_RATE,
  CLASSIC_PUB_DISTANCE,
  CLASSIC_PUB_EDGE,
  CLASSIC_PUB_HOLD_TIME,
  CLASSIC_PUB_COUNT
} ClassicPublished;

/* CTX_CMD_RUN_PAUSE argument. Context sends the toggle (C-103); the CLI may ask
 * for a state, which changes nothing if Classic is already in it. */
#define CLASSIC_RUN_TOGGLE 0
#define CLASSIC_RUN_ENSURE_RUNNING 1
#define CLASSIC_RUN_ENSURE_PAUSED 2

typedef struct
{
  uint32_t jumps;            /* jumps the core made */
  uint32_t tunes_requested;  /* tune commands the queue accepted */
  uint32_t tunes_refused;    /* tune commands the queue refused; the landing waits a period */
  uint32_t tunes_failed;     /* answered failed, rejected, superseded or abandoned */
  uint32_t events;
  uint32_t holds;            /* holds started */
  bool tune_outstanding;     /* a tune command has not been answered yet */
} ClassicServiceStats;

/* Starts Classic in the decision 0016 startup state over the receiver's bands. */
bool ClassicService_Init(const ClassicTerritory territories[CLASSIC_SCAN_BAND_COUNT]);
/* Applies a Classic command. Returns false for a command Classic does not take. */
bool ClassicService_OnCommand(CtxCommand command, int32_t arg, uint32_t now_ms,
                              const ClassicWorld *world);
/* The radio answered Classic's tune command; tuned is false for every answer
 * other than a completed tune. */
void ClassicService_OnTuneAnswered(bool tuned);
void ClassicService_Service(uint32_t now_ms, const ClassicWorld *world);
bool ClassicService_GetState(const ClassicWorld *world, ClassicState *state);
void ClassicService_GetStats(ClassicServiceStats *stats);

const char *ClassicService_RunName(uint8_t run_state);
const char *ClassicService_ReasonName(uint8_t reason);
const char *ClassicService_EdgeName(uint8_t edge);
const char *ClassicService_EventName(ClassicPublished event);

/* --- Integration functions, provided by the classic adapter or a test --------- */

/* Post an in-band tune command to the Radio machine. False if the queue refused
 * it. The machine answers it exactly once (ClassicService_OnTuneAnswered). */
bool classic_integration_request_tune(uint32_t frequency_khz);
/* Publish an event with the state after the change. */
void classic_integration_publish(ClassicPublished event, uint32_t now_ms,
                                 const ClassicState *state);

#endif /* SPOOKY_CLASSIC_SERVICE_H */
