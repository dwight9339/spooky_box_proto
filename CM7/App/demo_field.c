#include "demo_field.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_events.h"
#include "audio_path_service.h"
#include "classic_adapter.h"
#include "clip_decimator.h"
#include "command_policy.h"
#include "demo_clip.h"
#include "demo_instrument.h"
#include "demo_lights.h"
#include "demo_rolling.h"
#include "demo_view.h"
#include "emf_level.h"
#include "grain_matrix.h"
#include "main.h"
#include "matrix_service.h"
#include "radio_adapter.h"
#include "radio_control_service.h"
#include "radio_recorder.h"
#include "session_control.h"
#include "sm/context_port.h"
#include "sm/input_resolution_port.h"
#include "ui_board_test.h"
#include "usb_test.h"

_Static_assert((uint32_t)INP_CONTROL_COUNT == 6U, "controls follow the UI switch order");
_Static_assert((uint32_t)CTX_BAND_COUNT == (uint32_t)RADIO_BAND_COUNT,
               "the band menu lists the receiver's bands in RadioBand order");
_Static_assert(((uint32_t)DEMO_CLIP_VIEW_FAILED == (uint32_t)DEMO_CLIP_FAILED) &&
               ((uint32_t)DEMO_CLIP_VIEW_READY == (uint32_t)DEMO_CLIP_READY) &&
               ((uint32_t)DEMO_CLIP_VIEW_LOADING == (uint32_t)DEMO_CLIP_LOADING) &&
               ((uint32_t)DEMO_CLIP_VIEW_SAVING == (uint32_t)DEMO_CLIP_SAVING),
               "the view shows the clip in DemoClipState order");
_Static_assert(((uint32_t)DEMO_CLIP_REASON_LOAD_FAILED == (uint32_t)DEMO_CLIP_FAULT_LOAD_FAILED) &&
               ((uint32_t)DEMO_CLIP_REASON_SAVE_FAILED == (uint32_t)DEMO_CLIP_FAULT_SAVE_FAILED) &&
               ((uint32_t)DEMO_CLIP_REASON_SAVE_UNAVAILABLE ==
                (uint32_t)DEMO_CLIP_FAULT_SAVE_UNAVAILABLE) &&
               ((uint32_t)DEMO_CLIP_REASON_SAVE_BUSY == (uint32_t)DEMO_CLIP_FAULT_SAVE_BUSY),
               "the view names clip faults in DemoClipFault order");

/* View cadence: the frame is composed at most this often, and less often while
 * the recorder captures (PRES-R5). One OLED page is written per pass. */
#define DEMO_COMPOSE_MS 50U
#define DEMO_COMPOSE_CAPTURE_MS 100U
/* Notice durations; faults stay up longer (Principle I: failures are visible). */
#define DEMO_NOTICE_MS 2000U
#define DEMO_FAULT_NOTICE_MS 8000U
#define DEMO_BAND_NOTICE_MS 3000U

typedef struct
{
  uint32_t inputs;           /* inputs the queue admitted */
  uint32_t inputs_refused;   /* inputs the queue refused; it then reconciles */
  uint32_t gestures;
  uint32_t gestures_refused;
  uint32_t ticks;
  uint32_t reconciles;       /* reconcile events dispatched */
  uint32_t commands_refused; /* commands the queue refused (BUSY notice) */
  uint32_t commands_rejected;/* commands the policy rejected */
  uint32_t session_changes_refused;
  uint32_t clip_returns;         /* Shift+B0 gestures posted after a clip failure */
  uint32_t clip_returns_refused; /* refused by the queue, posted again later */
  uint32_t prompts_refused;      /* session prompts refused in Instrument */
  uint32_t display_us_max;   /* longest OLED page write */
  uint32_t matrix_capture_ms;          /* time observed while capturing */
  uint32_t matrix_capture_frames;      /* frames composed while capturing */
  uint32_t matrix_capture_superseded;  /* of those, replaced before fully written */
} DemoCounters;

static bool display_ready;
static bool lights_ready;
static bool pressed[INP_CONTROL_COUNT];
static bool tick_posted;
static CtxMode mode;
static CtxEngine field_engine;
static uint8_t notice;
static uint8_t notice_arg;
static uint32_t notice_until_ms;
static bool session_started;
static uint32_t session_started_ms;
static uint32_t last_compose_ms;
static bool composed_once;
static DemoLightsOutput lights_shown;
static bool lights_valid;
static uint32_t cycles_per_us;
static uint32_t matrix_last_ms;
static uint32_t matrix_last_frames;
static uint32_t matrix_last_superseded;
static DemoCounters counters;
static uint8_t last_buffer_state;
/* The answer to the latest save request, for the chord's load that follows it
 * in the same Context action (C-010). */
static uint8_t save_answer;
/* A clip failure sends Instrument back to Field once (p04.6). */
static bool clip_return_pending;
/* The provisional Instrument pages (p04.7); kept across visits. */
static DemoInstrument instrument;
static uint32_t instrument_gestures; /* encoder gestures taken by the pages */
static uint32_t matrix_grain_last_ms;
/* The grain view's frame cadence: 25 frames per second. */
#define DEMO_GRAIN_MATRIX_MS 40U

/* Classic's tunes, from the Radio machine's tune start (command written) to its
 * answer, per band and split by whether the recorder was capturing at the start
 * (decision 0011 item 13: measured tune timing; p04.4). */
typedef struct
{
  uint32_t tunes;
  uint32_t failed;  /* issued, then answered with anything other than tuned */
  uint32_t issue_failed; /* could not be issued: answered failed with no start */
  uint32_t max_us;
  uint64_t total_us;
} DemoTuneStats;

static DemoTuneStats tune_stats[RADIO_BAND_COUNT][2]; /* [band][capturing] */
static bool tune_timing;
static uint32_t tune_start_cycles;
static uint8_t tune_band;
static bool tune_capturing;

static uint32_t Now(void)
{
  return HAL_GetTick();
}

static void Notify(DemoNotice kind, uint8_t arg, uint32_t duration_ms)
{
  notice = (uint8_t)kind;
  notice_arg = arg;
  notice_until_ms = Now() + duration_ms;
}

/* --- Service interface ----------------------------------------------------- */

