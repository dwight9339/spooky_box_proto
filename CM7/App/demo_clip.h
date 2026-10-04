#ifndef SPOOKY_DEMO_CLIP_H
#define SPOOKY_DEMO_CLIP_H

/*
 * Demo-only Instrument clip (decision 0011 item 15, full_spooky_proto-p04.6;
 * builds only with SPOOKY_DEMO). It owns the clip buffer in free AXI SRAM, loads
 * the clip from a committed capture and plays it as a plain loop on the monitor.
 *
 * - The C-010 chord asks for a save; when it is accepted the old clip goes
 *   (DemoClip_Expect) and the clip waits for that save. demo_rolling.c starts
 *   the load once the capture's descriptor is committed, and runs its steps:
 *   one bounded FatFs operation each (open a segment, or read and decimate
 *   CLIP_LOAD_CHUNK_FRAMES frames), in the recorder's foreground service under
 *   its storage lease, below block writes. clip_decimator.c picks the frames and
 *   decimates them.
 * - A refused or failed save, or a failed load, ends in FAILED with its reason;
 *   only a fully loaded clip is READY, and only a READY clip plays (Principle II).
 *   Every outcome goes to DemoField_OnClipOutcome.
 * - A READY clip plays on the monitor while Instrument shows it, through the
 *   granular voice (Common/Src/granular.c, p04.7) or the plain loop
 *   (clip_player.c). Both render in the monitor stage of the radio DMA
 *   interrupt, timed against CLIP_RENDER_BUDGET_US; the raw capture is copied
 *   before it (Principle I).
 */

#include <stdbool.h>
#include <stdint.h>

#include "granular.h"

/* Source frames read and decimated per load step: 12,288 bytes. */
#define CLIP_LOAD_CHUNK_FRAMES 2048U

typedef enum
{
  DEMO_CLIP_NONE = 0,  /* nothing loaded since boot */
  DEMO_CLIP_SAVING,    /* waiting for the chord's save to commit */
  DEMO_CLIP_LOADING,
  DEMO_CLIP_READY,
  DEMO_CLIP_FAILED
} DemoClipState;

typedef enum
{
  DEMO_CLIP_FAULT_NONE = 0,
  DEMO_CLIP_FAULT_SAVE_BUSY,        /* a save or load was already in progress */
  DEMO_CLIP_FAULT_SAVE_UNAVAILABLE, /* no rolling window to save */
  DEMO_CLIP_FAULT_SAVE_FAILED,
  DEMO_CLIP_FAULT_LOAD_FAILED,      /* open, size check, read or length */
  DEMO_CLIP_FAULT_COUNT
} DemoClipFault;

typedef struct
{
  uint8_t state;          /* DemoClipState */
  uint8_t fault;          /* DemoClipFault of the latest failure */
  bool playing;
  uint32_t capture;       /* Cnnn the clip (or the load) comes from; 0 for none */
  uint32_t samples;       /* clip samples at 24 kHz, while READY */
  uint32_t loads;         /* clips loaded */
  uint32_t failures;
  uint32_t load_ms;       /* the latest load, from its start to READY */
  uint32_t load_ms_max;
  uint32_t step_ms_max;   /* longest single load step */
  uint32_t loops;         /* passes of the current clip */
} DemoClipStatus;

/* What plays a READY clip on the monitor: the granular voice (p04.7), or the
 * plain loop of p04.6 kept as the fallback. */
typedef enum
{
  DEMO_VOICE_GRAIN = 0,
  DEMO_VOICE_LOOP
} DemoVoice;

typedef struct
{
  uint8_t voice;              /* DemoVoice */
  uint32_t renders;           /* monitor halves rendered by a voice */
  uint32_t render_us_max;     /* longest voice render, in the radio interrupt */
  uint32_t render_over_budget;/* renders longer than render_budget_us */
  uint32_t render_budget_us;
  uint32_t grains_started;
  uint32_t grains_dropped;    /* due while all GRANULAR_MAX_GRAINS voices played */
  uint32_t grains_active;
  uint32_t grains_high_water;
} DemoVoiceStatus;

/* --- Control-facing (demo_field.c), foreground ------------------------------- */

/* Before anything else here: seeds the granular voice. */
void DemoClip_Init(void);

/* The chord's save was accepted: the old clip stops and is gone. */
void DemoClip_Expect(void);
/* The chord's save was refused or failed: FAILED with the reason. */
void DemoClip_Fail(DemoClipFault fault);
/* Whether Instrument shows the clip; it plays only while READY. */
void DemoClip_SetPlaying(bool play);
void DemoClip_GetStatus(DemoClipStatus *status);
/* Switching voices stops the current one; SetPlaying starts the new one. */
void DemoClip_SetVoice(DemoVoice voice);
DemoVoice DemoClip_Voice(void);
/* The granular voice's parameters, taken at its next render. */
void DemoClip_SetGrainParams(const GranularParams *params);
/* Sounding grains, for the matrix (Granular_GetGrains). */
uint32_t DemoClip_GetGrains(uint16_t *position_permille, uint8_t *envelope, uint32_t capacity);
void DemoClip_GetVoiceStatus(DemoVoiceStatus *status);
/* Clears the render maximum and the over-budget count. */
void DemoClip_ResetVoiceStats(void);
const char *DemoClip_StateName(uint8_t state);
const char *DemoClip_FaultName(uint8_t fault);

/* --- Rolling-facing (demo_rolling.c), recorder foreground -------------------- */

/* A chord's save is in progress and the clip waits for it. */
bool DemoClip_Waiting(void);
/* The waited-for save committed CAPS/<name>; its segments hold
 * segment_frames[0..count-1] frames, oldest first. */
void DemoClip_Begin(uint32_t capture, const char *name, const uint32_t *segment_frames,
                    uint32_t count);
bool DemoClip_Loading(void);
/* One load step. A failure ends the load (FAILED) and does not stop rolling. */
void DemoClip_Step(void);
/* Rolling stopped on a fault: a load in progress fails. */
void DemoClip_Abort(void);

/* --- Monitor (audio_path_service.c), radio DMA interrupt --------------------- */

/* Writes the loop over frame_count stereo frames; false while not playing. */
bool DemoClip_RenderMonitor(uint16_t *stereo, uint32_t frame_count);

#endif /* SPOOKY_DEMO_CLIP_H */
