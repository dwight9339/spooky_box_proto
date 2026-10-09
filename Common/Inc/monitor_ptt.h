#ifndef SPOOKY_MONITOR_PTT_H
#define SPOOKY_MONITOR_PTT_H

#include <stdbool.h>
#include <stdint.h>

/* Monitor-only PTT (C-016, C-017; decision 0028; Beads full_spooky_proto-54w.9).
 *
 * Portable: no hardware access. While PTT is on, the radio's gain in the
 * monitored mix ramps linearly to silence, and back to unity when it is off,
 * one step per frame, so a press or release never clicks. Only the monitor
 * buffer passes through here; the raw capture is copied before it and is never
 * touched.
 *
 * Concurrency: MonitorPtt_Set runs in the foreground and only writes the
 * target; MonitorPtt_Apply runs in the radio interrupt and owns the gain. */

#define MONITOR_PTT_UNITY_Q15 32768U

typedef struct
{
  volatile uint8_t target_on;  /* written by the foreground */
  uint32_t gain_q15;           /* MONITOR_PTT_UNITY_Q15 is unity, 0 is silent */
  uint32_t step_q15;           /* gain change per frame */
} MonitorPtt;

/* ramp_frames is the length of a full ramp; 0 switches at once. */
void MonitorPtt_Init(MonitorPtt *ptt, uint32_t ramp_frames);
void MonitorPtt_Set(MonitorPtt *ptt, bool on);
bool MonitorPtt_IsOn(const MonitorPtt *ptt);
uint32_t MonitorPtt_GainQ15(const MonitorPtt *ptt);

/* Scales frames of interleaved stereo in place, ramping the gain toward the
 * target one step per frame. Unity with PTT off leaves the samples untouched. */
void MonitorPtt_Apply(MonitorPtt *ptt, int16_t *stereo, uint32_t frames);

#endif /* SPOOKY_MONITOR_PTT_H */