void DemoField_Init(void)
{
  uint8_t control;

  (void)memset(&counters, 0, sizeof(counters));
  (void)memset(tune_stats, 0, sizeof(tune_stats));
  tune_timing = false;
  tick_posted = false;
  mode = CTX_MODE_FIELD;
  field_engine = CTX_ENGINE_CLASSIC;
  notice = (uint8_t)DEMO_NOTICE_NONE;
  session_started = false;
  composed_once = false;
  lights_valid = false;
  save_answer = (uint8_t)DEMO_SAVE_NONE;
  clip_return_pending = false;
  instrument_gestures = 0U;
  DemoClip_Init();
  DemoInstrument_Init(&instrument);
  DemoClip_SetGrainParams(&instrument.params);
  for (control = 0U; control < (uint8_t)INP_CONTROL_COUNT; ++control)
  {
    pressed[control] = UiBoardTest_DemoPressed(control);
  }
  InputResolution_Init();
  Context_Init();
  DemoView_Init();
  DemoLights_Init();
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  cycles_per_us = SystemCoreClock / 1000000U;
  matrix_last_ms = Now();
  display_ready = UiBoardTest_DemoDisplayStart();
  lights_ready = UiBoardTest_DemoLightsStart();
  printf("[demo] Field on the M7 (decision 0011 item 12): display=%u lights=%u\r\n",
         display_ready ? 1U : 0U, lights_ready ? 1U : 0U);
}

void DemoField_OnControl(uint8_t control, bool is_pressed, uint32_t now_ms)
{
  if (control >= (uint8_t)INP_CONTROL_COUNT)
  {
    return;
  }
  pressed[control] = is_pressed;
  if (AppEvents_Post(EVQ_CLASS_INPUT, APP_EVENT_DEMO_INPUT,
                     (uint32_t)(is_pressed ? INP_INPUT_PRESS : INP_INPUT_RELEASE) |
                     ((uint32_t)control << 8), now_ms))
  {
    ++counters.inputs;
  }
  else
  {
    ++counters.inputs_refused;
  }
}

void DemoField_OnDetents(uint8_t encoder, int32_t detents, uint32_t now_ms)
{
  const int32_t clamped = (detents > 127) ? 127 : ((detents < -127) ? -127 : detents);

  if ((encoder >= INP_ENCODER_COUNT) || (clamped == 0))
  {
    return;
  }
  if (AppEvents_Post(EVQ_CLASS_INPUT, APP_EVENT_DEMO_INPUT,
                     (uint32_t)INP_INPUT_DETENTS | ((uint32_t)encoder << 8) |
                     ((uint32_t)(uint8_t)(int8_t)clamped << 16), now_ms))
  {
    ++counters.inputs;
  }
  else
  {
    ++counters.inputs_refused;
  }
}

/* Demo only (decision 0011 item 16): Context has no Instrument engine yet, so
 * the provisional pages take the encoder turns and the Encoder 3 click (C-024)
 * while Instrument has the controls. Everything else, Shift+B0 (C-028)
 * included, goes to Context. */
static bool InstrumentGesture(Gesture gesture)
{
  const bool page_gesture = (gesture.kind == (uint8_t)GESTURE_TURN) ||
                            ((gesture.kind == (uint8_t)GESTURE_CLICK) && (gesture.encoder == 3U));
  DemoClipStatus clip;
  CtxStatus ctx;

  Context_GetStatus(&ctx);
  if ((ctx.state != (uint8_t)CTX_STATE_INSTRUMENT) || !page_gesture)
  {
    return false;
  }
  /* The pages change only what the display shows: with no clip, or the plain
   * loop, there is no page, and the gesture does nothing. */
  DemoClip_GetStatus(&clip);
  if ((clip.state != (uint8_t)DEMO_CLIP_READY) || (DemoClip_Voice() != DEMO_VOICE_GRAIN))
  {
    return true;
  }
  if (gesture.kind == (uint8_t)GESTURE_TURN)
  {
    if (DemoInstrument_Turn(&instrument, gesture.encoder, gesture.detents))
    {
      DemoClip_SetGrainParams(&instrument.params);
    }
    ++instrument_gestures;
    return true;
  }
  DemoInstrument_NextPage(&instrument);
  ++instrument_gestures;
  return true;
}

void DemoField_Dispatch(const EvqEvent *event)
{
  InpInput input;

  switch (event->type)
  {
    case APP_EVENT_DEMO_INPUT:
      input.sequence = event->sequence;
      input.time_ms = event->arg1;
      input.kind = (uint8_t)(event->arg0 & 0xFFU);
      input.index = (uint8_t)((event->arg0 >> 8) & 0xFFU);
      input.detents = (int8_t)(uint8_t)((event->arg0 >> 16) & 0xFFU);
      input.reserved = 0U;
      InputResolution_OnInput(&input);
      break;
    case APP_EVENT_DEMO_GESTURE:
    {
      const Gesture gesture = Gesture_Unpack(event->arg0);

      if (!InstrumentGesture(gesture))
      {
        Context_OnGesture(gesture);
      }
      break;
    }
    case APP_EVENT_DEMO_TICK:
      tick_posted = false;
      Context_OnTick();
      InputResolution_OnTick();
      break;
    case APP_EVENT_DEMO_SESSION_CHANGED:
      Context_OnSessionChanged();
      InputResolution_OnSessionChanged();
      break;
    case APP_EVENT_RECONCILE:
      /* Context ends PTT and closes a menu; InputResolution releases every
       * control without firing an action (decision 0009 item 6). */
      ++counters.reconciles;
      Context_OnReconcile();
      InputResolution_OnReconcile();
      break;
    default:
      break;
  }
}

/* --- Published state for the surfaces -------------------------------------- */

static DemoScreen Screen(const CtxStatus *ctx, const InpStatus *inp)
{
  /* The start prompt is refused in Instrument (p04.6): it stays on Instrument. */
  if ((inp->state == (uint8_t)INP_STATE_START_PROMPT) &&
      (ctx->state != (uint8_t)CTX_STATE_INSTRUMENT))
  {
    return DEMO_SCREEN_PROMPT_START;
  }
  if (inp->state == (uint8_t)INP_STATE_STOP_PROMPT)
  {
    return DEMO_SCREEN_PROMPT_STOP;
  }
  switch (ctx->state)
  {
    case CTX_STATE_MANUAL:
    case CTX_STATE_MANUAL_QUICK_JUMP:
      return DEMO_SCREEN_MANUAL;
    case CTX_STATE_ENGINE_MENU:
      return DEMO_SCREEN_ENGINE_MENU;
    case CTX_STATE_BAND_MENU:
      return DEMO_SCREEN_BAND_MENU;
    case CTX_STATE_INSTRUMENT:
      return DEMO_SCREEN_INSTRUMENT;
    case CTX_STATE_UTILITY:
      return DEMO_SCREEN_UTILITY;
    case CTX_STATE_CLASSIC:
    default:
      return DEMO_SCREEN_CLASSIC;
  }
}

