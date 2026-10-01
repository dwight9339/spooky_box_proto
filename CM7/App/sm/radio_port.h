#ifndef SPOOKY_RADIO_PORT_H
#define SPOOKY_RADIO_PORT_H

/*
 * Port of the Radio machine (docs/design/behavior/RadioSm.puml, decisions 0003,
 * 0006 and 0008). Three parts:
 *
 * 1. The service interface the M7 dispatcher calls, one function per event.
 * 2. The guards and actions the diagram calls. Nothing else may be called from the
 *    diagram.
 * 3. The integration functions the port calls. CM7/App/radio_adapter.c provides
 *    them with the radio control service; host tests provide fakes.
 *
 * Every command is answered exactly once through rad_integration_publish: tuned,
 * failed, band changed, rejected, superseded by a newer command, or abandoned by a
 * radio fault. One tune is in flight at a time and one command waits behind it.
 *
 * Portable C with no HAL calls. Not reentrant: call it from the dispatcher only.
 */

#include <stdbool.h>
#include <stdint.h>

typedef enum RadCommandKind {
    RAD_CMD_NONE = 0,
    RAD_CMD_TUNE, /* in-band tune to arg kHz */
    RAD_CMD_STEP, /* one band step; up and wrap select direction and edge */
    RAD_CMD_BAND  /* switch to band arg (radio_control_service RadioBand) */
} RadCommandKind;

/* Where a command came from, so its answer goes back there. */
typedef enum RadSource {
    RAD_SOURCE_INTERNAL = 0, /* scan engines and physical controls */
    RAD_SOURCE_CLI
} RadSource;

typedef struct RadCommand {
    uint8_t kind;   /* RadCommandKind */
    uint8_t source; /* RadSource */
    uint8_t up;     /* RAD_CMD_STEP: 1 up, 0 down */
    uint8_t wrap;   /* RAD_CMD_STEP: 1 wraps at the band edge, 0 stops there */
    uint32_t arg;   /* RAD_CMD_TUNE: kHz; RAD_CMD_BAND: band */
    uint32_t target_khz; /* the tune target once issued; set by the port */
} RadCommand;

typedef enum RadReject {
    RAD_REJECT_RANGE = 0,  /* the frequency is outside the current band */
    RAD_REJECT_UNAVAILABLE /* the radio is not running */
} RadReject;

/* Domain events (presentation.md). Command answers carry the command. */
typedef enum RadPublished {
    RAD_PUB_STARTED = 0,
    RAD_PUB_TUNE_STARTED,
    RAD_PUB_TUNED,
    RAD_PUB_TUNE_FAILED,
    RAD_PUB_BAND_CHANGED,
    RAD_PUB_REJECTED_RANGE,
    RAD_PUB_REJECTED_UNAVAILABLE,
    RAD_PUB_SUPERSEDED,
    RAD_PUB_ABANDONED,
    RAD_PUB_FAULT_START,
    RAD_PUB_FAULT_BAND,
    RAD_PUB_FAULT_AUDIO
} RadPublished;

typedef enum RadState {
    RAD_STATE_STOPPED = 0,
    RAD_STATE_SETTLED,
    RAD_STATE_TUNING,
    RAD_STATE_FAULTED
} RadState;

typedef struct RadStatus {
    uint8_t state;       /* RadState */
    uint8_t reserved[3];
    uint32_t commands;   /* commands received */
    uint32_t tuned;      /* tunes completed */
    uint32_t tune_failed;
    uint32_t superseded;
    uint32_t rejected;
} RadStatus;

/* --- 1. Service interface ------------------------------------------------------ */

void Radio_Init(void);
/* The boot sequence finished: the receiver, radio capture and monitor started. */
void Radio_OnStarted(bool ok);
void Radio_OnCommand(const RadCommand *command);
/* The radio service observed completion of the tune in flight. */
void Radio_OnTuneDone(void);
void Radio_OnTuneFailed(void);
/* Radio SAI or DMA error, or a codec output or volume failure. */
void Radio_OnAudioFault(void);

RadState Radio_GetState(void);
void Radio_GetStatus(RadStatus *status);

/* --- 2. Guards and actions called by the diagram ------------------------------- */

/* Guards read results stored before dispatch or by the action that produced them. */
bool rad_request_in_range(void);
bool rad_tune_issued(void);
bool rad_band_switched(void);
bool rad_pending_is_tune(void);
bool rad_pending_is_band(void);

void rad_begin_request(void);
void rad_begin_pending(void);
void rad_switch_band_request(void);
void rad_switch_band_pending(void);
void rad_hold_request(void);
void rad_abandon(void);
void rad_reject(RadReject reason);
void rad_publish(RadPublished event);

/* --- 3. Integration functions, provided by the radio adapter or a test -------- */

bool rad_integration_in_range(uint32_t frequency_khz);
uint32_t rad_integration_step_target(bool up, bool wrap);
/* Issue an in-band tune; completion is reported later through Radio_OnTuneDone or
 * Radio_OnTuneFailed. Returns false if the tune could not be issued. */
bool rad_integration_begin_tune(uint32_t frequency_khz);
/* Perform the whole band switch, including muting; returns its outcome. */
bool rad_integration_switch_band(uint32_t band);
/* Publish a domain event; command is NULL for events that answer no command. */
void rad_integration_publish(RadPublished event, const RadCommand *command);

#endif /* SPOOKY_RADIO_PORT_H */
