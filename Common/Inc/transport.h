#ifndef SPOOKY_TRANSPORT_H
#define SPOOKY_TRANSPORT_H

/*
 * The Instrument transport: one clock for every engine's sequencer (decision 0021
 * items 5 and 7, decision 0026). It owns tempo and run state; the meter is fixed
 * at 4/4 (0026 item 1). Division and length belong to each pattern
 * (step_pattern.h).
 *
 * - Time advances in output frames at TRANSPORT_RATE_HZ. The position is kept in
 *   beats, Q16, since the transport last started. A tempo change rebases the
 *   position, so the beat count never jumps.
 * - A pattern with q steps per beat (4 for 1/16, 2 for 1/8, 1 for 1/4) is on step
 *   floor(beats * q) (0027 item 3). Transport_FramesToNextStep lets a renderer
 *   split its block at the exact frame a step starts.
 * - Transport_Fit chooses the tempo and division that make a pattern's steps span
 *   a clip, inside the fit range (0026 items 2 and 3).
 *
 * Not thread-safe: one context (the radio interrupt in the demo) owns a Transport.
 * Portable C with no HAL calls.
 */

#include <stdbool.h>
#include <stdint.h>

#define TRANSPORT_RATE_HZ 48000U
#define TRANSPORT_BPM_MIN_X100 4000U      /* editable range, 0026 item 8 */
#define TRANSPORT_BPM_MAX_X100 20000U
#define TRANSPORT_BPM_DEFAULT_X100 12000U
#define TRANSPORT_FIT_MIN_X100 7000U      /* fit range, 0026 item 3 */
#define TRANSPORT_FIT_MAX_X100 14000U

typedef struct
{
  uint32_t bpm_x100;      /* tempo, hundredths of a beat per minute */
  bool running;
  uint64_t base_q16;      /* beats at the latest start or tempo change, Q16 */
  uint64_t frames;        /* frames since then */
} Transport;

/* Stopped at TRANSPORT_BPM_DEFAULT_X100. */
void Transport_Init(Transport *transport);
/* Clamps to TRANSPORT_BPM_MIN_X100..MAX; the beat position carries on. */
void Transport_SetTempo(Transport *transport, uint32_t bpm_x100);
/* Running from beat 0. */
void Transport_Start(Transport *transport);
void Transport_Stop(Transport *transport);
/* Moves the position on by frames while running; nothing while stopped. */
void Transport_Advance(Transport *transport, uint32_t frames);
/* Beats since the start, Q16. */
uint64_t Transport_BeatsQ16(const Transport *transport);
/* Whole steps since the start, at steps_per_beat (1 to 8). */
uint32_t Transport_Step(const Transport *transport, uint8_t steps_per_beat);
/* Frames until the next step starts (at least 1); 0 while stopped. */
uint32_t Transport_FramesToNextStep(const Transport *transport, uint8_t steps_per_beat);

/* The tempo and steps per beat (4, 2 or 1) for which steps steps span a clip of
 * clip_samples at clip_rate_hz: the finest division whose tempo is within
 * TRANSPORT_FIT_MIN_X100..MAX, else 1/16 clamped to the editable range. False,
 * with nothing written, for an empty clip or no steps. */
bool Transport_Fit(uint32_t clip_samples, uint32_t clip_rate_hz, uint8_t steps,
                   uint32_t *bpm_x100, uint8_t *steps_per_beat);

#endif /* SPOOKY_TRANSPORT_H */
