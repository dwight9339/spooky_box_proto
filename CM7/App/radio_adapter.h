#ifndef SPOOKY_RADIO_ADAPTER_H
#define SPOOKY_RADIO_ADAPTER_H

/*
 * Radio-machine adapter (RadioSm, full_spooky_proto-54w.28). Radio commands and radio
 * service events enter through the M7 event queue (decision 0007); the machine
 * alone drives the radio control service. The adapter polls the tune in flight
 * once per foreground pass and answers CLI commands when the machine publishes
 * their outcome. Foreground only.
 */

#include <stdbool.h>
#include <stdint.h>

#include "event_queue.h"

/* Post a CLI radio command. False if the queue rejected it (answer ERR BUSY). */
bool RadioAdapter_RequestTune(uint32_t frequency_khz);
bool RadioAdapter_RequestStep(bool up);
bool RadioAdapter_RequestBand(uint32_t band); /* radio_control_service RadioBand */
/* Post a scan engine's in-band tune (RAD_SOURCE_INTERNAL). Its answer goes to
 * ClassicAdapter_OnRadioAnswer, not to the CLI. False if the queue refused it. */
bool RadioAdapter_RequestInternalTune(uint32_t frequency_khz);
/* A CLI radio command was posted and the Radio machine has not answered it. */
bool RadioAdapter_CliCommandPending(void);

/* The boot sequence finished; called once after the dispatcher is initialized. */
void RadioAdapter_ReportStarted(bool ok);
/* Radio SAI or DMA error, or a codec output or volume failure. */
void RadioAdapter_ReportAudioFault(void);

void RadioAdapter_Init(void);
void RadioAdapter_Dispatch(const EvqEvent *event);
/* Poll the tune in flight; at most one bounded receiver transaction per call. */
void RadioAdapter_Service(void);
/* Feeds the radio onset detector (radio_activity_feed.c); once per pass,
 * before the Classic service takes its onsets. */
void RadioAdapter_ServiceActivity(void);

/* Reply `OK RADIO BAND=... FREQ=...` with the last completed tune result. */
void RadioAdapter_SendStatus(void);

#endif /* SPOOKY_RADIO_ADAPTER_H */