static void BuildModel(uint32_t now, const CtxStatus *ctx, const InpStatus *inp,
                       DemoViewModel *model)
{
  RadioControlStatus radio;
  ClassicState classic;
  EmfLevelReading emf;
  DemoRollStatus rolling;
  DemoClipStatus clip;
  const RadState radio_state = Radio_GetState();

  (void)memset(model, 0, sizeof(*model));
  model->screen = (uint8_t)Screen(ctx, inp);
  model->session = (uint8_t)Session_GetState();
  model->shift = InputResolution_ShiftActive();
  model->radio_ok = (radio_state == RAD_STATE_SETTLED) || (radio_state == RAD_STATE_TUNING);
  (void)RadioControl_GetStatus(&radio);
  model->band = (uint8_t)radio.tune.band;
  model->frequency_khz = radio.tune.frequency_khz;
  if (ClassicAdapter_GetState(&classic))
  {
    model->run_state = classic.run_state;
    model->unable_reason = classic.unable_reason;
    model->edge = classic.edge;
    model->direction_up = classic.direction_up;
    model->rate_limited = classic.rate_limited;
    model->rate_per_min = classic.rate_per_min;
    model->distance_channels = classic.distance_channels;
    model->hold_seconds = classic.hold_seconds;
  }
  model->menu_highlight = ctx->highlight;
  if (Session_IsActive() && session_started)
  {
    model->session_seconds = (now - session_started_ms) / 1000U;
  }
  (void)EmfLevel_Get(now, &emf);
  model->emf_known = emf.state == EMF_LEVEL_VALID;
  model->emf_uT = emf.emf_uT;
  DemoRolling_GetStatus(&rolling);
  model->buffer = rolling.state;
  model->buffer_seconds = rolling.retained_ms / 1000U;
  DemoClip_GetStatus(&clip);
  model->clip = clip.state;
  model->clip_capture = clip.capture;
  model->clip_tenths = (clip.samples * 10U) / CLIP_RATE_HZ;
  model->clip_playing = clip.playing;
  {
    DemoVoiceStatus voice;

    DemoClip_GetVoiceStatus(&voice);
    model->voice_loop = voice.voice == (uint8_t)DEMO_VOICE_LOOP;
    model->grains_active = (uint8_t)voice.grains_active;
  }
  model->instrument_page = instrument.page;
  model->grain = instrument.params;
  model->notice = notice;
  model->notice_arg = notice_arg;
}

static void ServiceDisplay(uint32_t now, const DemoViewModel *model)
{
  const uint32_t interval = RadioRecorder_IsCapturing() ? DEMO_COMPOSE_CAPTURE_MS
                                                        : DEMO_COMPOSE_MS;
  const uint8_t *bytes;
  uint8_t page;

  if (!display_ready)
  {
    return;
  }
  if (!composed_once || ((now - last_compose_ms) >= interval))
  {
    DemoView_Compose(model);
    last_compose_ms = now;
    composed_once = true;
  }
  if (DemoView_NextPage(&page, &bytes))
  {
    const uint32_t start = DWT->CYCCNT;
    const bool ok = UiBoardTest_DemoDisplayWritePage(page, bytes);
    const uint32_t elapsed_us = (DWT->CYCCNT - start) / cycles_per_us;

    DemoView_PageDone(page, ok);
    if (elapsed_us > counters.display_us_max)
    {
      counters.display_us_max = elapsed_us;
    }
  }
}

static void ServiceLights(uint32_t now, const CtxStatus *ctx, const DemoViewModel *model)
{
  DemoLightsInput input;
  DemoLightsOutput output;

  if (!lights_ready)
  {
    return;
  }
  input.screen = model->screen;
  input.shift = model->shift;
  input.session_active = Session_IsActive();
  input.button_pressed[0] = pressed[INP_BUTTON0];
  input.button_pressed[1] = pressed[INP_BUTTON1];
  input.shift_mode_switch = !Session_IsActive();
  /* Shift+B1 saves on Field pages only; Instrument has no Shift+B1 action. */
  input.shift_save = !Session_IsActive() && (model->buffer == DEMO_BUFFER_RUNNING) &&
                     (mode == CTX_MODE_FIELD);
  input.shift_quick_jump = (ctx->state == (uint8_t)CTX_STATE_CLASSIC) ||
                           (ctx->state == (uint8_t)CTX_STATE_MANUAL_QUICK_JUMP);
  DemoLights_Compute(&input, now, &output);
  if (!lights_valid || (memcmp(&output, &lights_shown, sizeof(output)) != 0))
  {
    UiBoardTest_DemoSetLights(output.button_duty, output.encoder_rgb);
    lights_shown = output;
    lights_valid = true;
  }
}

/* Frames the matrix composes while the recorder captures (decision 0011 item 12:
 * a reduced, measured frame rate). */
static void MeasureMatrix(uint32_t now)
{
  MatrixServiceStatus matrix;

  MatrixService_GetStatus(&matrix);
  if (RadioRecorder_IsCapturing() && matrix.enabled)
  {
    counters.matrix_capture_ms += now - matrix_last_ms;
    counters.matrix_capture_frames += matrix.frames - matrix_last_frames;
    counters.matrix_capture_superseded += matrix.frames_superseded - matrix_last_superseded;
  }
  matrix_last_ms = now;
  matrix_last_frames = matrix.frames;
  matrix_last_superseded = matrix.frames_superseded;
}

/* The clip plays while Instrument has it. After a clip failure, a Shift+B0
 * gesture takes Instrument back to Field (C-028, decision 0011 item 15); it is
 * posted again only if the queue refused it. */
static void ServiceClip(void)
{
  if (clip_return_pending)
  {
    if (mode != CTX_MODE_INSTRUMENT)
    {
      clip_return_pending = false;
    }
    else
    {
      Gesture gesture;

      (void)memset(&gesture, 0, sizeof(gesture));
      gesture.kind = (uint8_t)GESTURE_SHIFT_BUTTON0;
      if (AppEvents_Post(EVQ_CLASS_INTERNAL, APP_EVENT_DEMO_GESTURE, Gesture_Pack(gesture),
                         0U))
      {
        clip_return_pending = false;
        ++counters.clip_returns;
        printf("[demo] t=%lu clip failed: back to Field\r\n", (unsigned long)Now());
      }
      else
      {
        ++counters.clip_returns_refused;
      }
    }
  }
  DemoClip_SetPlaying(mode == CTX_MODE_INSTRUMENT);
}

