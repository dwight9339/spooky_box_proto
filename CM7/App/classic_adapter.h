#ifndef SPOOKY_CLASSIC_ADAPTER_H
#define SPOOKY_CLASSIC_ADAPTER_H

/*
 * Firmware wiring of the Classic service (full_spooky_proto-54w.33, 54w.32). It
 * reads the shared radio and session state and the radio onsets of
 * radio_activity_feed.c, issues Classic's tunes to the Radio machine
 * through the M7 event queue, takes Classic commands from the queue, and
 * publishes Classic's events on the log stamped with the integration clock and
 * the radio sample timeline (decision 0012). Foreground only.
 */

#include <stdbool.h>
#include <stdint.h>

#include "event_queue.h"
#if defined(SPOOKY_DEMO)
#include "classic_service.h"
#endif
#include "sm/context_port.h"
#include "sm/radio_port.h"

/* After RadioAdapter_Init: Classic starts running on FM (decision 0016 item 22). */
void ClassicAdapter_Init(void);
/* One pass: takes the radio onsets since the last pass and may issue Classic's
 * next jump. Call once per foreground pass, after RadioAdapter_ServiceActivity. */
void ClassicAdapter_Service(void);

/* Post a CLI Classic command; the reply follows when it is dispatched. False if
 * the queue refused it (answer ERR BUSY). */
bool ClassicAdapter_RequestCommand(CtxCommand command, int32_t arg);
void ClassicAdapter_Dispatch(const EvqEvent *event);
/* The Radio machine answered an internal command (radio_adapter.c). */
void ClassicAdapter_OnRadioAnswer(RadPublished event, const RadCommand *command);

#if defined(SPOOKY_DEMO)
/* Demo-only (p04.3): a Classic command from the physical controls, through the
 * Context machine. No CLI reply follows. False if the queue refused it. */
bool ClassicAdapter_RequestInternalCommand(CtxCommand command, int32_t arg);
/* Classic's published state, for the demo view. */
bool ClassicAdapter_GetState(ClassicState *state);
#endif

/* The CLASSIC commands of the USB CLI (usb-cli.md), already upper-case and past
 * the command policy. Returns false if the command is not a CLASSIC command. */
bool ClassicAdapter_HandleCommand(const char *command);
/* Reply `OK CLASSIC STATE=...` with Classic's published state. */
void ClassicAdapter_SendStatus(void);

#endif /* SPOOKY_CLASSIC_ADAPTER_H */
