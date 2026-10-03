#include "radio_activity_feed.h"

#include <stddef.h>
#include <string.h>

_Static_assert((RADIO_ACTIVITY_FEED_CAPACITY & (RADIO_ACTIVITY_FEED_CAPACITY - 1U)) == 0U,
               "the ring index wraps by masking");

typedef struct
{
  uint32_t block;
  uint32_t mean_abs;
} FeedEntry;

/* Single producer (the radio capture callback), single consumer (the
 * foreground). Indices run freely; the producer writes the entry before it
 * publishes the head, and the consumer reads it before it publishes the tail. */
static volatile FeedEntry ring[RADIO_ACTIVITY_FEED_CAPACITY];
static volatile uint32_t ring_head;
static volatile uint32_t ring_tail;
static volatile uint32_t ring_dropped;

/* Foreground state. */
static bool feed_running;
static uint32_t feed_now_ms;
static bool retune_open;
static uint32_t retune_from;       /* first unmeasured block of the open interval */
static bool closed_known;
static uint32_t closed_through;    /* last unmeasured block of closed intervals */
static uint8_t onset_max[RADIO_ACTIVITY_READER_COUNT];
static bool have_last;
static bool last_measured;
static uint32_t last_block_ms;
static uint32_t blocks_fed;
static uint32_t blocks_measured;
static uint32_t high_water;
static uint32_t retunes;

bool RadioActivityFeed_Init(void)
{
  RadioActivityConfig config;
  uint32_t index;

  ring_head = 0U;
  ring_tail = 0U;
  ring_dropped = 0U;
  for (index = 0U; index < RADIO_ACTIVITY_FEED_CAPACITY; ++index)
  {
    ring[index].block = 0U;
    ring[index].mean_abs = 0U;
  }
  feed_running = false;
  feed_now_ms = 0U;
  retune_open = false;
  retune_from = 0U;
  closed_known = false;
  closed_through = 0U;
  (void)memset(onset_max, 0, sizeof(onset_max));
  have_last = false;
  last_measured = false;
  last_block_ms = 0U;
  blocks_fed = 0U;
  blocks_measured = 0U;
  high_water = 0U;
  retunes = 0U;
  RadioActivity_DefaultConfig(&config);
  return RadioActivity_Init(&config);
}

void RadioActivityFeed_OnBlock(uint32_t block, const int16_t *samples, uint32_t count)
{
  const uint32_t head = ring_head;
  volatile FeedEntry *entry;

  if ((head - ring_tail) >= RADIO_ACTIVITY_FEED_CAPACITY)
  {
    ++ring_dropped;
    return;
  }
  entry = &ring[head & (RADIO_ACTIVITY_FEED_CAPACITY - 1U)];
  entry->block = block;
  entry->mean_abs = RadioActivity_MeanAbs(samples, count);
  ring_head = head + 1U;
}

static bool Measured(uint32_t block)
{
  if (!feed_running)
  {
    return false;
  }
  if (closed_known && ((int32_t)(block - closed_through) <= 0))
  {
    return false;
  }
  return !retune_open || ((int32_t)(block - retune_from) < 0);
}

/* Gives every waiting block to the detector, classified by the retune state
 * as it is now. Called before an interval closes, so a block from before the
 * start stamp is not swallowed by the closed range. */
static void Drain(void)
{
  const uint32_t head = ring_head;
  uint32_t tail = ring_tail;
  uint32_t reader;

  if ((head - tail) > high_water)
  {
    high_water = head - tail;
  }
  while (tail != head)
  {
    const volatile FeedEntry *entry = &ring[tail & (RADIO_ACTIVITY_FEED_CAPACITY - 1U)];
    const bool measuring = Measured(entry->block);
    const RadioOnset onset = RadioActivity_OnBlock(entry->mean_abs, measuring);

    for (reader = 0U; reader < (uint32_t)RADIO_ACTIVITY_READER_COUNT; ++reader)
    {
      if ((uint8_t)onset > onset_max[reader])
      {
        onset_max[reader] = (uint8_t)onset;
      }
    }
    ++blocks_fed;
    if (measuring)
    {
      ++blocks_measured;
    }
    have_last = true;
    last_measured = measuring;
    last_block_ms = feed_now_ms;
    ++tail;
    ring_tail = tail;
  }
}

void RadioActivityFeed_OnRetuneStart(bool block_known, uint32_t block_in_progress)
{
  /* A tune issued before the previous interval closed adjoins it (decision
   * 0015 item 8): the open interval simply continues. */
  if (!retune_open)
  {
    retune_open = true;
    /* Unknown: every block until the interval closes. */
    retune_from = block_known ? block_in_progress : 0U;
  }
  ++retunes;
}

void RadioActivityFeed_ResetAverages(void)
{
  Drain(); /* the old band's blocks first, against the old averages */
  RadioActivity_Reset();
}

void RadioActivityFeed_Service(uint32_t now_ms, bool radio_running, bool radio_tuning,
                               bool block_known, uint32_t block_in_progress)
{
  feed_now_ms = now_ms;
  feed_running = radio_running;
  Drain();
  if (retune_open && !radio_tuning && block_known)
  {
    closed_through = block_in_progress + RADIO_ACTIVITY_FEED_SETTLE_BLOCKS;
    closed_known = true;
    retune_open = false;
  }
}

uint8_t RadioActivityFeed_TakeOnset(RadioActivityReader reader)
{
  uint8_t onset;

  if ((uint32_t)reader >= (uint32_t)RADIO_ACTIVITY_READER_COUNT)
  {
    return 0U;
  }
  onset = onset_max[reader];
  onset_max[reader] = 0U;
  return onset;
}

bool RadioActivityFeed_Valid(uint32_t now_ms)
{
  return have_last && last_measured &&
         ((now_ms - last_block_ms) <= RADIO_ACTIVITY_FEED_STALE_MS);
}

void RadioActivityFeed_GetStatus(uint32_t now_ms, RadioActivityFeedStatus *status)
{
  if (status == NULL)
  {
    return;
  }
  status->blocks = blocks_fed;
  status->measured = blocks_measured;
  status->dropped = ring_dropped;
  status->high_water = high_water;
  status->retunes = retunes;
  status->retuning = retune_open;
  status->valid = RadioActivityFeed_Valid(now_ms);
}