void DemoField_Service(void)
{
  const uint32_t now = Now();
  DemoViewModel model;
  CtxStatus ctx;
  InpStatus inp;

  if (!tick_posted && (InputResolution_TickDue(now) || Context_TickDue(now)))
  {
    tick_posted = AppEvents_Post(EVQ_CLASS_INTERNAL, APP_EVENT_DEMO_TICK, 0U, 0U);
    counters.ticks += tick_posted ? 1U : 0U;
  }
  if ((notice != (uint8_t)DEMO_NOTICE_NONE) && ((int32_t)(now - notice_until_ms) >= 0))
  {
    notice = (uint8_t)DEMO_NOTICE_NONE;
  }
  MeasureMatrix(now);
  {
    DemoRollStatus rolling;

    DemoRolling_GetStatus(&rolling);
    if ((rolling.state == (uint8_t)DEMO_ROLL_FAULT) &&
        (last_buffer_state != (uint8_t)DEMO_ROLL_FAULT))
    {
      Notify(DEMO_NOTICE_BUFFER_FAULT, 0U, DEMO_FAULT_NOTICE_MS);
    }
    last_buffer_state = rolling.state;
  }
  ServiceClip();
  Context_GetStatus(&ctx);
  InputResolution_GetStatus(&inp);
  BuildModel(now, &ctx, &inp, &model);
  ServiceDisplay(now, &model);
  ServiceLights(now, &ctx, &model);
}

bool DemoField_InstrumentMatrix(uint32_t now_ms, MatrixFeedbackFrame *frame, bool *due)
{
  uint16_t positions[GRANULAR_MAX_GRAINS];
  uint8_t envelopes[GRANULAR_MAX_GRAINS];
  DemoClipStatus clip;
  GrainMatrixInput input;

  *due = false;
  if (mode != CTX_MODE_INSTRUMENT)
  {
    return false;
  }
  if ((now_ms - matrix_grain_last_ms) < DEMO_GRAIN_MATRIX_MS)
  {
    return true;
  }
  matrix_grain_last_ms = now_ms;
  DemoClip_GetStatus(&clip);
  input.clip_ready = clip.state == (uint8_t)DEMO_CLIP_READY;
  input.recording = Session_IsActive();
  input.position_permille = instrument.params.position_permille;
  input.grain_permille = positions;
  input.grain_envelope = envelopes;
  input.grains = (input.clip_ready && clip.playing &&
                  (DemoClip_Voice() == DEMO_VOICE_GRAIN))
                   ? DemoClip_GetGrains(positions, envelopes, GRANULAR_MAX_GRAINS) : 0U;
  GrainMatrix_Compose(&input, frame);
  *due = true;
  return true;
}

bool DemoField_ClassicActive(void)
{
  return (mode == CTX_MODE_FIELD) && (field_engine == CTX_ENGINE_CLASSIC);
}

/* --- Published events from other regions ----------------------------------- */

void DemoField_OnSessionEvent(SesPublished event)
{
  const uint32_t now = Now();

  switch (event)
  {
    case SES_PUB_RECORDING_STARTED:
      session_started = true;
      session_started_ms = now;
      Notify(DEMO_NOTICE_SESSION_STARTED, 0U, DEMO_NOTICE_MS);
      break;
    case SES_PUB_RECORDING_REJECTED:
      DemoLights_OnSessionFault(now);
      Notify(DEMO_NOTICE_SESSION_REJECTED, 0U, DEMO_FAULT_NOTICE_MS);
      break;
    case SES_PUB_RECORDING_COMPLETED:
      session_started = false;
      Notify(DEMO_NOTICE_SESSION_SAVED, 0U, DEMO_NOTICE_MS);
      break;
    case SES_PUB_RECORDING_FILE_LIMIT:
      session_started = false;
      Notify(DEMO_NOTICE_SESSION_LIMIT, 0U, DEMO_NOTICE_MS);
      break;
    case SES_PUB_RECORDING_ABORTED:
      session_started = false;
      DemoLights_OnSessionFault(now);
      Notify(DEMO_NOTICE_SESSION_ABORTED, 0U, DEMO_FAULT_NOTICE_MS);
      break;
    case SES_PUB_RECORDING_CARD_FULL:
      session_started = false;
      DemoLights_OnSessionFault(now);
      Notify(DEMO_NOTICE_CARD_FULL, 0U, DEMO_FAULT_NOTICE_MS);
      break;
    case SES_PUB_RECORDING_CANCELLED:
      session_started = false;
      Notify(DEMO_NOTICE_SESSION_CANCELLED, 0U, DEMO_NOTICE_MS);
      break;
    default:
      break; /* preparing, stopping and an ignored stop show in the header */
  }
}

void DemoField_OnSessionStateChanged(void)
{
  if (!AppEvents_Post(EVQ_CLASS_INTERNAL, APP_EVENT_DEMO_SESSION_CHANGED, 0U, 0U))
  {
    ++counters.session_changes_refused;
  }
}

static void TimeTune(RadPublished event)
{
  DemoTuneStats *stats;
  uint32_t elapsed_us;

  if (event == RAD_PUB_TUNE_STARTED)
  {
    RadioControlStatus radio;

    (void)RadioControl_GetStatus(&radio);
    tune_timing = radio.band < RADIO_BAND_COUNT;
    tune_band = (uint8_t)radio.band;
    tune_capturing = RadioRecorder_IsCapturing();
    tune_start_cycles = DWT->CYCCNT;
    return;
  }
  if (!tune_timing)
  {
    /* No timed start: the tune could not be issued (RadioSm TuneIssue), or the
     * command was rejected or superseded before it was. Only the first is a
     * receiver failure. */
    if (event == RAD_PUB_TUNE_FAILED)
    {
      RadioControlStatus radio;

      (void)RadioControl_GetStatus(&radio);
      if (radio.band < RADIO_BAND_COUNT)
      {
        ++tune_stats[radio.band][RadioRecorder_IsCapturing() ? 1U : 0U].issue_failed;
      }
    }
    return;
  }
  tune_timing = false;
  elapsed_us = (DWT->CYCCNT - tune_start_cycles) / cycles_per_us;
  stats = &tune_stats[tune_band][tune_capturing ? 1U : 0U];
  ++stats->tunes;
  stats->failed += (event == RAD_PUB_TUNED) ? 0U : 1U;
  stats->total_us += elapsed_us;
  if (elapsed_us > stats->max_us)
  {
    stats->max_us = elapsed_us;
  }
}

