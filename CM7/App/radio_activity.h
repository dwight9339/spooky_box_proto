#ifndef SPOOKY_RADIO_ACTIVITY_H
#define SPOOKY_RADIO_ACTIVITY_H

#include <stdbool.h>
#include <stdint.h>

/* Radio onset detector (decision 0013 items 6, 7 and 10). Measured from the
 * radio audio only, never RSSI or SNR. Fed once per radio half-buffer
 * (512 frames, 10.67 ms at 48 kHz) with the block's mean absolute level; it
 * keeps a fast and a slow average and reports an onset when the fast one
 * rises above the slow one. Bounded: constant work per block, no buffers. */

#define RADIO_ACTIVITY_ONSET_LEVELS 3U

typedef enum
{
  RADIO_ONSET_NONE = 0,
  RADIO_ONSET_SMALL = 1,
  RADIO_ONSET_MEDIUM = 2,
  RADIO_ONSET_LARGE = 3
} RadioOnset;

typedef struct
{
  /* Per-block smoothing weights in Q16: 1 - exp(-10.67 ms / tau).
   * tau 30 ms -> 0.2992 (19608); tau 1 s -> 0.01061 (695). */
  uint32_t fast_alpha_q16;
  uint32_t slow_alpha_q16;
  /* Fast/slow ratios in Q8 for small, medium, large: 1.5x, 2.5x, 4x. */
  uint32_t onset_ratio_q8[RADIO_ACTIVITY_ONSET_LEVELS];
  /* An onset re-arms once the ratio falls below this (1.25x). */
  uint32_t rearm_ratio_q8;
  /* The slow average is floored at this mean absolute level, in sample
   * units, so near-silence cannot make tiny changes look like large ratios.
   * Must be at least 1. Starting value, tuned on the bench. */
  uint32_t level_floor;
  /* Once a threshold is crossed the detector waits for the ratio to stop
   * rising, so the onset reports the highest level crossed, but never longer
   * than this many blocks after the first crossing. */
  uint8_t max_hold_blocks;
} RadioActivityConfig;

typedef struct
{
  bool seeded;
  bool armed;
  uint8_t pending;              /* level crossed, not yet reported */
  uint32_t fast_q8;             /* averages in Q8 sample units */
  uint32_t slow_q8;
  uint32_t ratio_q8;
  uint32_t onsets[RADIO_ACTIVITY_ONSET_LEVELS];
  uint32_t measured_blocks;
  uint32_t unmeasured_blocks;
} RadioActivityStatus;

void RadioActivity_DefaultConfig(RadioActivityConfig *config);
/* Rejects alphas outside 1..65536, thresholds that do not increase, a
 * re-arm ratio at or above the small threshold, a zero floor or hold. */
bool RadioActivity_Init(const RadioActivityConfig *config);
/* Forgets the averages; the next measured block seeds them. Call when the
 * radio starts or restarts after being off or faulted. */
void RadioActivity_Reset(void);
/* Mean absolute value of count interleaved int16 samples. */
uint32_t RadioActivity_MeanAbs(const int16_t *samples, uint32_t count);
/* measuring is false while the radio is not running, is Faulted, or is
 * tuning (the receiver mutes): the averages hold, any onset being measured
 * is dropped and none fires. */
RadioOnset RadioActivity_OnBlock(uint32_t mean_abs, bool measuring);
bool RadioActivity_GetStatus(RadioActivityStatus *status);

#endif /* SPOOKY_RADIO_ACTIVITY_H */
