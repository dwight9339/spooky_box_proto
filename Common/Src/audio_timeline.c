#include "audio_timeline.h"

#include <stddef.h>

uint64_t AudioTimeline_Extend32(uint64_t reference, uint32_t raw)
{
  int32_t delta = (int32_t)(raw - (uint32_t)reference);

  if (delta >= 0)
  {
    return reference + (uint64_t)delta;
  }
  if ((uint64_t)(-(int64_t)delta) > reference)
  {
    return 0U;
  }
  return reference - (uint64_t)(-(int64_t)delta);
}

AudioTimelineResult AudioTimeline_StreamStart(AudioTimelineStream *stream,
                                              uint32_t frames_per_half,
                                              uint32_t items_per_frame,
                                              uint32_t raw_completed_halves)
{
  uint32_t epoch;

  if ((stream == NULL) || (frames_per_half == 0U) || (items_per_frame == 0U) ||
      ((uint64_t)frames_per_half * items_per_frame * 2U > UINT32_MAX))
  {
    return AUDIO_TIMELINE_ERR_ARGUMENT;
  }
  epoch = stream->epoch + 1U;
  if (epoch == 0U)
  {
    epoch = 1U;
  }
  stream->epoch = epoch;
  stream->frames_per_half = frames_per_half;
  stream->items_per_frame = items_per_frame;
  stream->raw_base = raw_completed_halves;
  stream->completed_halves = 0U;
  return AUDIO_TIMELINE_OK;
}

AudioTimelineResult AudioTimeline_StreamObserve(AudioTimelineStream *stream,
                                                uint32_t raw_completed_halves,
                                                uint32_t remaining_items,
                                                AudioTimelinePosition *position)
{
  uint32_t half_items;
  uint32_t buffer_items;
  uint32_t index;
  uint64_t completed;
  uint64_t counted;

  if ((stream == NULL) || (position == NULL))
  {
    return AUDIO_TIMELINE_ERR_ARGUMENT;
  }
  if (stream->epoch == 0U)
  {
    return AUDIO_TIMELINE_ERR_NOT_STARTED;
  }
  half_items = stream->frames_per_half * stream->items_per_frame;
  buffer_items = 2U * half_items;
  if (remaining_items > buffer_items)
  {
    return AUDIO_TIMELINE_ERR_ARGUMENT;
  }

  completed = AudioTimeline_Extend32(stream->completed_halves,
                                     raw_completed_halves - stream->raw_base);
  if (completed < stream->completed_halves)
  {
    return AUDIO_TIMELINE_ERR_BACKWARDS;
  }
  stream->completed_halves = completed;

  /* The DMA counts down and reloads, so zero means the end of the buffer was
   * just reached: the transfer is back at index 0. */
  index = (buffer_items - remaining_items) % buffer_items;
  counted = completed;
  /* After an even number of completions the DMA fills the first half. If it is
   * already in the other half, one completion is pending and not yet counted. */
  if ((index / half_items) != (uint32_t)(completed % 2U))
  {
    ++counted;
  }
  position->epoch = stream->epoch;
  position->frame = (counted * stream->frames_per_half) +
                    ((index % half_items) / stream->items_per_frame);
  return AUDIO_TIMELINE_OK;
}

AudioTimelineResult AudioTimeline_AlignStart(const AudioTimelineStream *stream,
                                             AudioTimelinePosition mic_start,
                                             uint32_t mic_latency_frames,
                                             AudioTimelineAlignment *alignment)
{
  uint64_t first_half_frame;
  uint64_t trim;

  if ((stream == NULL) || (alignment == NULL))
  {
    return AUDIO_TIMELINE_ERR_ARGUMENT;
  }
  if (stream->epoch == 0U)
  {
    return AUDIO_TIMELINE_ERR_NOT_STARTED;
  }
  if (mic_start.epoch != stream->epoch)
  {
    return AUDIO_TIMELINE_ERR_STALE_EPOCH;
  }
  first_half_frame =
    (mic_start.frame / stream->frames_per_half) * stream->frames_per_half;
  trim = (mic_start.frame - first_half_frame) + mic_latency_frames;
  if (trim > UINT32_MAX)
  {
    return AUDIO_TIMELINE_ERR_ARGUMENT;
  }
  alignment->origin.epoch = mic_start.epoch;
  alignment->origin.frame = mic_start.frame + mic_latency_frames;
  alignment->first_half_frame = first_half_frame;
  alignment->trim_frames = (uint32_t)trim;
  return AUDIO_TIMELINE_OK;
}

AudioTimelineResult AudioTimeline_Offset(AudioTimelinePosition origin,
                                         AudioTimelinePosition position,
                                         uint64_t *offset)
{
  if (offset == NULL)
  {
    return AUDIO_TIMELINE_ERR_ARGUMENT;
  }
  if ((origin.epoch == 0U) || (position.epoch == 0U))
  {
    return AUDIO_TIMELINE_ERR_NOT_STARTED;
  }
  if (position.epoch != origin.epoch)
  {
    return AUDIO_TIMELINE_ERR_STALE_EPOCH;
  }
  if (position.frame < origin.frame)
  {
    return AUDIO_TIMELINE_ERR_BEFORE_ORIGIN;
  }
  *offset = position.frame - origin.frame;
  return AUDIO_TIMELINE_OK;
}

AudioTimelineResult AudioTimeline_StampOffset(AudioTimelinePosition origin,
                                              AudioTimelineStamp stamp,
                                              uint64_t *latest_offset,
                                              uint32_t *uncertainty_frames)
{
  AudioTimelineResult result;
  uint64_t offset;

  if ((latest_offset == NULL) || (uncertainty_frames == NULL))
  {
    return AUDIO_TIMELINE_ERR_ARGUMENT;
  }
  result = AudioTimeline_Offset(origin, stamp.position, &offset);
  if (result != AUDIO_TIMELINE_OK)
  {
    return result;
  }
  *latest_offset = offset;
  *uncertainty_frames = (offset < stamp.uncertainty_frames)
                          ? (uint32_t)offset
                          : stamp.uncertainty_frames;
  return AUDIO_TIMELINE_OK;
}