void DemoField_OnRadioAnswer(RadPublished event, const RadCommand *command)
{
  const bool band_command = (command != NULL) &&
                            (command->source == (uint8_t)RAD_SOURCE_INTERNAL) &&
                            (command->kind == (uint8_t)RAD_CMD_BAND);

  if ((command != NULL) && (command->source == (uint8_t)RAD_SOURCE_INTERNAL) &&
      (command->kind == (uint8_t)RAD_CMD_TUNE))
  {
    TimeTune(event);
  }

  switch (event)
  {
    case RAD_PUB_BAND_CHANGED:
      if (band_command)
      {
        Notify(DEMO_NOTICE_BAND_CHANGED, (uint8_t)command->arg, DEMO_NOTICE_MS);
      }
      break;
    case RAD_PUB_FAULT_BAND:
      Notify(DEMO_NOTICE_BAND_FAILED, 0U, DEMO_FAULT_NOTICE_MS);
      break;
    case RAD_PUB_REJECTED_RANGE:
    case RAD_PUB_REJECTED_UNAVAILABLE:
    case RAD_PUB_SUPERSEDED:
    case RAD_PUB_ABANDONED:
      if (band_command)
      {
        Notify(DEMO_NOTICE_BAND_FAILED, 0U, DEMO_FAULT_NOTICE_MS);
      }
      break;
    case RAD_PUB_FAULT_START:
    case RAD_PUB_FAULT_AUDIO:
      Notify(DEMO_NOTICE_RADIO_FAULT, 0U, DEMO_FAULT_NOTICE_MS);
      break;
    default:
      break;
  }
}

/* --- InputResolution integration -------------------------------------------- */

void inp_integration_emit(Gesture gesture)
{
  if (AppEvents_Post(EVQ_CLASS_INTERNAL, APP_EVENT_DEMO_GESTURE, Gesture_Pack(gesture), 0U))
  {
    ++counters.gestures;
  }
  else
  {
    ++counters.gestures_refused;
  }
}

void inp_integration_release_all(void)
{
  /* Context takes the same reconcile event first (DemoField_Dispatch). */
}

bool inp_integration_on_page(void)
{
  return Context_OnPage();
}

bool inp_integration_session_active(void)
{
  return Session_IsActive();
}

uint32_t inp_integration_now_ms(void)
{
  return Now();
}

void inp_integration_issue(InpCommand command)
{
  const bool start = command == INP_CMD_START_SESSION;
  const CommandAction action = start ? COMMAND_ACTION_SESSION_START
                                     : COMMAND_ACTION_SESSION_STOP;
  bool posted;

  /* Demo narrowing of C-091 (decision 0011 item 15): no session from Instrument. */
  if (start && (mode == CTX_MODE_INSTRUMENT))
  {
    ++counters.commands_rejected;
    Notify(DEMO_NOTICE_SESSION_IN_INSTRUMENT, 0U, DEMO_NOTICE_MS);
    return;
  }
  if (CommandPolicy_Evaluate(action, Session_GetState()) != COMMAND_POLICY_ALLOWED)
  {
    ++counters.commands_rejected;
    Notify(DEMO_NOTICE_SESSION_REJECTED, 0U, DEMO_FAULT_NOTICE_MS);
    return;
  }
  /* An open-ended session, as RECORD START without a duration. */
  posted = start ? SessionControl_RequestStart(0U, AudioPath_IsRunning())
                 : SessionControl_RequestStop();
  printf("[demo] session %s from the prompt%s\r\n", start ? "start" : "stop",
         posted ? "" : ": queue full");
  if (!posted)
  {
    ++counters.commands_refused;
    Notify(DEMO_NOTICE_BUSY, 0U, DEMO_NOTICE_MS);
  }
}

void inp_integration_publish(InpPublished event)
{
  static const char *const names[] = {
    "PROMPT_OPENED_START", "PROMPT_OPENED_STOP", "PROMPT_CONFIRMED_START",
    "PROMPT_CONFIRMED_STOP", "PROMPT_CANCELLED", "PROMPT_WITHDRAWN",
    "SHIFT_ENTERED", "SHIFT_LEFT"
  };

  printf("[demo] t=%lu inp %s\r\n", (unsigned long)Now(),
         ((uint32_t)event < (sizeof(names) / sizeof(names[0]))) ? names[event] : "?");
  if ((event == INP_PUB_PROMPT_OPENED_START) && (mode == CTX_MODE_INSTRUMENT))
  {
    ++counters.prompts_refused;
    Notify(DEMO_NOTICE_SESSION_IN_INSTRUMENT, 0U, DEMO_NOTICE_MS);
  }
}

/* --- Context integration ---------------------------------------------------- */

bool ctx_integration_session_active(void)
{
  return Session_IsActive();
}

bool ctx_integration_band_change_allowed(void)
{
  return CommandPolicy_Evaluate(COMMAND_ACTION_RADIO_BAND, Session_GetState()) ==
         COMMAND_POLICY_ALLOWED;
}

bool ctx_integration_engine_available(CtxEngine engine)
{
  return engine == CTX_ENGINE_CLASSIC; /* Manual is not in the demo */
}

uint8_t ctx_integration_page_count(CtxEngine engine)
{
  (void)engine;
  return 1U;
}

CtxBand ctx_integration_current_band(void)
{
  RadioControlStatus radio;

  (void)RadioControl_GetStatus(&radio);
  return (radio.band < RADIO_BAND_COUNT) ? (CtxBand)radio.band : CTX_BAND_FM;
}

bool ctx_integration_utility_at_root(void)
{
  return true; /* no utility service in the demo */
}

uint32_t ctx_integration_now_ms(void)
{
  return Now();
}

static bool Allowed(CommandAction action)
{
  if (CommandPolicy_Evaluate(action, Session_GetState()) == COMMAND_POLICY_ALLOWED)
  {
    return true;
  }
  ++counters.commands_rejected;
  Notify(DEMO_NOTICE_NOT_WHILE_RECORDING, 0U, DEMO_NOTICE_MS);
  return false;
}

static void Posted(bool posted)
{
  if (!posted)
  {
    ++counters.commands_refused;
    Notify(DEMO_NOTICE_BUSY, 0U, DEMO_NOTICE_MS);
  }
}

/* C-009 (and the save half of C-010). Rolling capture is off during a session,
 * so a save is rejected there with its reason (decision 0011 item 14). */
static void RequestSave(void)
{
  save_answer = (uint8_t)DEMO_SAVE_NONE;
  if (Session_IsActive())
  {
    Notify(DEMO_NOTICE_SAVE_IN_SESSION, 0U, DEMO_NOTICE_MS);
    return;
  }
  save_answer = (uint8_t)DemoRolling_RequestSave();
  switch ((DemoSaveOutcome)save_answer)
  {
    case DEMO_SAVE_WRITING:
      Notify(DEMO_NOTICE_SAVE_WRITING, 0U, DEMO_FAULT_NOTICE_MS); /* until the outcome */
      break;
    case DEMO_SAVE_BUSY:
      Notify(DEMO_NOTICE_SAVE_BUSY, 0U, DEMO_NOTICE_MS);
      break;
    case DEMO_SAVE_UNAVAILABLE:
    default:
      Notify(DEMO_NOTICE_SAVE_UNAVAILABLE, 0U, DEMO_NOTICE_MS);
      break;
  }
}

