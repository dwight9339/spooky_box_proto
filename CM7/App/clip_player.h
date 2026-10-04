#ifndef SPOOKY_CLIP_PLAYER_H
#define SPOOKY_CLIP_PLAYER_H

/*
 * Demo-only plain loop of the Instrument clip (decision 0011 item 15,
 * full_spooky_proto-p04.6: the checkpoint-2 fallback before the granular voice
 * of p04.7). It renders the 24 kHz mono clip into the monitor's 48 kHz
 * interleaved stereo halfwords, the same sample on both channels: even output
 * frames take a clip sample, odd ones the mean of it and the next, and the loop
 * interpolates from the last sample back to the first.
 *
 * Render runs in the radio DMA interrupt (RenderMonitor); Start and Stop run in
 * the foreground on the same core. Start fills every field before it sets
 * active, and Stop only clears it, so a render never sees a clip half set up.
 * Stop the player before changing the clip's samples.
 *
 * Portable C with no HAL calls.
 */

#include <stdbool.h>
#include <stdint.h>

/* Every field is volatile so the compiler keeps Start's stores in order: the
 * clip before active. */
typedef struct
{
  const int16_t *volatile samples;
  volatile uint32_t count;
  volatile uint32_t position; /* output frame within the loop, below 2 * count */
  volatile uint32_t loops;    /* completed passes, for the status */
  volatile bool active;
} ClipPlayer;

void ClipPlayer_Init(ClipPlayer *player);
/* Plays samples[0..count-1] from the start; false (and stopped) for no clip. */
bool ClipPlayer_Start(ClipPlayer *player, const int16_t *samples, uint32_t count);
void ClipPlayer_Stop(ClipPlayer *player);
bool ClipPlayer_Active(const ClipPlayer *player);
/* Writes frame_count stereo frames (2 * frame_count halfwords). False, with
 * nothing written, while stopped. */
bool ClipPlayer_Render(ClipPlayer *player, uint16_t *stereo, uint32_t frame_count);

#endif /* SPOOKY_CLIP_PLAYER_H */
