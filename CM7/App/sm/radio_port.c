#include "radio_port.h"

#include <stddef.h>

#include "RadioSm.h"

/* One machine per region, so one private instance. */
static RadioSm machine;

static const RadCommand *current; /* the command being dispatched, if any */
static RadCommand in_flight;      /* the tune or band switch being performed */
static RadCommand pending;        /* the newest command waiting behind it */
static bool issued;
static bool switched;
static RadStatus status;

static const RadCommand no_command = {RAD_CMD_NONE, RAD_SOURCE_INTERNAL, 0u, 0u, 0u, 0u};

static void publish(RadPublished event, const RadCommand *command)
{
    switch (event) {
    case RAD_PUB_TUNED:
        ++status.tuned;
        break;
    case RAD_PUB_TUNE_FAILED:
        ++status.tune_failed;
        break;
    case RAD_PUB_SUPERSEDED:
        ++status.superseded;
        break;
    case RAD_PUB_REJECTED_RANGE:
    case RAD_PUB_REJECTED_UNAVAILABLE:
        ++status.rejected;
        break;
    default:
        break;
    }
    rad_integration_publish(event, command);
}

static void begin(RadCommand command)
{
    in_flight = command;
    in_flight.target_khz = command.kind == RAD_CMD_STEP
        ? rad_integration_step_target(command.up != 0u, command.wrap != 0u)
        : command.arg;
    issued = rad_integration_begin_tune(in_flight.target_khz);
}

static void switch_band(RadCommand command)
{
    in_flight = command;
    switched = rad_integration_switch_band(command.arg);
}

static void dispatch_command(RadioSm_EventId event, const RadCommand *command)
{
    current = command;
    RadioSm_dispatch_event(&machine, event);
    current = NULL;
}

void Radio_Init(void)
{
    current = NULL;
    in_flight = no_command;
    pending = no_command;
    issued = false;
    switched = false;
    status = (RadStatus){0};
    RadioSm_ctor(&machine);
    RadioSm_start(&machine);
}

void Radio_OnStarted(bool ok)
{
    RadioSm_dispatch_event(&machine, ok ? RadioSm_EventId_START_OK : RadioSm_EventId_START_FAILED);
}

void Radio_OnCommand(const RadCommand *command)
{
    if (command == NULL) {
        return;
    }
    ++status.commands;
    switch (command->kind) {
    case RAD_CMD_TUNE:
        dispatch_command(RadioSm_EventId_TUNE, command);
        break;
    case RAD_CMD_STEP:
        dispatch_command(RadioSm_EventId_STEP, command);
        break;
    case RAD_CMD_BAND:
        dispatch_command(RadioSm_EventId_BAND, command);
        break;
    default:
        /* Not a command: answer it so its source never waits. */
        publish(RAD_PUB_REJECTED_UNAVAILABLE, command);
        break;
    }
}

void Radio_OnTuneDone(void)
{
    RadioSm_dispatch_event(&machine, RadioSm_EventId_TUNE_DONE);
}

void Radio_OnTuneFailed(void)
{
    RadioSm_dispatch_event(&machine, RadioSm_EventId_TUNE_FAILED);
}

void Radio_OnAudioFault(void)
{
    RadioSm_dispatch_event(&machine, RadioSm_EventId_AUDIO_FAULT);
}

RadState Radio_GetState(void)
{
    switch (machine.state_id) {
    case RadioSm_StateId_SETTLED:
        return RAD_STATE_SETTLED;
    case RadioSm_StateId_TUNING:
        return RAD_STATE_TUNING;
    case RadioSm_StateId_FAULTED:
        return RAD_STATE_FAULTED;
    default:
        return RAD_STATE_STOPPED;
    }
}

void Radio_GetStatus(RadStatus *out)
{
    if (out == NULL) {
        return;
    }
    *out = status;
    out->state = (uint8_t)Radio_GetState();
}

/* --- Guards called by the diagram ---------------------------------------------- */

bool rad_request_in_range(void)
{
    return current != NULL && rad_integration_in_range(current->arg);
}

bool rad_tune_issued(void)
{
    return issued;
}

bool rad_band_switched(void)
{
    return switched;
}

bool rad_pending_is_tune(void)
{
    return pending.kind == RAD_CMD_TUNE || pending.kind == RAD_CMD_STEP;
}

bool rad_pending_is_band(void)
{
    return pending.kind == RAD_CMD_BAND;
}

/* --- Actions called by the diagram --------------------------------------------- */

void rad_begin_request(void)
{
    if (current != NULL) {
        begin(*current);
    }
}

void rad_begin_pending(void)
{
    const RadCommand next = pending;
    pending = no_command;
    begin(next);
}

void rad_switch_band_request(void)
{
    if (current != NULL) {
        switch_band(*current);
    }
}

void rad_switch_band_pending(void)
{
    const RadCommand next = pending;
    pending = no_command;
    switch_band(next);
}

void rad_hold_request(void)
{
    if (current == NULL) {
        return;
    }
    if (pending.kind != RAD_CMD_NONE) {
        publish(RAD_PUB_SUPERSEDED, &pending);
    }
    pending = *current;
}

void rad_abandon(void)
{
    if (in_flight.kind != RAD_CMD_NONE) {
        publish(RAD_PUB_ABANDONED, &in_flight);
        in_flight = no_command;
    }
    if (pending.kind != RAD_CMD_NONE) {
        publish(RAD_PUB_ABANDONED, &pending);
        pending = no_command;
    }
}

void rad_reject(RadReject reason)
{
    publish(reason == RAD_REJECT_RANGE ? RAD_PUB_REJECTED_RANGE : RAD_PUB_REJECTED_UNAVAILABLE,
            current);
}

void rad_publish(RadPublished event)
{
    switch (event) {
    case RAD_PUB_TUNE_STARTED:
        publish(event, &in_flight);
        break;
    case RAD_PUB_TUNED:
    case RAD_PUB_TUNE_FAILED:
    case RAD_PUB_BAND_CHANGED:
    case RAD_PUB_FAULT_BAND:
        /* These answer the command in flight, which is then finished. */
        publish(event, &in_flight);
        in_flight = no_command;
        break;
    default:
        publish(event, NULL);
        break;
    }
}