void DemoField_OnSaveOutcome(uint8_t outcome)
{
  switch (outcome)
  {
    case DEMO_SAVE_SAVED:
      Notify(DEMO_NOTICE_SAVE_DONE, 0U, DEMO_NOTICE_MS);
      break;
    case DEMO_SAVE_UNAVAILABLE:
      Notify(DEMO_NOTICE_SAVE_UNAVAILABLE, 0U, DEMO_NOTICE_MS);
      break;
    case DEMO_SAVE_FAILED:
      Notify(DEMO_NOTICE_SAVE_FAILED, 0U, DEMO_FAULT_NOTICE_MS);
      break;
    default:
      break;
  }
}

/* The load half of C-010, right after its save in the same Context action.
 * Only the save just accepted can feed the clip; otherwise the clip fails with
 * the save's reason and Instrument goes back to Field. */
static void RequestLoad(void)
{
  switch ((DemoSaveOutcome)save_answer)
  {
    case DEMO_SAVE_WRITING:
      DemoClip_Expect();
      break;
    case DEMO_SAVE_BUSY:
      DemoClip_Fail(DEMO_CLIP_FAULT_SAVE_BUSY);
      break;
    case DEMO_SAVE_UNAVAILABLE:
    default:
      DemoClip_Fail(DEMO_CLIP_FAULT_SAVE_UNAVAILABLE);
      break;
  }
  save_answer = (uint8_t)DEMO_SAVE_NONE;
}

void DemoField_OnClipOutcome(uint8_t state, uint8_t fault)
{
  if (state == (uint8_t)DEMO_CLIP_READY)
  {
    Notify(DEMO_NOTICE_CLIP_LOADED, 0U, DEMO_NOTICE_MS);
    return;
  }
  Notify(DEMO_NOTICE_CLIP_FAILED, fault, DEMO_FAULT_NOTICE_MS);
  clip_return_pending = true;
}

void ctx_integration_command(CtxCommand command, int32_t arg)
{
  switch (command)
  {
    case CTX_CMD_RUN_PAUSE:
    case CTX_CMD_TOGGLE_DIRECTION:
    case CTX_CMD_JUMP_RATE:
    case CTX_CMD_JUMP_DISTANCE:
    case CTX_CMD_EDGE_BEHAVIOR:
    case CTX_CMD_HOLD_TIME:
      if (Allowed(COMMAND_ACTION_SCAN_PARAMETER))
      {
        Posted(ClassicAdapter_RequestInternalCommand(command, arg));
      }
      break;
    case CTX_CMD_SWITCH_BAND:
      if (Allowed(COMMAND_ACTION_RADIO_BAND))
      {
        const bool posted = RadioAdapter_RequestInternalBand((uint32_t)arg);

        Posted(posted);
        if (posted)
        {
          Notify(DEMO_NOTICE_BAND_SWITCHING, (uint8_t)arg, DEMO_BAND_NOTICE_MS);
        }
      }
      break;
    case CTX_CMD_CAPTURE_SAVE:
      RequestSave();
      break;
    case CTX_CMD_LOAD_CAPTURE:
      RequestLoad();
      break;
    case CTX_CMD_MONITOR_PTT:  /* no monitor stage yet: full_spooky_proto-54w.9 */
    case CTX_CMD_SELECT_ENGINE:/* followed by CTX_PUB_ENGINE_CHANGED */
    case CTX_CMD_SET_MODE:     /* followed by CTX_PUB_MODE_CHANGED */
    case CTX_CMD_TUNE:         /* Manual is not in the demo */
    case CTX_CMD_TOGGLE_WRAP:
    case CTX_CMD_UTILITY_OPEN: /* no utility service in the demo */
    case CTX_CMD_UTILITY_CLOSE:
    case CTX_CMD_UTILITY_SCROLL:
    case CTX_CMD_UTILITY_SELECT:
    case CTX_CMD_UTILITY_BACK:
    default:
      break;
  }
}

void ctx_integration_publish(CtxPublished event, int32_t arg)
{
  static const char *const names[] = {
    "MODE_CHANGED", "ENGINE_CHANGED", "PAGE_CHANGED", "MENU_OPENED", "MENU_HIGHLIGHT",
    "MENU_CLOSED", "MENU_WITHDRAWN", "UTILITY_OPENED", "UTILITY_CLOSED", "ACTION_REJECTED"
  };

  switch (event)
  {
    case CTX_PUB_MODE_CHANGED:
      mode = (CtxMode)arg;
      break;
    case CTX_PUB_ENGINE_CHANGED:
      field_engine = (CtxEngine)arg;
      break;
    case CTX_PUB_ACTION_REJECTED:
      Notify((arg == (int32_t)CTX_ACTION_BAND_CHANGE) ? DEMO_NOTICE_BAND_UNAVAILABLE
                                                      : DEMO_NOTICE_MODE_UNAVAILABLE,
             0U, DEMO_NOTICE_MS);
      break;
    default:
      break;
  }
  printf("[demo] t=%lu ctx %s %ld\r\n", (unsigned long)Now(),
         ((uint32_t)event < (sizeof(names) / sizeof(names[0]))) ? names[event] : "?",
         (long)arg);
}

/* --- CLI ----------------------------------------------------------------------- */

/* `DEMO LIGHTS` reports and `DEMO LIGHTS IDLE <0-1000>` sets the button idle
 * level in permille of perceived lightness, for setting it by eye on the bench.
 * Not kept across a reset. */
static bool HandleLights(const char *command)
{
  static const char prefix[] = "DEMO LIGHTS IDLE ";
  char response[96];
  const char *digits;
  uint32_t value = 0U;
  uint32_t count = 0U;

  if (strcmp(command, "DEMO LIGHTS") != 0)
  {
    if (strncmp(command, prefix, sizeof(prefix) - 1U) != 0)
    {
      return false;
    }
    for (digits = &command[sizeof(prefix) - 1U]; (*digits >= '0') && (*digits <= '9');
         ++digits)
    {
      value = (value * 10U) + (uint32_t)(*digits - '0');
      ++count;
      if (count > 4U)
      {
        break;
      }
    }
    if ((count == 0U) || (count > 4U) || (*digits != '\0') || (value > 1000U))
    {
      (void)UsbTest_SendText("ERR usage: DEMO LIGHTS [IDLE 0..1000]\r\n");
      return true;
    }
    DemoLights_SetIdleLightness((uint16_t)value);
  }
  (void)snprintf(response, sizeof(response),
                 "OK DEMO LIGHTS IDLE=%u DUTY=%u PRESSED_DUTY=1000\r\n",
                 (unsigned)DemoLights_IdleLightness(),
                 (unsigned)DemoLights_LightnessToDuty(DemoLights_IdleLightness()));
  (void)UsbTest_SendText(response);
  return true;
}

