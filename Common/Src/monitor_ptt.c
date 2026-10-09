#include "monitor_ptt.h"

#include <stddef.h>

void MonitorPtt_Init(MonitorPtt *ptt, uint32_t ramp_frames)
{
  if (ptt == NULL)
  {
    return;
  }
  ptt->target_on = 0U;
  ptt->gain_q15 = MONITOR_PTT_UNITY_Q15;
  /* Rounded up, so a full ramp never takes longer than ramp_frames. */
  ptt->step_q15 = (ramp_frames == 0U) ? MONITOR_PTT_UNITY_Q15 :
                  ((MONITOR_PTT_UNITY_Q15 + ramp_frames - 1U) / ramp_frames);
}

void MonitorPtt_Set(MonitorPtt *ptt, bool on)
{
  if (ptt != NULL)
  {
    ptt->target_on = on ? 1U : 0U;
  }
}

bool MonitorPtt_IsOn(const MonitorPtt *ptt)
{
  return (ptt != NULL) && (ptt->target_on != 0U);
}

uint32_t MonitorPtt_GainQ15(const MonitorPtt *ptt)
{
  return (ptt == NULL) ? MONITOR_PTT_UNITY_Q15 : ptt->gain_q15;
}

void MonitorPtt_Apply(MonitorPtt *ptt, int16_t *stereo, uint32_t frames)
{
  const bool on = MonitorPtt_IsOn(ptt);
  const uint32_t target = on ? 0U : MONITOR_PTT_UNITY_Q15;
  uint32_t gain;
  uint32_t frame;

  if ((ptt == NULL) || (stereo == NULL))
  {
    return;
  }
  gain = ptt->gain_q15;
  if ((gain == target) && !on)
  {
    return; /* unity: the monitor copy stands as it is */
  }
  for (frame = 0U; frame < frames; ++frame)
  {
    if (gain > target)
    {
      gain = (gain > (target + ptt->step_q15)) ? (gain - ptt->step_q15) : target;
    }
    else if (gain < target)
    {
      gain = ((gain + ptt->step_q15) < target) ? (gain + ptt->step_q15) : target;
    }
    /* Gain never exceeds unity, so the product stays in range. */
    stereo[2U * frame] = (int16_t)(((int32_t)stereo[2U * frame] * (int32_t)gain) >> 15);
    stereo[(2U * frame) + 1U] =
      (int16_t)(((int32_t)stereo[(2U * frame) + 1U] * (int32_t)gain) >> 15);
  }
  ptt->gain_q15 = gain;
}
