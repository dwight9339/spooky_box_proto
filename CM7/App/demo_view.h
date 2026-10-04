#ifndef SPOOKY_DEMO_VIEW_H
#define SPOOKY_DEMO_VIEW_H

/*
 * Demo-only OLED view (decision 0011 item 12, full_spooky_proto-p04.3). Builds
 * the 128x64 SSD1309 frame from a snapshot of published state and tracks which
 * of its eight 128-byte pages differ from what the display shows, so the owner
 * writes at most one page per foreground pass.
 *
 * The snapshot is rebuilt from published state for every frame (PRES-R1): the
 * view keeps no copy of Context, Session or Classic state. Layouts are a first
 * draft for bench review, not settled presentation (presentation.md Open
 * behavior: Classic view, utility entry and exit).
 *
 * Portable C with no HAL calls. Frame layout matches ui_render_service.c: byte
 * (page * 128 + x), bit (y & 7), bit 0 at the top.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEMO_VIEW_WIDTH 128U
#define DEMO_VIEW_HEIGHT 64U
#define DEMO_VIEW_PAGES 8U
#define DEMO_VIEW_FRAME_BYTES (DEMO_VIEW_WIDTH * DEMO_VIEW_PAGES)

/* What has the controls, from the Context and InputResolution status. */
typedef enum
{
  DEMO_SCREEN_CLASSIC = 0,
  DEMO_SCREEN_MANUAL,
  DEMO_SCREEN_BAND_MENU,
  DEMO_SCREEN_ENGINE_MENU,
  DEMO_SCREEN_PROMPT_START,
  DEMO_SCREEN_PROMPT_STOP,
  DEMO_SCREEN_INSTRUMENT,
  DEMO_SCREEN_UTILITY,
  DEMO_SCREEN_COUNT
} DemoScreen;

/* Rolling capture (demo_rolling.h DemoRollState order). */
typedef enum
{
  DEMO_BUFFER_OFF = 0,
  DEMO_BUFFER_WAITING,
  DEMO_BUFFER_RUNNING,
  DEMO_BUFFER_FAULT
} DemoBuffer;

/* Published Session state (SesState order). */
typedef enum
{
  DEMO_SESSION_IDLE = 0,
  DEMO_SESSION_RECORDING,
  DEMO_SESSION_FINALIZING,
  DEMO_SESSION_PREPARING
} DemoSession;

/* A domain event shown briefly on the bottom line, in reverse video. */
typedef enum
{
  DEMO_NOTICE_NONE = 0,
  DEMO_NOTICE_BAND_UNAVAILABLE,  /* CTX_PUB_ACTION_REJECTED band change */
  DEMO_NOTICE_MODE_UNAVAILABLE,  /* CTX_PUB_ACTION_REJECTED mode switch */
  DEMO_NOTICE_BAND_SWITCHING,    /* band command posted; arg is the target */
  DEMO_NOTICE_BAND_CHANGED,      /* RAD_PUB_BAND_CHANGED */
  DEMO_NOTICE_BAND_FAILED,       /* RAD_PUB_FAULT_BAND */
  DEMO_NOTICE_RADIO_FAULT,       /* RAD_PUB_FAULT_START or _AUDIO */
  DEMO_NOTICE_SESSION_STARTED,
  DEMO_NOTICE_SESSION_SAVED,
  DEMO_NOTICE_SESSION_LIMIT,
  DEMO_NOTICE_SESSION_REJECTED,
  DEMO_NOTICE_SESSION_ABORTED,
  DEMO_NOTICE_CARD_FULL,
  DEMO_NOTICE_SESSION_CANCELLED,
  DEMO_NOTICE_SAVE_WRITING,      /* C-009 accepted; the save is being committed */
  DEMO_NOTICE_SAVE_DONE,         /* the capture descriptor is committed */
  DEMO_NOTICE_SAVE_BUSY,         /* a save is already in progress */
  DEMO_NOTICE_SAVE_UNAVAILABLE,  /* no rolling window to save */
  DEMO_NOTICE_SAVE_IN_SESSION,   /* rolling capture is off during a session */
  DEMO_NOTICE_SAVE_FAILED,
  DEMO_NOTICE_BUFFER_FAULT,      /* rolling capture stopped by a fault */
  DEMO_NOTICE_NOT_WHILE_RECORDING,
  DEMO_NOTICE_BUSY,              /* the event queue refused a command */
  DEMO_NOTICE_COUNT
} DemoNotice;

typedef struct
{
  uint8_t screen;          /* DemoScreen */
  uint8_t session;         /* DemoSession */
  bool shift;
  bool radio_ok;           /* the radio runs and is not faulted */
  uint8_t band;            /* RadioBand of the last completed tune */
  uint32_t frequency_khz;  /* 0 before the first tune */
  /* Classic's published state (ClassicState). */
  uint8_t run_state;       /* ClassicPublishedRun */
  uint8_t unable_reason;   /* ClassicUnableReason */
  uint8_t edge;            /* ClassicEdge */
  bool direction_up;
  bool rate_limited;
  uint16_t rate_per_min;
  uint16_t distance_channels;
  uint16_t hold_seconds;   /* 0: the hold is off */
  uint8_t menu_highlight;  /* CtxBand or CtxEngine while a menu is open */
  uint32_t session_seconds;/* since SES_PUB_RECORDING_STARTED, while active */
  uint8_t buffer;          /* DemoBuffer */
  uint32_t buffer_seconds; /* rolling window held, while running */
  bool emf_known;          /* emf_level VALID */
  uint32_t emf_uT;
  uint8_t notice;          /* DemoNotice */
  uint8_t notice_arg;      /* band for the band notices */
} DemoViewModel;

typedef struct
{
  uint32_t frames;         /* frames composed */
  uint32_t pages_written;
  uint32_t pages_failed;
  uint8_t dirty_mask;      /* bit n: page n differs from the display */
} DemoViewStatus;

/* The display contents are unknown: every page is written again. */
void DemoView_Init(void);
void DemoView_Invalidate(void);
/* Composes a frame from the snapshot and marks the pages that differ. */
void DemoView_Compose(const DemoViewModel *model);
/* The lowest page that differs, and its 128 bytes; false when none does. */
bool DemoView_NextPage(uint8_t *page, const uint8_t **bytes);
/* The owner wrote that page; on failure it stays dirty. */
void DemoView_PageDone(uint8_t page, bool ok);
void DemoView_GetStatus(DemoViewStatus *status);
/* The composed frame, for tests. */
const uint8_t *DemoView_Frame(void);
/* The frequency as the view shows it: FM in MHz with one or two decimals, the
 * other bands in kHz, "---" before the first tune. Returns the unit. */
const char *DemoView_FormatFrequency(uint8_t band, uint32_t frequency_khz, char *text,
                                     size_t size);

#endif /* SPOOKY_DEMO_VIEW_H */
