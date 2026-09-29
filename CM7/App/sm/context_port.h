#ifndef SPOOKY_CONTEXT_PORT_H
#define SPOOKY_CONTEXT_PORT_H

/*
 * Port of the Context machine (docs/design/behavior/ContextSm.puml, decisions 0006,
 * 0008 and 0009). Three parts:
 *
 * 1. The service interface the M7 dispatcher calls, one function per event.
 * 2. The guards and actions the diagram calls. Nothing else may be called from the
 *    diagram.
 * 3. The integration functions the port calls. The firmware wiring provides them
 *    with the engine, radio, capture and utility services; host tests provide fakes.
 *
 * Context decides which operating mode, engine page, menu or utility has the
 * controls, and turns resolved gestures (gesture.h) into commands. It never changes
 * the Session region: commands go out through the shared command policy.
 *
 * Portable C with no HAL calls. Not reentrant: call it from the dispatcher only.
 */

#include <stdbool.h>
#include <stdint.h>

#include "gesture.h"

typedef enum CtxMode {
    CTX_MODE_FIELD = 0,
    CTX_MODE_INSTRUMENT
} CtxMode;

/* Field engines in the first slice. Seek and Orbit are not selectable yet. */
typedef enum CtxEngine {
    CTX_ENGINE_CLASSIC = 0,
    CTX_ENGINE_MANUAL,
    CTX_ENGINE_COUNT
} CtxEngine;

/* The operating pages. The Manual page records how Manual was entered, because the
 * quick-jump return slot changes what Shift plus Encoder 1 does (decision 0009
 * items 28 and 29). */
typedef enum CtxPage {
    CTX_PAGE_CLASSIC = 0,
    CTX_PAGE_MANUAL,
    CTX_PAGE_MANUAL_QUICK_JUMP,
    CTX_PAGE_INSTRUMENT
} CtxPage;

typedef enum CtxMenu {
    CTX_MENU_NONE = 0,
    CTX_MENU_ENGINE,
    CTX_MENU_BAND
} CtxMenu;

/* The receiver's bands, in menu order (radio.md SwitchBand). */
typedef enum CtxBand {
    CTX_BAND_FM = 0,
    CTX_BAND_AM,
    CTX_BAND_SW,
    CTX_BAND_LW,
    CTX_BAND_COUNT
} CtxBand;

/* Actions that can be rejected while a session is active (decision 0008 item 10). */
typedef enum CtxAction {
    CTX_ACTION_MODE_SWITCH = 0,
    CTX_ACTION_BAND_CHANGE
} CtxAction;

/* Commands issued through the shared command policy. The argument is 0 unless
 * noted. */
typedef enum CtxCommand {
    CTX_CMD_RUN_PAUSE = 0,     /* Classic: run or pause the scan */
    CTX_CMD_TOGGLE_DIRECTION,  /* Classic: toggle the scan direction */
    CTX_CMD_JUMP_RATE,         /* Classic: signed detents */
    CTX_CMD_JUMP_DISTANCE,     /* Classic: signed detents */
    CTX_CMD_TUNE,              /* Manual: signed detents, one band step each */
    CTX_CMD_TOGGLE_WRAP,       /* Manual: toggle wrap at the band edges */
    CTX_CMD_SWITCH_BAND,       /* CtxBand */
    CTX_CMD_SELECT_ENGINE,     /* CtxEngine; the engine service restores its state */
    CTX_CMD_SET_MODE,          /* CtxMode; sensor policy follows the mode */
    CTX_CMD_MONITOR_PTT,       /* 1 mutes the radio in the monitored mix, 0 restores it */
    CTX_CMD_CAPTURE_SAVE,      /* save the rolling capture */
    CTX_CMD_LOAD_CAPTURE,      /* load the saved capture as the Instrument sample */
    CTX_CMD_UTILITY_OPEN,      /* open the global utility root */
    CTX_CMD_UTILITY_CLOSE,
    CTX_CMD_UTILITY_SCROLL,    /* signed detents */
    CTX_CMD_UTILITY_SELECT,
    CTX_CMD_UTILITY_BACK
} CtxCommand;

/* Domain events the region publishes (presentation.md). The argument is 0 unless
 * noted. */
typedef enum CtxPublished {
    CTX_PUB_MODE_CHANGED = 0, /* CtxMode */
    CTX_PUB_ENGINE_CHANGED,   /* CtxEngine */
    CTX_PUB_PAGE_CHANGED,     /* page index within the active engine */
    CTX_PUB_MENU_OPENED,      /* CtxMenu */
    CTX_PUB_MENU_HIGHLIGHT,   /* highlighted CtxEngine or CtxBand */
    CTX_PUB_MENU_CLOSED,      /* CtxMenu */
    CTX_PUB_MENU_WITHDRAWN,   /* CtxMenu, closed by the policy rather than the user */
    CTX_PUB_UTILITY_OPENED,
    CTX_PUB_UTILITY_CLOSED,
    CTX_PUB_ACTION_REJECTED   /* CtxAction, "unavailable while recording" */
} CtxPublished;

