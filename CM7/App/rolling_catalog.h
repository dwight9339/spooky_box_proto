#ifndef SPOOKY_ROLLING_CATALOG_H
#define SPOOKY_ROLLING_CATALOG_H

/*
 * Demo-only rolling-capture catalog (decision 0011 item 14, decision 0010's
 * shape; full_spooky_proto-p04.5). It decides which segment file each block of
 * the rolling stream goes into and which segments a save pins. It does no I/O:
 * demo_rolling.c owns the files.
 *
 * - One write stream: blocks are written once, in order, into fixed-length
 *   segments held in a ring of slots. A slot is reused only when its segment is
 *   no longer needed for the window, never while it is pinned by a save.
 * - The window is at least the newest ROLLING_WINDOW_BLOCKS blocks (decision 0010
 *   item 1: 704 blocks, 60.074667 s). With every slot in use the ring retains
 *   more than that, so reclaiming the oldest segment never cuts into the window.
 * - A save pins, oldest first, the newest segments that together hold at least
 *   the window (or everything retained, if less has been written). It never
 *   copies audio. One save at a time (decision 0010 item 8): a second request
 *   while one is in progress is busy.
 * - Demo narrowing: pinned segments leave the ring (they are renamed into the
 *   capture), so the rolling window after a save holds only newer blocks.
 *
 * Portable C with no HAL or FatFs calls.
 */

#include <stdbool.h>
#include <stdint.h>

#define ROLLING_SEGMENT_BLOCKS 64U   /* 262,144 frames, 5.461 s */
#define ROLLING_WINDOW_BLOCKS 704U   /* decision 0010 item 1 */
/* The window's eleven segments, the one being written, and one spare that a save
 * leaves free for the stream while it renames the pinned segments. */
#define ROLLING_SLOTS 13U

_Static_assert(((ROLLING_WINDOW_BLOCKS + ROLLING_SEGMENT_BLOCKS - 1U) /
                ROLLING_SEGMENT_BLOCKS) + 2U <= ROLLING_SLOTS,
               "the ring holds the window, the segment being written and a spare");

typedef enum
{
  ROLLING_SLOT_EMPTY = 0, /* holds nothing of the current window */
  ROLLING_SLOT_WRITING,
  ROLLING_SLOT_COMPLETE,  /* a closed segment in the window */
  ROLLING_SLOT_PINNED     /* pinned by the save in progress */
} RollingSlotState;

typedef struct
{
  uint8_t state;     /* RollingSlotState */
  uint16_t blocks;   /* blocks in the segment */
  uint32_t sequence; /* order in which segments were started */
} RollingSlot;

typedef enum
{
  ROLLING_SAVE_ACCEPTED = 0,
  ROLLING_SAVE_BUSY,        /* a save is already in progress */
  ROLLING_SAVE_UNAVAILABLE  /* nothing retained to save */
} RollingSaveAdmission;

typedef struct
{
  RollingSlot slots[ROLLING_SLOTS];
  int8_t writing;               /* slot being written, -1 for none */
  uint32_t next_sequence;
  bool save_active;
  uint8_t pinned_count;
  uint8_t pinned[ROLLING_SLOTS]; /* slot indices, oldest segment first */
  uint32_t pinned_blocks;
  uint32_t segments_started;
  uint32_t segments_reclaimed;   /* complete segments overwritten */
  uint32_t allocation_failures;  /* no slot free for the next segment */
} RollingCatalog;

/* Every slot empty; nothing written. */
void RollingCatalog_Init(RollingCatalog *catalog);
/* Rolling stopped (a session started, a fault): the window is discarded. A save
 * in progress keeps its pinned slots. */
void RollingCatalog_Discard(RollingCatalog *catalog);

/* The slot for the next segment: an empty slot, else the oldest complete one.
 * Returns -1 if every slot is pinned or being written (allocation failure). */
int RollingCatalog_BeginSegment(RollingCatalog *catalog);
/* One block written to the current segment. True when the segment is full. */
bool RollingCatalog_BlockWritten(RollingCatalog *catalog);
/* The current segment is closed: complete if it holds a block, else empty. */
void RollingCatalog_EndSegment(RollingCatalog *catalog);
/* Blocks of the window held in the ring: complete segments and the one being
 * written. Pinned segments are the save's, not the window's. */
uint32_t RollingCatalog_RetainedBlocks(const RollingCatalog *catalog);

/* Admission only; changes nothing. */
RollingSaveAdmission RollingCatalog_CheckSave(const RollingCatalog *catalog);
/* Pins the window. Call at a segment boundary, after EndSegment, so the newest
 * block is in a closed segment. Fills pinned[] oldest first. */
RollingSaveAdmission RollingCatalog_PinWindow(RollingCatalog *catalog);
/* A pinned slot's file has left the ring (renamed into the capture). */
void RollingCatalog_SlotReleased(RollingCatalog *catalog, uint8_t slot);
/* The save ended, committed or failed; any slot still pinned becomes empty. */
void RollingCatalog_SaveDone(RollingCatalog *catalog);

#endif /* SPOOKY_ROLLING_CATALOG_H */
