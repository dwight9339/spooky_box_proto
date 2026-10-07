#include "matrix_service.h"

#include <stddef.h>
#include <string.h>

static bool service_ready;
static bool service_enabled;
static bool trail_enabled;
static bool frame_due;          /* compose on the next pass */
static MatrixServiceStatus service_status;

bool MatrixService_Init(uint32_t now_ms)
{
  MatrixFeedbackConfig config;

  MatrixFeedback_DefaultConfig(&config);
  service_ready = MatrixFeedback_Init(&config, now_ms);
  MatrixWriter_Init();
  service_enabled = false;
  trail_enabled = false;         /* off by default (decision 0019) */
  frame_due = false;
  (void)memset(&service_status, 0, sizeof(service_status));
  return service_ready;
}

void MatrixService_SetEnabled(bool enabled, uint32_t now_ms)
{
  MatrixFeedbackConfig config;

  if (!service_ready || (enabled == service_enabled))
  {
    return;
  }
  service_enabled = enabled;
  if (enabled)
  {
    /* A fresh loop at step 0, keeping the trail setting and session state. */
    MatrixFeedbackStatus renderer;

    (void)MatrixFeedback_GetStatus(&renderer);
    MatrixFeedback_DefaultConfig(&config);
    (void)MatrixFeedback_Init(&config, now_ms);
    MatrixFeedback_SetTrail(trail_enabled);
    MatrixFeedback_SetRecording((renderer.border == MATRIX_BORDER_RECORDING) ||
                                (renderer.border == MATRIX_BORDER_RECORDING_KICK));
    MatrixWriter_Init();
    frame_due = true;
  }
}

bool MatrixService_IsEnabled(void)
{
  return service_enabled;
}

void MatrixService_Invalidate(void)
{
  MatrixWriter_Init();
  frame_due = true;
}

void MatrixService_SetTrail(bool enabled)
{
  trail_enabled = enabled;
  if (service_ready)
  {
    MatrixFeedback_SetTrail(enabled);
    frame_due = true;
  }
}

void MatrixService_OnRecording(bool recording)
{
  if (service_ready)
  {
    MatrixFeedback_SetRecording(recording);
    frame_due = true;
  }
}

void MatrixService_OnRecordingFault(uint32_t now_ms)
{
  if (service_ready)
  {
    MatrixFeedback_OnRecordingFault(now_ms);
    frame_due = true;
  }
}

bool MatrixService_Service(uint32_t now_ms, const MatrixServiceInput *input,
                           MatrixWriterRun *run)
{
  if (!service_ready || !service_enabled || (input == NULL) || (run == NULL))
  {
    return false;
  }
  MatrixFeedback_SetEmf(input->emf_known, input->emf_bucket);
  if (input->onset != 0U)
  {
    MatrixFeedback_OnOnset(input->onset, now_ms);
  }
  if (MatrixFeedback_Advance(now_ms))
  {
    frame_due = true;
  }
  if (frame_due)
  {
    MatrixFeedbackFrame frame;
    MatrixWriterStatus writer;

    if (MatrixFeedback_Compose(&frame))
    {
      MatrixWriter_GetStatus(&writer);
      if (writer.pending_runs != 0U)
      {
        ++service_status.frames_superseded;
      }
      MatrixWriter_SetTarget(&frame);
      ++service_status.frames;
    }
    frame_due = false;
  }
  return MatrixWriter_Next(run);
}

#if defined(SPOOKY_DEMO)
bool MatrixService_ServiceFrame(const MatrixFeedbackFrame *frame, MatrixWriterRun *run)
{
  if (!service_ready || !service_enabled || (run == NULL))
  {
    return false;
  }
  if (frame != NULL)
  {
    MatrixWriterStatus writer;

    MatrixWriter_GetStatus(&writer);
    if (writer.pending_runs != 0U)
    {
      ++service_status.frames_superseded;
    }
    MatrixWriter_SetTarget(frame);
    ++service_status.frames;
  }
  frame_due = true; /* Field feedback redraws its own frame when it is back */
  return MatrixWriter_Next(run);
}
#endif

void MatrixService_RunDone(bool ok, uint32_t elapsed_us)
{
  MatrixWriter_Done(ok);
  if (ok)
  {
    ++service_status.runs_written;
  }
  else
  {
    ++service_status.runs_failed;
  }
  if (elapsed_us > service_status.run_us_max)
  {
    service_status.run_us_max = elapsed_us;
  }
}

void MatrixService_GetStatus(MatrixServiceStatus *status)
{
  MatrixFeedbackStatus renderer;
  MatrixWriterStatus writer;

  if (status == NULL)
  {
    return;
  }
  *status = service_status;
  status->enabled = service_enabled;
  status->trail = trail_enabled;
  if (MatrixFeedback_GetStatus(&renderer))
  {
    status->dropped_steps = renderer.dropped_steps;
  }
  MatrixWriter_GetStatus(&writer);
  status->pending_runs = writer.pending_runs;
}