/* `DEMO TUNES`: Classic's tune timing per band, idle and while capturing;
 * `DEMO TUNES RESET` clears it. The reply goes out as one USB transfer, because
 * UsbTest_SendText drops a second send while the first is in flight. */
static bool HandleTunes(const char *command)
{
  static const char *const bands[RADIO_BAND_COUNT] = {"FM", "AM", "SW", "LW"};
  static char response[1000]; /* one USB transfer: at most 1 KiB */
  size_t used = 0U;
  uint32_t band;
  uint32_t capturing;

  if (strcmp(command, "DEMO TUNES RESET") == 0)
  {
    (void)memset(tune_stats, 0, sizeof(tune_stats));
    tune_timing = false;
    (void)UsbTest_SendText("OK DEMO TUNES RESET\r\n");
    return true;
  }
  if (strcmp(command, "DEMO TUNES") != 0)
  {
    return false;
  }
  for (band = 0U; band < RADIO_BAND_COUNT; ++band)
  {
    for (capturing = 0U; capturing < 2U; ++capturing)
    {
      const DemoTuneStats *stats = &tune_stats[band][capturing];

      if ((stats->tunes == 0U) && (stats->issue_failed == 0U))
      {
        continue;
      }
      const int written =
        snprintf(&response[used], sizeof(response) - used,
                 "DEMO TUNES BAND=%s CAPTURING=%lu TUNES=%lu FAILED=%lu "
                 "ISSUE_FAILED=%lu MEAN_US=%lu MAX_US=%lu\r\n",
                 bands[band], (unsigned long)capturing, (unsigned long)stats->tunes,
                 (unsigned long)stats->failed, (unsigned long)stats->issue_failed,
                 (stats->tunes == 0U) ? 0UL
                                      : (unsigned long)(stats->total_us / stats->tunes),
                 (unsigned long)stats->max_us);

      if ((written > 0) && ((size_t)written < (sizeof(response) - used)))
      {
        used += (size_t)written;
      }
    }
  }
  (void)snprintf(&response[used], sizeof(response) - used, "OK DEMO TUNES END\r\n");
  (void)UsbTest_SendText(response);
  return true;
}

/* `ROLL`: rolling-capture status. `ROLL ON|OFF` turns it on or off (off frees
 * the card for SD and WAV commands). `ROLL SAVE` is C-009 from the CLI. */
static bool HandleRoll(const char *command)
{
  static char response[512];
  RadioRecorderRollingStats stream;
  DemoRollStatus rolling;

  if ((strcmp(command, "ROLL") != 0) && (strncmp(command, "ROLL ", 5U) != 0))
  {
    return false;
  }
  if (strcmp(command, "ROLL ON") == 0)
  {
    (void)RadioRecorder_SetRolling(true);
  }
  else if (strcmp(command, "ROLL OFF") == 0)
  {
    if (!RadioRecorder_SetRolling(false))
    {
      (void)UsbTest_SendText("ERR ROLL OFF refused: a capture save is in progress\r\n");
      return true;
    }
  }
  else if (strcmp(command, "ROLL SAVE") == 0)
  {
    RequestSave();
  }
  else if ((strcmp(command, "ROLL STATUS") != 0) && (strcmp(command, "ROLL") != 0))
  {
    (void)UsbTest_SendText("ERR usage: ROLL [STATUS|ON|OFF|SAVE]\r\n");
    return true;
  }
  DemoRolling_GetStatus(&rolling);
  RadioRecorder_GetRollingStats(&stream);
  (void)snprintf(response, sizeof(response),
                 "OK ROLL STATE=%s ENABLED=%u RETAINED_MS=%lu SEGMENTS=%lu ROTATIONS=%lu "
                 "ROTATE_MS_MAX=%lu STEP_MS_MAX=%lu WRITE_MS_MAX=%lu QUEUES=%u,%u/8 "
                 "BLOCKS=%lu STARTS=%lu FAULTS=%lu RECLAIMED=%lu ALLOC_FAIL=%lu "
                 "SAVE=%s SAVES=%lu FAILED=%lu BUSY=%lu UNAVAILABLE=%lu SAVE_MS_MAX=%lu "
                 "LAST=C%03lu LAST_FRAMES=%lu\r\n",
                 DemoRolling_StateName(rolling.state), stream.enabled ? 1U : 0U,
                 (unsigned long)rolling.retained_ms, (unsigned long)rolling.segments,
                 (unsigned long)rolling.rotations, (unsigned long)rolling.rotate_ms_max,
                 (unsigned long)rolling.step_ms_max, (unsigned long)stream.max_write_ms,
                 (unsigned)stream.radio_high_water, (unsigned)stream.pdm_high_water,
                 (unsigned long)stream.blocks, (unsigned long)stream.starts,
                 (unsigned long)rolling.faults, (unsigned long)rolling.reclaimed,
                 (unsigned long)rolling.allocation_failures,
                 DemoRolling_SaveName(rolling.save), (unsigned long)rolling.saves,
                 (unsigned long)rolling.saves_failed, (unsigned long)rolling.saves_busy,
                 (unsigned long)rolling.saves_unavailable,
                 (unsigned long)rolling.save_ms_max, (unsigned long)rolling.last_capture,
                 (unsigned long)rolling.last_capture_frames);
  (void)UsbTest_SendText(response);
  return true;
}

/* `DEMO CLIP`: the Instrument clip and its loads (p04.6). */
static bool HandleClip(const char *command)
{
  char response[320];
  DemoClipStatus clip;

  if (strcmp(command, "DEMO CLIP") != 0)
  {
    return false;
  }
  DemoClip_GetStatus(&clip);
  (void)snprintf(response, sizeof(response),
                 "OK DEMO CLIP STATE=%s FAULT=%s CAPTURE=C%03lu SAMPLES=%lu MS=%lu "
                 "PLAYING=%u LOOPS=%lu LOADS=%lu FAILURES=%lu LOAD_MS=%lu LOAD_MS_MAX=%lu "
                 "STEP_MS_MAX=%lu RETURNS=%lu RETURNS_REFUSED=%lu PROMPTS_REFUSED=%lu\r\n",
                 DemoClip_StateName(clip.state), DemoClip_FaultName(clip.fault),
                 (unsigned long)clip.capture, (unsigned long)clip.samples,
                 (unsigned long)((clip.samples * 1000U) / CLIP_RATE_HZ),
                 clip.playing ? 1U : 0U, (unsigned long)clip.loops,
                 (unsigned long)clip.loads, (unsigned long)clip.failures,
                 (unsigned long)clip.load_ms, (unsigned long)clip.load_ms_max,
                 (unsigned long)clip.step_ms_max, (unsigned long)counters.clip_returns,
                 (unsigned long)counters.clip_returns_refused,
                 (unsigned long)counters.prompts_refused);
  (void)UsbTest_SendText(response);
  return true;
}

