#ifndef SPOOKY_RADIO_GAP_H
#define SPOOKY_RADIO_GAP_H

/* Decision 0004 gaps: the radio half-buffers replaced by digital silence while
 * the stream gate is closed, tracked by block index (block b holds timeline
 * frames b*512 to b*512+511, decision 0012). The capture callback records each
 * silenced block; the foreground closes the gap when the gate reopens and
 * reports each edge once. Portable, for host tests. */

#include <stdbool.h>
#include <stdint.h>

#define RADIO_GAP_FRAMES_PER_BLOCK 512U

typedef struct
{
  volatile bool open;
  volatile uint32_t first_block;
  volatile uint32_t last_block;
  volatile uint32_t opened;  /* gaps begun */
  volatile uint32_t closed;  /* gaps ended */
} RadioGap;

typedef struct
{
  bool open;
  uint32_t first_block;
  uint32_t last_block;
  uint32_t opened;
  uint32_t closed;
} RadioGapSnapshot;

typedef struct
{
  uint32_t starts_reported;
  uint32_t ends_reported;
  uint32_t missed;          /* gaps that began and ended between two polls of others */
} RadioGapReporter;

typedef struct
{
  bool start;
  bool end;
  uint64_t first_frame;     /* the gap's first silenced frame */
  uint64_t last_frame;      /* its last silenced frame */
} RadioGapEdges;

void RadioGap_Reset(RadioGap *gap);
/* Capture callback: block was replaced by silence. */
void RadioGap_OnSilencedBlock(RadioGap *gap, uint32_t block);
/* Foreground, with the capture interrupt masked: the gate reopened. */
void RadioGap_Close(RadioGap *gap);
/* Foreground, with the capture interrupt masked: a consistent copy. */
void RadioGap_Snapshot(const RadioGap *gap, RadioGapSnapshot *snapshot);

void RadioGap_ResetReporter(RadioGapReporter *reporter, const RadioGapSnapshot *now);
/* The edges of the latest gap not yet reported. A gap that both began and
 * ended unseen behind a newer one is counted in missed. */
RadioGapEdges RadioGap_NewEdges(RadioGapReporter *reporter, const RadioGapSnapshot *now);

#endif /* SPOOKY_RADIO_GAP_H */
