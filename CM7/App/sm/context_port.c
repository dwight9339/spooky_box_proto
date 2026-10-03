#include "context_port.h"

#include <stddef.h>

#include "ContextSm.h"

/* One machine per region, so one private instance. */
static ContextSm machine;

static CtxConfig config;

/* Model data: published state that owns no transitions (behavior README). */
static CtxPage current_page; /* the operating page that last had the controls */
static CtxPage field_page;   /* the Field page that last had the controls */
static uint8_t pages[CTX_ENGINE_COUNT];
static bool ptt;

/* The open menu. The highlight and the menu kind survive the menu's exit, because a
 * transition leaves the menu before its choice point reads them. */
static CtxMenu menu;
static CtxMenu last_menu;
static uint8_t highlight;
static uint32_t menu_deadline_ms;

static int32_t current_detents;
static CtxStatus counters;

static CtxEngine engine_of(CtxPage page)
{
    return page == CTX_PAGE_CLASSIC ? CTX_ENGINE_CLASSIC : CTX_ENGINE_MANUAL;
}

static bool is_due(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static void dispatch(ContextSm_EventId event)
{
    ContextSm_dispatch_event(&machine, event);
}

/* The machine event for a gesture, or false for a gesture with no binding in any
 * state of this slice. */
static bool event_for(Gesture gesture, ContextSm_EventId *event)
{
    switch (gesture.kind) {
    case GESTURE_CLICK:
        switch (gesture.encoder) {
        case 0: *event = ContextSm_EventId_E0_CLICK; return true;
        case 1: *event = ContextSm_EventId_E1_CLICK; return true;
        case 3: *event = ContextSm_EventId_E3_CLICK; return true;
        default: return false;
        }
    case GESTURE_HOLD:
        switch (gesture.encoder) {
        case 1: *event = ContextSm_EventId_E1_HOLD; return true;
        case 2: *event = ContextSm_EventId_E2_HOLD; return true;
        case 3: *event = ContextSm_EventId_E3_HOLD; return true;
        default: return false;
        }
    case GESTURE_TURN:
        switch (gesture.encoder) {
        case 0: *event = ContextSm_EventId_E0_TURN; return true;
        case 1: *event = ContextSm_EventId_E1_TURN; return true;
        case 2: *event = ContextSm_EventId_E2_TURN; return true;
        case 3: *event = ContextSm_EventId_E3_TURN; return true;
        default: return false;
        }
    case GESTURE_BUTTON1_DOWN:
        *event = ContextSm_EventId_PTT_ON;
        return true;
    case GESTURE_BUTTON1_UP:
        *event = ContextSm_EventId_PTT_OFF;
        return true;
    case GESTURE_SHIFT_ENCODER:
        switch (gesture.encoder) {
        case 0: *event = ContextSm_EventId_SHIFT_E0; return true;
        case 1: *event = ContextSm_EventId_SHIFT_E1; return true;
        default: return false;
        }
    case GESTURE_SHIFT_BUTTON0:
        *event = ContextSm_EventId_SHIFT_B0;
        return true;
    case GESTURE_SHIFT_BUTTON1:
        *event = ContextSm_EventId_SHIFT_B1;
        return true;
    case GESTURE_SHIFT_CHORD:
        *event = ContextSm_EventId_SHIFT_CHORD;
        return true;
    default:
        return false;
    }
}

void Context_Init(void)
{
    config.menu_timeout_ms = CTX_DEFAULT_MENU_TIMEOUT_MS;
    current_page = CTX_PAGE_CLASSIC;
    field_page = CTX_PAGE_CLASSIC;
    for (size_t i = 0; i < CTX_ENGINE_COUNT; ++i) {
        pages[i] = 0u;
    }
    ptt = false;
    menu = CTX_MENU_NONE;
    last_menu = CTX_MENU_NONE;
    highlight = 0u;
    menu_deadline_ms = 0u;
    current_detents = 0;
    counters = (CtxStatus){0};
    ContextSm_ctor(&machine);
    ContextSm_start(&machine);
}

void Context_Configure(const CtxConfig *new_config)
{
    if (new_config != NULL) {
        config = *new_config;
    }
}

void Context_OnGesture(Gesture gesture)
{
    ContextSm_EventId event;
    if (!event_for(gesture, &event)) {
        ++counters.unbound;
        return;
    }
    ++counters.gestures;
    current_detents = gesture.kind == GESTURE_TURN ? gesture.detents : 0;
    dispatch(event);
    current_detents = 0;
}

void Context_OnTick(void)
{
    if (Context_TickDue(ctx_integration_now_ms())) {
        dispatch(ContextSm_EventId_MENU_TIMEOUT);
    }
}

bool Context_TickDue(uint32_t now_ms)
{
    return menu != CTX_MENU_NONE && is_due(now_ms, menu_deadline_ms);
}

void Context_OnSessionChanged(void)
{
    dispatch(ContextSm_EventId_SESSION_CHANGED);
}

void Context_OnReconcile(void)
{
    dispatch(ContextSm_EventId_RECONCILE);
}

void Context_GetStatus(CtxStatus *out)
{
    if (out == NULL) {
        return;
    }
    *out = counters;
    out->menu = (uint8_t)menu;
    out->highlight = highlight;
    out->ptt = ptt ? 1u : 0u;
    for (size_t i = 0; i < CTX_ENGINE_COUNT; ++i) {
        out->page[i] = pages[i];
    }
    switch (machine.state_id) {
    case ContextSm_StateId_MANUALDIRECT:
        out->state = CTX_STATE_MANUAL;
        break;
    case ContextSm_StateId_MANUALQUICKJUMP:
        out->state = CTX_STATE_MANUAL_QUICK_JUMP;
        break;
    case ContextSm_StateId_ENGINEMENU:
        out->state = CTX_STATE_ENGINE_MENU;
        break;
    case ContextSm_StateId_BANDMENU:
        out->state = CTX_STATE_BAND_MENU;
        break;
    case ContextSm_StateId_INSTRUMENT:
        out->state = CTX_STATE_INSTRUMENT;
        break;
    case ContextSm_StateId_UTILITY:
        out->state = CTX_STATE_UTILITY;
        break;
    default:
        out->state = CTX_STATE_CLASSIC;
        break;
    }
}

bool Context_OnPage(void)
{
    switch (machine.state_id) {
    case ContextSm_StateId_CLASSIC:
    case ContextSm_StateId_MANUALDIRECT:
    case ContextSm_StateId_MANUALQUICKJUMP:
    case ContextSm_StateId_INSTRUMENT:
        return true;
    default:
        return false;
    }
}

/* --- Guards called by the diagram ---------------------------------------------- */

bool ctx_session_active(void)
{
    return ctx_integration_session_active();
}

bool ctx_band_change_allowed(void)
{
    return ctx_integration_band_change_allowed();
}

bool ctx_highlight_is_current(void)
{
    if (last_menu == CTX_MENU_BAND) {
        return highlight == (uint8_t)ctx_integration_current_band();
    }
    return highlight == (uint8_t)engine_of(field_page);
}

bool ctx_highlight_is(int item)
{
    return item >= 0 && highlight == (uint8_t)item;
}

bool ctx_field_page_is(CtxPage page)
{
    return field_page == page;
}

bool ctx_current_page_is(CtxPage page)
{
    return current_page == page;
}

bool ctx_utility_at_root(void)
{
    return ctx_integration_utility_at_root();
}

int32_t ctx_detents(void)
{
    return current_detents;
}

/* --- Actions called by the diagram --------------------------------------------- */

void ctx_page_entered(CtxPage page)
{
    current_page = page;
    if (page != CTX_PAGE_INSTRUMENT) {
        field_page = page;
    }
}

void ctx_page_advance(void)
{
    const CtxEngine engine = engine_of(field_page);
    uint8_t count = ctx_integration_page_count(engine);
    if (count == 0u) {
        count = 1u;
    }
    pages[engine] = (uint8_t)((pages[engine] + 1u) % count);
    ctx_integration_publish(CTX_PUB_PAGE_CHANGED, pages[engine]);
}

void ctx_select_engine(CtxEngine engine)
{
    ctx_integration_command(CTX_CMD_SELECT_ENGINE, (int32_t)engine);
    ctx_integration_publish(CTX_PUB_ENGINE_CHANGED, (int32_t)engine);
}

void ctx_select_band(void)
{
    if (!ctx_highlight_is_current()) {
        ctx_integration_command(CTX_CMD_SWITCH_BAND, highlight);
    }
}

void ctx_mode_changed(CtxMode mode)
{
    ctx_integration_command(CTX_CMD_SET_MODE, (int32_t)mode);
    ctx_integration_publish(CTX_PUB_MODE_CHANGED, (int32_t)mode);
}

void ctx_menu_open(CtxMenu kind)
{
    menu = kind;
    last_menu = kind;
    highlight = kind == CTX_MENU_BAND ? (uint8_t)ctx_integration_current_band()
                                      : (uint8_t)engine_of(field_page);
    menu_deadline_ms = ctx_integration_now_ms() + config.menu_timeout_ms;
    ctx_integration_publish(CTX_PUB_MENU_OPENED, (int32_t)kind);
    ctx_integration_publish(CTX_PUB_MENU_HIGHLIGHT, highlight);
}

/* Scrolling clamps at the ends of the list and skips engines that cannot be
 * selected. Any scroll restarts the inactivity timeout. */
void ctx_menu_scroll(void)
{
    menu_deadline_ms = ctx_integration_now_ms() + config.menu_timeout_ms;
    const int32_t step = current_detents > 0 ? 1 : -1;
    int32_t remaining = current_detents > 0 ? current_detents : -current_detents;
    const int32_t count = menu == CTX_MENU_BAND ? (int32_t)CTX_BAND_COUNT : (int32_t)CTX_ENGINE_COUNT;
    int32_t position = highlight;
    while (remaining > 0) {
        int32_t next = position + step;
        while (menu == CTX_MENU_ENGINE && next >= 0 && next < count &&
               !ctx_integration_engine_available((CtxEngine)next)) {
            next += step;
        }
        if (next < 0 || next >= count) {
            break;
        }
        position = next;
        --remaining;
    }
    if ((uint8_t)position != highlight) {
        highlight = (uint8_t)position;
        ctx_integration_publish(CTX_PUB_MENU_HIGHLIGHT, highlight);
    }
}

void ctx_menu_close(void)
{
    const CtxMenu closed = menu;
    menu = CTX_MENU_NONE;
    ctx_integration_publish(CTX_PUB_MENU_CLOSED, (int32_t)closed);
}

void ctx_utility_opened(void)
{
    ctx_integration_command(CTX_CMD_UTILITY_OPEN, 0);
    ctx_integration_publish(CTX_PUB_UTILITY_OPENED, 0);
}

void ctx_utility_closed(void)
{
    ctx_integration_command(CTX_CMD_UTILITY_CLOSE, 0);
    ctx_integration_publish(CTX_PUB_UTILITY_CLOSED, 0);
}

void ctx_ptt_on(void)
{
    if (!ptt) {
        ptt = true;
        ctx_integration_command(CTX_CMD_MONITOR_PTT, 1);
    }
}

void ctx_ptt_off(void)
{
    if (ptt) {
        ptt = false;
        ctx_integration_command(CTX_CMD_MONITOR_PTT, 0);
    }
}

void ctx_reject(CtxAction action)
{
    ctx_integration_publish(CTX_PUB_ACTION_REJECTED, (int32_t)action);
}

void ctx_command(CtxCommand command, int32_t arg)
{
    ctx_integration_command(command, arg);
}

void ctx_publish(CtxPublished event, int32_t arg)
{
    ctx_integration_publish(event, arg);
}