/* `DEMO GRAIN`: the Instrument voice, its page and parameters, and its render
 * cost in the radio interrupt; `DEMO GRAIN RESET` clears the render maximum;
 * `DEMO GRAIN MAX <n>` sets the grain limit (1 to 16), for bench sweeps.
 * `DEMO VOICE GRAIN|LOOP` picks the voice (LOOP is p04.6's plain loop). */
static bool HandleGrain(const char *command)
{
  static char response[448];
  DemoVoiceStatus voice;
  const GranularParams *params = &instrument.params;

  if (strcmp(command, "DEMO VOICE GRAIN") == 0)
  {
    DemoClip_SetVoice(DEMO_VOICE_GRAIN);
  }
  else if (strcmp(command, "DEMO VOICE LOOP") == 0)
  {
    DemoClip_SetVoice(DEMO_VOICE_LOOP);
  }
  else if (strcmp(command, "DEMO GRAIN RESET") == 0)
  {
    DemoClip_ResetVoiceStats();
  }
  else if ((strncmp(command, "DEMO GRAIN MAX ", 15U) == 0) && (command[15] >= '1') &&
           (command[15] <= '9'))
  {
    DemoClip_SetMaxGrains((uint32_t)strtoul(&command[15], NULL, 10));
  }
  else if (strcmp(command, "DEMO GRAIN") != 0)
  {
    if ((strncmp(command, "DEMO VOICE", 10U) == 0) || (strncmp(command, "DEMO GRAIN", 10U) == 0))
    {
      (void)UsbTest_SendText("ERR usage: DEMO GRAIN [RESET | MAX <n>] | DEMO VOICE GRAIN|LOOP\r\n");
      return true;
    }
    return false;
  }
  DemoClip_GetVoiceStatus(&voice);
  (void)snprintf(response, sizeof(response),
                 "OK DEMO GRAIN VOICE=%s PAGE=%u POS=%u SIZE_MS=%u DENSITY=%u PITCH=%d "
                 "SPRAY=%u ENV=%u LEVEL=%u ACTIVE=%lu HIGH=%lu/%u STARTED=%lu "
                 "DROPPED=%lu RENDERS=%lu RENDER_US_MAX=%lu OVER_BUDGET=%lu BUDGET_US=%lu "
                 "GESTURES=%lu\r\n",
                 (voice.voice == (uint8_t)DEMO_VOICE_LOOP) ? "LOOP" : "GRAIN",
                 (unsigned)instrument.page + 1U, (unsigned)params->position_permille,
                 (unsigned)params->size_ms, (unsigned)params->density,
                 (int)params->pitch_semitones, (unsigned)params->spray_permille,
                 (unsigned)params->envelope_percent,
                 (unsigned)params->level_percent, (unsigned long)voice.grains_active,
                 (unsigned long)voice.grains_high_water, (unsigned)voice.max_grains,
                 (unsigned long)voice.grains_started, (unsigned long)voice.grains_dropped,
                 (unsigned long)voice.renders, (unsigned long)voice.render_us_max,
                 (unsigned long)voice.render_over_budget,
                 (unsigned long)voice.render_budget_us, (unsigned long)instrument_gestures);
  (void)UsbTest_SendText(response);
  return true;
}

bool DemoField_HandleCommand(const char *command)
{
  static const char *const screens[DEMO_SCREEN_COUNT] = {
    "CLASSIC", "MANUAL", "BAND_MENU", "ENGINE_MENU", "PROMPT_START", "PROMPT_STOP",
    "INSTRUMENT", "UTILITY"
  };
  DemoViewModel model;
  DemoViewStatus view;
  CtxStatus ctx;
  InpStatus inp;
  EvqStats queue;
  char response[384];

  if (HandleLights(command) || HandleTunes(command) || HandleRoll(command) ||
      HandleClip(command) || HandleGrain(command))
  {
    return true;
  }
  if ((strcmp(command, "DEMO") != 0) && (strcmp(command, "DEMO STATUS") != 0))
  {
    return false;
  }
  Context_GetStatus(&ctx);
  InputResolution_GetStatus(&inp);
  BuildModel(Now(), &ctx, &inp, &model);
  DemoView_GetStatus(&view);
  AppEvents_GetStats(&queue);
  (void)snprintf(response, sizeof(response),
                 "OK DEMO SCREEN=%s INP=%u SHIFT=%u INPUTS=%lu REFUSED=%lu "
                 "GESTURES=%lu/%lu UNBOUND=%lu RECONCILES=%lu TICKS=%lu "
                 "CMD_REJECTED=%lu CMD_REFUSED=%lu QUEUE_HIGH=%lu "
                 "OLED=%u FRAMES=%lu PAGES=%lu PAGE_FAIL=%lu PAGE_US_MAX=%lu "
                 "LIGHTS=%u MATRIX_REC_MS=%lu MATRIX_REC_FRAMES=%lu "
                 "MATRIX_REC_SUPERSEDED=%lu\r\n",
                 (model.screen < DEMO_SCREEN_COUNT) ? screens[model.screen] : "?",
                 (unsigned)inp.state, model.shift ? 1U : 0U,
                 (unsigned long)counters.inputs, (unsigned long)counters.inputs_refused,
                 (unsigned long)counters.gestures, (unsigned long)counters.gestures_refused,
                 (unsigned long)ctx.unbound, (unsigned long)counters.reconciles,
                 (unsigned long)counters.ticks, (unsigned long)counters.commands_rejected,
                 (unsigned long)counters.commands_refused, (unsigned long)queue.high_water,
                 display_ready ? 1U : 0U, (unsigned long)view.frames,
                 (unsigned long)view.pages_written, (unsigned long)view.pages_failed,
                 (unsigned long)counters.display_us_max, lights_ready ? 1U : 0U,
                 (unsigned long)counters.matrix_capture_ms,
                 (unsigned long)counters.matrix_capture_frames,
                 (unsigned long)counters.matrix_capture_superseded);
  (void)UsbTest_SendText(response);
  return true;
}
