#ifndef SPOOKY_SLICER_H
#define SPOOKY_SLICER_H

/*
 * The Slicer engine's render core (decision 0022 items 3, 6, 13, 15 and 17;
 * full_spooky_proto-p04.15). It plays slices of an immutable 24 kHz 16-bit mono
 * clip, as a slice map divides it (slice_map.h), and renders 48 kHz interleaved
 * stereo halfwords, the same sample on both channels.
 *
 * - One voice. A trigger cuts the slice sounding with a SLICER_FADE_FRAMES
 *   crossfade: the old slice fades out while the new one fades in. A slice
 *   plays until its end or the next trigger, whichever comes first; there is no
 *   time-stretch, so a tempo change truncates slices or leaves gaps.
 * - Per slice: pitch changes the playback rate (+/-12 semitones, a slice played
 *   higher is also shorter); gate is a share of the slice's own pitched length
 *   and trims its tail; level scales it. A slice's end fades over
 *   SLICER_FADE_FRAMES so it does not click.
 * - A disabled slice is silent wherever it plays; it still cuts the slice
 *   before it, so the timing does not shift.
 * - Bounded work: at most two voice reads per output frame (the new slice and
 *   the one fading out). A third voice is never kept: a cut while a fade is
 *   still running ends that fade at once and counts it.
 *
 * Trigger and Silence run in the render's context (the radio interrupt),
 * between renders. Start, Stop and SetSetup run in the foreground on the same
 * core. SetSetup stages the slice map and the per-slice parameters, and the
 * render context takes them at its next call, so it never sees half of an
 * update. Stop the engine before changing the clip's samples.
 *
 * Portable C with no HAL calls.
 */

#include <stdbool.h>
#include <stdint.h>

#include "slice_map.h"

#define SLICER_SOURCE_RATE_HZ 24000U
#define SLICER_OUTPUT_RATE_HZ 48000U
#define SLICER_PITCH_MIN (-12)       /* semitones (0022 item 15) */
#define SLICER_PITCH_MAX 12
#define SLICER_GATE_MIN 5U           /* percent of the pitched slice */
#define SLICER_FADE_FRAMES 96U       /* 2 ms: the cut crossfade and the end fade */
#define SLICER_MIN_SLICE_SAMPLES 240U /* 10 ms at 24 kHz (0022 item 7) */
#define SLICER_NO_SLICE 0xFFU

typedef struct
{
  int8_t pitch_semitones; /* SLICER_PITCH_MIN..MAX */
  uint8_t gate_percent;   /* SLICER_GATE_MIN..100 */
  uint8_t level_percent;  /* 0..100 */
  uint8_t reserved;
} SlicerSlice;

/* What the foreground owns and stages: the map and each slice's parameters. */
typedef struct
{
  SliceMap map;
  SlicerSlice slices[SLICE_MAP_MAX_SLICES];
} SlicerSetup;

_Static_assert((sizeof(SlicerSetup) % sizeof(uint32_t)) == 0U,
               "the setup stages as whole words");

typedef struct
{
  bool active;
  uint8_t slice;
  uint32_t index;     /* clip sample read next */
  uint32_t fraction;  /* Q16 */
  uint32_t increment; /* clip samples per output frame, Q16 */
  uint32_t end;       /* the slice's end: reading stops there */
  uint32_t elapsed;   /* output frames since the slice's start */
  uint32_t length;    /* output frames it plays, the gate applied */
  uint32_t fade_out;  /* frames of the final fade */
  uint32_t faded_in;  /* output frames since the voice began */
  int32_t gain_q15;
} SlicerVoice;

typedef struct
{
  uint32_t triggers;
  uint32_t silent;          /* triggers of a disabled slice */
  uint32_t fades_cut;       /* fades ended at once by a quick second cut */
  uint32_t renders;
  uint8_t sounding;         /* the slice playing; SLICER_NO_SLICE for none */
  uint16_t position_permille; /* where in the clip it reads, while sounding */
} SlicerStatus;

typedef struct
{
  const int16_t *volatile samples;
  volatile uint32_t count;
  volatile bool active;
  SlicerSetup setup;        /* render context */
  volatile uint32_t staged[sizeof(SlicerSetup) / sizeof(uint32_t)];
  volatile bool pending;
  SlicerVoice voices[2];    /* the slice playing, and the one fading out */
  uint8_t current;          /* index of the slice playing in voices */
  int32_t mix[512];         /* one render chunk */
  volatile SlicerStatus status;
} SlicerEngine;

/* 0 semitones, full gate, 80 % level (the granular voice's default level). */
void Slicer_DefaultSlice(SlicerSlice *slice);
/* Clamps each parameter to its range. Returns true if nothing had to change. */
bool Slicer_ClampSlice(SlicerSlice *slice);
/* count equal slices over clip_samples (16 for a count out of range, as on a
 * first load: 0020 item 9) with every slice at its default. */
void Slicer_DefaultSetup(SlicerSetup *setup, uint32_t clip_samples, uint8_t count);

void Slicer_Init(SlicerEngine *engine);
/* Stages a setup (slices clamped); the render context takes it at its next call. */
void Slicer_SetSetup(SlicerEngine *engine, const SlicerSetup *setup);
/* Ready to play samples[0..count-1], silent until a trigger. A staged setup is
 * taken now; a map made for another clip length is made equal again, keeping
 * its count. False (and stopped) for no clip. */
bool Slicer_Start(SlicerEngine *engine, const int16_t *samples, uint32_t count);
void Slicer_Stop(SlicerEngine *engine);
bool Slicer_Active(const SlicerEngine *engine);
/* Render context only. Plays a slice from its start, as if it had started
 * offset_frames output frames ago (a slice joined mid-step), cutting the one
 * playing. False for a slice outside the map or a stopped engine. */
bool Slicer_Trigger(SlicerEngine *engine, uint8_t slice, uint32_t offset_frames);
/* Render context only. Fades the slice playing out, as a cut with nothing
 * after it. */
void Slicer_Silence(SlicerEngine *engine);
/* Writes frame_count stereo frames (2 * frame_count halfwords), silence when no
 * slice sounds, in chunks of at most 512. False, with nothing written, while
 * stopped. */
bool Slicer_Render(SlicerEngine *engine, uint16_t *stereo, uint32_t frame_count);
void Slicer_GetStatus(const SlicerEngine *engine, SlicerStatus *status);
/* Output frames a slice plays at its pitch and gate (0 outside the map). */
uint32_t Slicer_SliceFrames(const SlicerSetup *setup, uint8_t slice);

#endif /* SPOOKY_SLICER_H */
