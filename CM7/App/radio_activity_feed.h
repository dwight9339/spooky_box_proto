#ifndef SPOOKY_RADIO_ACTIVITY_FEED_H
#define SPOOKY_RADIO_ACTIVITY_FEED_H

#include <stdbool.h>
#include <stdint.h>

#include "radio_activity.h"

/*
 * Feeds the radio onset detector (radio_activity.c, decision 0013) from the
 * live radio stream and marks retune intervals as "no radio measurement"
 * (decision 0015 items 4 to 6). Portable: no HAL calls.
 *
 * - The radio capture callback measures each delivered half-buffer (its mean
 *   absolute level) and posts it with its block index: the stream's completed
 *   half-buffer count before it, so block b holds frames b*512 to b*512+511 of
 *   the decision 0012 timeline. Constant work per block.
 * - The ring holds RADIO_ACTIVITY_FEED_CAPACITY blocks (341 ms). When it is
 *   full the newest block is dropped and counted; the detector then simply sees
 *   fewer blocks.
 * - A retune interval starts at the block in progress when a tune or band
 *   switch is about to be written to the receiver, and ends at the block in
 *   progress when the foreground observes the radio no longer tuning, plus the
 *   interval's settle margin in blocks. The caller reads the block in progress from the radio
 *   stream's sample timeline (decision 0012); when it cannot, the interval
 *   starts at block 0 or stays open. Blocks inside it, and every block while
 *   the radio is not running, reach the detector as not measuring, so the
 *   receiver's mute and its end cannot fire an onset.
 *
 * RadioActivityFeed_OnBlock is the only function called from interrupt context.
 */

#define RADIO_ACTIVITY_FEED_CAPACITY 32U
/* Decision 0015 item 5 starting value, one half-buffer: the settle margin of
 * any band or transition the item 10 qualification has not set. */
#define RADIO_ACTIVITY_FEED_SETTLE_BLOCKS 1U
/* The measurement is current while a measured block arrived this recently;
 * the foreground loop's recording maximum is 43 ms
 * (docs/evidence/2026-10-02-classic-on-m7.md). */
#define RADIO_ACTIVITY_FEED_STALE_MS 100U

typedef struct
{
  uint32_t blocks;          /* blocks given to the detector */
  uint32_t measured;        /* of them, measured */
  uint32_t dropped;         /* blocks the full ring refused */
  uint32_t high_water;      /* most blocks waiting at once */
  uint32_t retunes;         /* retune intervals opened */
  bool retuning;            /* a retune interval is open */
  bool valid;               /* RadioActivityFeed_Valid(now_ms) */
} RadioActivityFeedStatus;

/* Foreground, before the radio stream starts: empties the ring, clears the
 * retune state and starts the detector with its default configuration. */
bool RadioActivityFeed_Init(void);
/* Radio capture callback: one delivered half-buffer of interleaved samples. */
void RadioActivityFeed_OnBlock(uint32_t block, const int16_t *samples, uint32_t count);
/* Foreground, immediately before an in-band tune or a band switch is written to
 * the receiver. Starts a retune interval with the given settle margin; while
 * one is open it continues with the larger of the two margins. block_known is
 * false when the stream position cannot be read. */
void RadioActivityFeed_OnRetuneStart(bool block_known, uint32_t block_in_progress,
                                     uint32_t settle_blocks);
/* Foreground, after a band switch: the detector's averages reseed from the new
 * band's first measured block. */
void RadioActivityFeed_ResetAverages(void);
/* Foreground, once per pass: gives the waiting blocks to the detector, then
 * closes the retune interval if the radio is no longer tuning and the block in
 * progress is known. radio_running is true while the radio is Settled or
 * Tuning. */
void RadioActivityFeed_Service(uint32_t now_ms, bool radio_running, bool radio_tuning,
                               bool block_known, uint32_t block_in_progress);
/* Readers of the onsets; each sees every onset once. */
typedef enum
{
  RADIO_ACTIVITY_READER_CLASSIC = 0,  /* activity hold */
  RADIO_ACTIVITY_READER_MATRIX,       /* decision 0013 kicks */
  RADIO_ACTIVITY_READER_COUNT
} RadioActivityReader;

/* The largest onset (RadioOnset) since this reader's previous call, then
 * none. An unknown reader gets none. */
uint8_t RadioActivityFeed_TakeOnset(RadioActivityReader reader);
/* The latest block was measured and arrived within the stale bound. */
bool RadioActivityFeed_Valid(uint32_t now_ms);
void RadioActivityFeed_GetStatus(uint32_t now_ms, RadioActivityFeedStatus *status);

#endif /* SPOOKY_RADIO_ACTIVITY_FEED_H */
