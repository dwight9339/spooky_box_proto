#ifndef SPOOKY_GRANULAR_H
#define SPOOKY_GRANULAR_H

/*
 * One bounded granular voice over an immutable mono clip (decision 0011 item 16,
 * full_spooky_proto-p04.7; the start of full_spooky_proto-v7l.1). It reads a
 * 24 kHz 16-bit clip through at most GRANULAR_MAX_GRAINS overlapping grains and
 * renders 48 kHz interleaved stereo halfwords, the same sample on both channels.
 *
 * - Grains start at a fixed interval (1 / density). Each reads the clip from
 *   position plus a random spray offset, circularly, at the pitch's rate, with
 *   linear interpolation, under a trapezoid envelope whose ramps grow with the
 *   envelope parameter (2 ms ramps at 0 %, a triangle at 100 %).
 * - There is no slice parameter: slicing belongs to the Slicer engine
 *   (decisions 0020 item 6 and 0021).
 * - The mix is scaled by level / sqrt(overlap), overlap being density times
 *   grain size, and saturates rather than wrapping.
 * - Bounded work: Render costs at most GRANULAR_MAX_GRAINS voice reads per
 *   output frame. A grain due while every voice is busy is dropped and counted.
 *
 * Render runs in the radio DMA interrupt; Start, Stop and SetParams run in the
 * foreground on the same core. SetParams stages the new values and Render takes
 * them at its next call, so a render never sees half of an update. Stop the
 * engine before changing the clip's samples.
 *
 * Portable C with no HAL calls.
 */

#include <stdbool.h>
#include <stdint.h>

#define GRANULAR_MAX_GRAINS 16U
#define GRANULAR_SOURCE_RATE_HZ 24000U
#define GRANULAR_OUTPUT_RATE_HZ 48000U

#define GRANULAR_SIZE_MS_MIN 10U
#define GRANULAR_SIZE_MS_MAX 500U
#define GRANULAR_DENSITY_MIN 1U    /* grains per second */
#define GRANULAR_DENSITY_MAX 100U
#define GRANULAR_PITCH_MIN (-24)   /* semitones */
#define GRANULAR_PITCH_MAX 24
#define GRANULAR_RAMP_MIN_FRAMES 96U /* 2 ms at 48 kHz */

typedef struct
{
  uint16_t position_permille; /* grain start within the clip, 0..1000 */
  uint16_t size_ms;           /* GRANULAR_SIZE_MS_MIN..MAX */
  uint16_t density;           /* grains per second, GRANULAR_DENSITY_MIN..MAX */
  int8_t pitch_semitones;     /* GRANULAR_PITCH_MIN..MAX */
  uint16_t spray_permille;    /* random start spread, a share of the clip, 0..1000 */
  uint8_t envelope_percent;   /* 0 (2 ms ramps) .. 100 (triangle) */
  uint8_t level_percent;      /* 0..100 */
} GranularParams;

/* Values already converted for the render. */
typedef struct
{
  uint32_t position;        /* clip sample */
  uint32_t spray;           /* clip samples */
  uint32_t length;          /* grain length, output frames */
  uint32_t ramp;            /* envelope ramp, output frames */
  uint32_t interval;        /* output frames between grain starts */
  uint32_t increment;       /* clip samples per output frame, Q16 */
  int32_t gain_q15;         /* level / sqrt(overlap) */
} GranularControl;

typedef struct
{
  bool active;
  uint32_t index;           /* clip sample read next */
  uint32_t fraction;        /* Q16 */
  uint32_t elapsed;         /* output frames since the grain started */
  uint32_t length;
  uint32_t ramp;
  uint32_t increment;
  uint32_t start;           /* clip sample the grain started at */
} GranularGrain;

typedef struct
{
  uint32_t grains_started;
  uint32_t grains_dropped;  /* due while every voice was busy */
  uint32_t active;          /* voices playing after the latest render */
  uint32_t active_high_water;
  uint32_t renders;
  uint32_t last_start;      /* clip sample of the newest grain */
} GranularStatus;

typedef struct
{
  const int16_t *volatile samples;
  volatile uint32_t count;
  volatile bool active;
  GranularParams params;    /* foreground: the latest parameters, kept for Start */
  /* Staged by SetParams, taken by Render while pending is set. */
  volatile uint32_t staged[sizeof(GranularControl) / sizeof(uint32_t)];
  volatile bool pending;
  GranularControl control;
  GranularGrain grains[GRANULAR_MAX_GRAINS];
  uint32_t until_next;      /* output frames to the next grain start */
  uint32_t random;          /* xorshift32 state, never 0 */
  int32_t mix[512];         /* one render chunk */
  volatile GranularStatus status;
} GranularEngine;

_Static_assert((sizeof(GranularControl) % sizeof(uint32_t)) == 0U,
               "the control block stages as whole words");

/* Default parameters: the middle of the clip, 80 ms grains, 20 per second, no
 * pitch change or spray, 50 % envelope, 80 % level. */
void Granular_DefaultParams(GranularParams *params);
/* Clamps each parameter to its range. Returns true if nothing had to change. */
bool Granular_ClampParams(GranularParams *params);

void Granular_Init(GranularEngine *engine, uint32_t seed);
/* Stages new parameters (clamped); Render takes them at its next call. */
void Granular_SetParams(GranularEngine *engine, const GranularParams *params);
/* Plays samples[0..count-1] with no grains sounding; the first grain starts at
 * once. False (and stopped) for no clip. */
bool Granular_Start(GranularEngine *engine, const int16_t *samples, uint32_t count);
void Granular_Stop(GranularEngine *engine);
bool Granular_Active(const GranularEngine *engine);
/* Writes frame_count stereo frames (2 * frame_count halfwords), in chunks of at
 * most 512. False, with nothing written, while stopped. */
bool Granular_Render(GranularEngine *engine, uint16_t *stereo, uint32_t frame_count);
void Granular_GetStatus(const GranularEngine *engine, GranularStatus *status);
/* The clip positions of the sounding grains, in permille of the clip, with
 * their envelope in 0..255, for display. Returns how many were written. Read
 * from the foreground while Render runs: each value is whole, the set may mix
 * two renders. */
uint32_t Granular_GetGrains(const GranularEngine *engine, uint16_t *position_permille,
                            uint8_t *envelope, uint32_t capacity);

#endif /* SPOOKY_GRANULAR_H */