/* Machine states, for status and diagnostics. */
typedef enum CtxState {
    CTX_STATE_CLASSIC = 0,
    CTX_STATE_MANUAL,
    CTX_STATE_MANUAL_QUICK_JUMP,
    CTX_STATE_ENGINE_MENU,
    CTX_STATE_BAND_MENU,
    CTX_STATE_INSTRUMENT,
    CTX_STATE_UTILITY
} CtxState;

/* The menu inactivity timeout (decision 0009 item 7) stays configurable until bench
 * trials set it (Principle IV). */
typedef struct CtxConfig {
    uint32_t menu_timeout_ms;
} CtxConfig;

#define CTX_DEFAULT_MENU_TIMEOUT_MS 5000u

typedef struct CtxStatus {
    uint8_t state;      /* CtxState */
    uint8_t menu;       /* CtxMenu */
    uint8_t highlight;  /* CtxEngine or CtxBand while a menu is open */
    uint8_t ptt;        /* 1 while PTT mutes the radio in the monitored mix */
    uint8_t page[CTX_ENGINE_COUNT]; /* parameter page index of each Field engine */
    uint8_t reserved[2];
    uint32_t gestures;  /* gestures dispatched */
    uint32_t unbound;   /* gestures with no binding in any state of this slice */
} CtxStatus;

/* --- 1. Service interface ------------------------------------------------------ */

/* Starts in Field with Classic (CTX-01) and the default configuration. */
void Context_Init(void);
void Context_Configure(const CtxConfig *config);

void Context_OnGesture(Gesture gesture);
/* The integration clock advanced: closes a menu whose inactivity timeout is due. */
void Context_OnTick(void);
bool Context_TickDue(uint32_t now_ms);
void Context_OnSessionChanged(void);
/* Input reporting restarted, overflowed or went stale. */
void Context_OnReconcile(void);

void Context_GetStatus(CtxStatus *status);
/* An operating page has the controls: no menu and no utility is open. */
bool Context_OnPage(void);

/* --- 2. Guards and actions called by the diagram ------------------------------- */

bool ctx_session_active(void);
bool ctx_band_change_allowed(void);
bool ctx_highlight_is_current(void);
bool ctx_highlight_is(int item);
bool ctx_field_page_is(CtxPage page);
bool ctx_current_page_is(CtxPage page);
bool ctx_utility_at_root(void);
int32_t ctx_detents(void);

void ctx_page_entered(CtxPage page);
void ctx_page_advance(void);
void ctx_select_engine(CtxEngine engine);
void ctx_select_band(void);
void ctx_mode_changed(CtxMode mode);
void ctx_menu_open(CtxMenu menu);
void ctx_menu_scroll(void);
void ctx_menu_close(void);
void ctx_utility_opened(void);
void ctx_utility_closed(void);
void ctx_ptt_on(void);
void ctx_ptt_off(void);
void ctx_reject(CtxAction action);
void ctx_command(CtxCommand command, int32_t arg);
void ctx_publish(CtxPublished event, int32_t arg);

/* --- 3. Integration functions, provided by the firmware wiring or a test ------- */

/* The Session region is active (recording or finalizing). */
bool ctx_integration_session_active(void);
/* The shared command policy would accept a band change now. While a session is
 * active this stays false until full_spooky_proto-54w.12 qualifies band changes
 * during recording (decisions 0003 and 0008). */
bool ctx_integration_band_change_allowed(void);
/* The engine can be selected; unavailable engines are not offered (item 17). */
bool ctx_integration_engine_available(CtxEngine engine);
/* Parameter pages of an engine; at least 1. */
uint8_t ctx_integration_page_count(CtxEngine engine);
/* The band the radio last reported. */
CtxBand ctx_integration_current_band(void);
/* The utility service is at its root, so Encoder 1 back leaves the utility. */
bool ctx_integration_utility_at_root(void);
/* The integration clock, in milliseconds, modulo 2^32. */
uint32_t ctx_integration_now_ms(void);
/* Issue a command through the shared command policy. */
void ctx_integration_command(CtxCommand command, int32_t arg);
/* Publish a domain event. */
void ctx_integration_publish(CtxPublished event, int32_t arg);

#endif /* SPOOKY_CONTEXT_PORT_H */
