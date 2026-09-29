/*
 * Host scenario tests for the Context machine (docs/design/behavior/ContextSm.puml;
 * decision 0009 items 8-22, 26-41 and decision 0008). The generated machine and
 * the real port run against a fake integration that records every command and
 * published event. Host results only; not hardware evidence.
 */

#include <stdio.h>
#include <string.h>

#include "context_port.h"

/* --- Fake integration ---------------------------------------------------------- */

#define LOG_CAPACITY 128u

typedef struct Entry {
    int code;
    int32_t arg;
} Entry;

typedef struct Fake {
    bool session_active;
    bool band_allowed;
    bool engine_available[CTX_ENGINE_COUNT];
    uint8_t page_count[CTX_ENGINE_COUNT];
    CtxBand band;
    bool utility_at_root;
    uint32_t now;
    unsigned command_count;
    Entry commands[LOG_CAPACITY];
    unsigned published_count;
    Entry published[LOG_CAPACITY];
} Fake;

static Fake fake;

bool ctx_integration_session_active(void) { return fake.session_active; }
bool ctx_integration_band_change_allowed(void) { return fake.band_allowed; }
bool ctx_integration_engine_available(CtxEngine engine) { return fake.engine_available[engine]; }
uint8_t ctx_integration_page_count(CtxEngine engine) { return fake.page_count[engine]; }
CtxBand ctx_integration_current_band(void) { return fake.band; }
bool ctx_integration_utility_at_root(void) { return fake.utility_at_root; }
uint32_t ctx_integration_now_ms(void) { return fake.now; }

void ctx_integration_command(CtxCommand command, int32_t arg)
{
    if (fake.command_count < LOG_CAPACITY) {
        fake.commands[fake.command_count] = (Entry){(int)command, arg};
    }
    ++fake.command_count;
}

void ctx_integration_publish(CtxPublished event, int32_t arg)
{
    if (fake.published_count < LOG_CAPACITY) {
        fake.published[fake.published_count] = (Entry){(int)event, arg};
    }
    ++fake.published_count;
}

/* --- Helpers ------------------------------------------------------------------- */

static int failures;
static const char *current_test;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            printf("FAIL %s (line %d): %s\n", current_test, __LINE__, #cond);        \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

static void reset(void)
{
    memset(&fake, 0, sizeof(fake));
    fake.band_allowed = true;
    fake.engine_available[CTX_ENGINE_CLASSIC] = true;
    fake.engine_available[CTX_ENGINE_MANUAL] = true;
    fake.page_count[CTX_ENGINE_CLASSIC] = 1u;
    fake.page_count[CTX_ENGINE_MANUAL] = 1u;
    fake.band = CTX_BAND_FM;
    fake.utility_at_root = true;
    fake.now = 5000u;
    Context_Init();
}

static void gesture(GestureKind kind, uint8_t encoder, int8_t detents)
{
    const Gesture g = {(uint8_t)kind, encoder, detents, 0u};
    Context_OnGesture(g);
}

static void click(uint8_t encoder) { gesture(GESTURE_CLICK, encoder, 0); }
static void hold(uint8_t encoder) { gesture(GESTURE_HOLD, encoder, 0); }
static void turn(uint8_t encoder, int8_t detents) { gesture(GESTURE_TURN, encoder, detents); }
static void shift_encoder(uint8_t encoder) { gesture(GESTURE_SHIFT_ENCODER, encoder, 0); }

static void advance(uint32_t ms)
{
    fake.now += ms;
    Context_OnTick();
}

static CtxStatus status(void)
{
    CtxStatus s;
    Context_GetStatus(&s);
    return s;
}

static uint8_t state(void) { return status().state; }

static uint8_t page_of(CtxEngine engine)
{
    const CtxStatus s = status();
    return s.page[engine];
}

static bool last_command_is(CtxCommand command, int32_t arg)
{
    return fake.command_count > 0 && fake.commands[fake.command_count - 1].code == (int)command &&
           fake.commands[fake.command_count - 1].arg == arg;
}

static unsigned count_commands(CtxCommand command)
{
    unsigned n = 0;
    for (unsigned i = 0; i < fake.command_count && i < LOG_CAPACITY; ++i) {
        n += fake.commands[i].code == (int)command ? 1u : 0u;
    }
    return n;
}

static bool published(CtxPublished event, int32_t arg)
{
    for (unsigned i = 0; i < fake.published_count && i < LOG_CAPACITY; ++i) {
        if (fake.published[i].code == (int)event && fake.published[i].arg == arg) {
            return true;
        }
    }
    return false;
}

static void clear_logs(void)
{
    fake.command_count = 0;
    fake.published_count = 0;
}

/* --- Boot and page bindings ---------------------------------------------------- */

static void boots_in_field_classic_on_a_page(void)
{
    reset();
    CHECK(state() == CTX_STATE_CLASSIC);
    CHECK(Context_OnPage());
    CHECK(fake.command_count == 0 && fake.published_count == 0);
}

static void classic_page_bindings(void)
{
    reset();
    turn(0, 2);
    CHECK(last_command_is(CTX_CMD_JUMP_RATE, 2));
    turn(1, -3);
    CHECK(last_command_is(CTX_CMD_JUMP_DISTANCE, -3));
    click(0);
    CHECK(last_command_is(CTX_CMD_RUN_PAUSE, 0));
    click(1);
    CHECK(last_command_is(CTX_CMD_TOGGLE_DIRECTION, 0));
    const unsigned before = fake.command_count;
    turn(2, 1);
    turn(3, 1);
    click(2);
    hold(0); /* no hold action: nothing happens (item 3) */
    CHECK(fake.command_count == before);
    CHECK(status().unbound == 4u);
}

static void manual_page_bindings(void)
{
    reset();
    shift_encoder(1);
    CHECK(state() == CTX_STATE_MANUAL_QUICK_JUMP);
    clear_logs();
    turn(0, -1);
    CHECK(last_command_is(CTX_CMD_TUNE, -1));
    click(0);
    CHECK(last_command_is(CTX_CMD_TOGGLE_WRAP, 0));
    const unsigned before = fake.command_count;
    turn(1, 1);
    click(1);
    CHECK(fake.command_count == before);
}

static void page_advance_wraps_after_the_last_page(void)
{
    reset();
    fake.page_count[CTX_ENGINE_CLASSIC] = 3u;
    click(3);
    CHECK(page_of(CTX_ENGINE_CLASSIC) == 1u);
    click(3);
    CHECK(page_of(CTX_ENGINE_CLASSIC) == 2u);
    click(3);
    CHECK(page_of(CTX_ENGINE_CLASSIC) == 0u);
    CHECK(published(CTX_PUB_PAGE_CHANGED, 0));
    /* One page: a click stays on it (item 12). */
    fake.page_count[CTX_ENGINE_CLASSIC] = 1u;
    click(3);
    CHECK(page_of(CTX_ENGINE_CLASSIC) == 0u);
}

static void pages_are_kept_per_engine_across_navigation(void)
{
    reset();
    fake.page_count[CTX_ENGINE_CLASSIC] = 3u;
    fake.page_count[CTX_ENGINE_MANUAL] = 2u;
    click(3);
    click(3);
    shift_encoder(1); /* Manual */
    click(3);
    CHECK(page_of(CTX_ENGINE_MANUAL) == 1u);
    shift_encoder(1); /* back to Classic */
    CHECK(page_of(CTX_ENGINE_CLASSIC) == 2u);
    shift_encoder(0); /* utility and back */
    hold(3);
    CHECK(state() == CTX_STATE_CLASSIC && page_of(CTX_ENGINE_CLASSIC) == 2u);
}

/* --- Manual quick-jump (items 26 to 29) ---------------------------------------- */

static void manual_quick_jump_and_return(void)
{
    reset();
    shift_encoder(1);
    CHECK(state() == CTX_STATE_MANUAL_QUICK_JUMP);
    CHECK(last_command_is(CTX_CMD_SELECT_ENGINE, CTX_ENGINE_MANUAL));
    CHECK(published(CTX_PUB_ENGINE_CHANGED, CTX_ENGINE_MANUAL));
    shift_encoder(1);
    CHECK(state() == CTX_STATE_CLASSIC);
    CHECK(last_command_is(CTX_CMD_SELECT_ENGINE, CTX_ENGINE_CLASSIC));
}

static void manual_chosen_from_the_menu_has_no_return(void)
{
    reset();
    hold(2);
    turn(0, 1);
    click(0);
    CHECK(state() == CTX_STATE_MANUAL);
    clear_logs();
    shift_encoder(1);
    CHECK(state() == CTX_STATE_MANUAL && fake.command_count == 0);
}

static void choosing_an_engine_clears_the_return_slot(void)
{
    reset();
    shift_encoder(1); /* Manual by quick-jump */
    hold(2);
    turn(0, -1);
    click(0); /* Classic from the menu */
    CHECK(state() == CTX_STATE_CLASSIC);
    hold(2);
    turn(0, 1);
    click(0); /* Manual from the menu */
    CHECK(state() == CTX_STATE_MANUAL);
    clear_logs();
    shift_encoder(1);
    CHECK(state() == CTX_STATE_MANUAL && fake.command_count == 0);
}

static void closing_the_engine_menu_keeps_the_return_slot(void)
{
    reset();
    shift_encoder(1);
    hold(2);
    click(1); /* close */
    CHECK(state() == CTX_STATE_MANUAL_QUICK_JUMP);
    hold(2);
    click(0); /* select the current engine */
    CHECK(state() == CTX_STATE_MANUAL_QUICK_JUMP);
    CHECK(count_commands(CTX_CMD_SELECT_ENGINE) == 1u);
    shift_encoder(1);
    CHECK(state() == CTX_STATE_CLASSIC);
}

/* --- Engine and band menus (items 16 to 22) ------------------------------------ */

static void engine_menu_opens_on_hold_and_selects_on_click(void)
{
    reset();
    hold(2);
    CHECK(state() == CTX_STATE_ENGINE_MENU && !Context_OnPage());
    CHECK(published(CTX_PUB_MENU_OPENED, CTX_MENU_ENGINE));
    CHECK(status().highlight == CTX_ENGINE_CLASSIC);
    turn(0, 1);
    CHECK(status().highlight == CTX_ENGINE_MANUAL);
    turn(0, 5); /* clamps at the end */
    CHECK(status().highlight == CTX_ENGINE_MANUAL);
    click(0);
    CHECK(state() == CTX_STATE_MANUAL);
    CHECK(last_command_is(CTX_CMD_SELECT_ENGINE, CTX_ENGINE_MANUAL));
    CHECK(published(CTX_PUB_MENU_CLOSED, CTX_MENU_ENGINE));
    CHECK(status().menu == CTX_MENU_NONE);
}

static void unavailable_engines_are_not_offered(void)
{
    reset();
    fake.engine_available[CTX_ENGINE_MANUAL] = false;
    hold(2);
    turn(0, 1);
    CHECK(status().highlight == CTX_ENGINE_CLASSIC);
    click(0);
    CHECK(state() == CTX_STATE_CLASSIC && count_commands(CTX_CMD_SELECT_ENGINE) == 0u);
}

static void encoder1_click_closes_a_menu_without_a_change(void)
{
    reset();
    hold(2);
    turn(0, 1);
    click(1);
    CHECK(state() == CTX_STATE_CLASSIC);
    CHECK(count_commands(CTX_CMD_SELECT_ENGINE) == 0u);
    hold(1);
    turn(0, 2);
    click(1);
    CHECK(state() == CTX_STATE_CLASSIC && count_commands(CTX_CMD_SWITCH_BAND) == 0u);
}

static void menus_close_after_the_inactivity_timeout(void)
{
    reset();
    hold(1);
    advance(CTX_DEFAULT_MENU_TIMEOUT_MS - 1u);
    CHECK(state() == CTX_STATE_BAND_MENU);
    turn(0, 1); /* restarts the timeout */
    advance(CTX_DEFAULT_MENU_TIMEOUT_MS - 1u);
    CHECK(state() == CTX_STATE_BAND_MENU);
    advance(1u);
    CHECK(state() == CTX_STATE_CLASSIC);
    CHECK(count_commands(CTX_CMD_SWITCH_BAND) == 0u);
    CHECK(!Context_TickDue(fake.now + 100000u));
}

static void band_menu_switches_to_the_highlighted_band(void)
{
    reset();
    fake.band = CTX_BAND_AM;
    shift_encoder(1); /* Manual */
    hold(1);
    CHECK(state() == CTX_STATE_BAND_MENU);
    CHECK(status().highlight == CTX_BAND_AM);
    turn(0, 1);
    click(0);
    CHECK(last_command_is(CTX_CMD_SWITCH_BAND, CTX_BAND_SW));
    CHECK(state() == CTX_STATE_MANUAL_QUICK_JUMP);
    /* The current band just closes the menu. */
    hold(1);
    click(0);
    CHECK(count_commands(CTX_CMD_SWITCH_BAND) == 1u);
    CHECK(state() == CTX_STATE_MANUAL_QUICK_JUMP);
}

static void a_menu_ignores_page_and_shift_gestures(void)
{
    reset();
    hold(2);
    clear_logs();
    turn(1, 1);
    click(3);
    hold(1);
    hold(3);
    gesture(GESTURE_SHIFT_BUTTON0, 0, 0);
    gesture(GESTURE_SHIFT_CHORD, 0, 0);
    shift_encoder(0);
    CHECK(state() == CTX_STATE_ENGINE_MENU);
    CHECK(fake.command_count == 0);
    /* PTT still works in a menu (item 18). */
    gesture(GESTURE_BUTTON1_DOWN, 0, 0);
    CHECK(last_command_is(CTX_CMD_MONITOR_PTT, 1));
    gesture(GESTURE_BUTTON1_UP, 0, 0);
    CHECK(last_command_is(CTX_CMD_MONITOR_PTT, 0));
}

static void band_menu_is_rejected_while_band_changes_are_blocked(void)
{
    reset();
    fake.session_active = true;
    fake.band_allowed = false;
    hold(1);
    CHECK(state() == CTX_STATE_CLASSIC);
    CHECK(published(CTX_PUB_ACTION_REJECTED, CTX_ACTION_BAND_CHANGE));
    /* The engine menu stays available during a session (decision 0008). */
    hold(2);
    CHECK(state() == CTX_STATE_ENGINE_MENU);
}

static void a_session_start_withdraws_the_band_menu(void)
{
    reset();
    hold(1);
    fake.session_active = true;
    fake.band_allowed = false;
    Context_OnSessionChanged();
    CHECK(state() == CTX_STATE_CLASSIC);
    CHECK(published(CTX_PUB_MENU_WITHDRAWN, CTX_MENU_BAND));
    CHECK(count_commands(CTX_CMD_SWITCH_BAND) == 0u);
}

/* --- Shift actions and mode (items 30 to 36, decision 0008) -------------------- */

static void mode_switch_and_return_restore_the_field_page(void)
{
    reset();
    shift_encoder(1); /* Manual by quick-jump */
    gesture(GESTURE_SHIFT_BUTTON0, 0, 0);
    CHECK(state() == CTX_STATE_INSTRUMENT);
    CHECK(last_command_is(CTX_CMD_SET_MODE, CTX_MODE_INSTRUMENT));
    CHECK(published(CTX_PUB_MODE_CHANGED, CTX_MODE_INSTRUMENT));
    gesture(GESTURE_SHIFT_BUTTON0, 0, 0);
    CHECK(state() == CTX_STATE_MANUAL_QUICK_JUMP);
    CHECK(last_command_is(CTX_CMD_SET_MODE, CTX_MODE_FIELD));
}

static void mode_switch_is_rejected_during_a_session(void)
{
    reset();
    fake.session_active = true;
    gesture(GESTURE_SHIFT_BUTTON0, 0, 0);
    CHECK(state() == CTX_STATE_CLASSIC);
    CHECK(published(CTX_PUB_ACTION_REJECTED, CTX_ACTION_MODE_SWITCH));
    CHECK(count_commands(CTX_CMD_SET_MODE) == 0u);
}

static void capture_save_and_chord(void)
{
    reset();
    gesture(GESTURE_SHIFT_BUTTON1, 0, 0);
    CHECK(last_command_is(CTX_CMD_CAPTURE_SAVE, 0) && state() == CTX_STATE_CLASSIC);
    clear_logs();
    gesture(GESTURE_SHIFT_CHORD, 0, 0);
    CHECK(fake.commands[0].code == CTX_CMD_CAPTURE_SAVE);
    CHECK(fake.commands[1].code == CTX_CMD_LOAD_CAPTURE);
    CHECK(state() == CTX_STATE_INSTRUMENT);
}

static void chord_during_a_session_saves_and_rejects_the_switch(void)
{
    reset();
    fake.session_active = true;
    gesture(GESTURE_SHIFT_CHORD, 0, 0);
    CHECK(count_commands(CTX_CMD_CAPTURE_SAVE) == 1u);
    CHECK(count_commands(CTX_CMD_LOAD_CAPTURE) == 0u);
    CHECK(published(CTX_PUB_ACTION_REJECTED, CTX_ACTION_MODE_SWITCH));
    CHECK(state() == CTX_STATE_CLASSIC);
}

static void quick_jump_is_allowed_during_a_session(void)
{
    reset();
    fake.session_active = true;
    shift_encoder(1);
    CHECK(state() == CTX_STATE_MANUAL_QUICK_JUMP);
    hold(2);
    CHECK(state() == CTX_STATE_ENGINE_MENU);
}

/* --- Utility (items 24, 31, 38) ------------------------------------------------ */

static void utility_opens_and_restores_the_page_on_encoder3_hold(void)
{
    reset();
    shift_encoder(1);
    shift_encoder(0);
    CHECK(state() == CTX_STATE_UTILITY && !Context_OnPage());
    CHECK(last_command_is(CTX_CMD_UTILITY_OPEN, 0));
    turn(0, 2);
    CHECK(last_command_is(CTX_CMD_UTILITY_SCROLL, 2));
    click(0);
    CHECK(last_command_is(CTX_CMD_UTILITY_SELECT, 0));
    click(3); /* no action in utilities */
    CHECK(state() == CTX_STATE_UTILITY);
    hold(3);
    CHECK(state() == CTX_STATE_MANUAL_QUICK_JUMP);
    CHECK(last_command_is(CTX_CMD_UTILITY_CLOSE, 0));
}

static void utility_back_leaves_only_from_the_root(void)
{
    reset();
    shift_encoder(0);
    fake.utility_at_root = false;
    click(1);
    CHECK(state() == CTX_STATE_UTILITY && last_command_is(CTX_CMD_UTILITY_BACK, 0));
    fake.utility_at_root = true;
    click(1);
    CHECK(state() == CTX_STATE_CLASSIC);
}

static void utility_from_instrument_returns_to_instrument(void)
{
    reset();
    gesture(GESTURE_SHIFT_BUTTON0, 0, 0);
    shift_encoder(0);
    CHECK(state() == CTX_STATE_UTILITY);
    hold(3);
    CHECK(state() == CTX_STATE_INSTRUMENT);
    gesture(GESTURE_SHIFT_BUTTON0, 0, 0);
    CHECK(state() == CTX_STATE_CLASSIC);
}

static void ptt_is_ignored_in_a_utility_but_always_ends(void)
{
    reset();
    gesture(GESTURE_BUTTON1_DOWN, 0, 0);
    CHECK(status().ptt == 1u);
    shift_encoder(0); /* PTT still held while the utility opens */
    gesture(GESTURE_BUTTON1_UP, 0, 0);
    CHECK(status().ptt == 0u && last_command_is(CTX_CMD_MONITOR_PTT, 0));
    gesture(GESTURE_BUTTON1_DOWN, 0, 0);
    CHECK(status().ptt == 0u);
}

/* --- Reconciliation ------------------------------------------------------------ */

static void reconcile_ends_ptt_and_closes_menus_without_committing(void)
{
    reset();
    gesture(GESTURE_BUTTON1_DOWN, 0, 0);
    hold(2);
    turn(0, 1);
    Context_OnReconcile();
    CHECK(state() == CTX_STATE_CLASSIC);
    CHECK(status().ptt == 0u);
    CHECK(count_commands(CTX_CMD_SELECT_ENGINE) == 0u);
    /* On a page, reconciliation changes only PTT. */
    shift_encoder(1);
    gesture(GESTURE_BUTTON1_DOWN, 0, 0);
    Context_OnReconcile();
    CHECK(state() == CTX_STATE_MANUAL_QUICK_JUMP && status().ptt == 0u);
}

static void random_gestures_keep_the_invariants(void)
{
    /* Drive every gesture from every state: menu state always matches the machine
     * state, and no mode change or band change is commanded while a session is
     * active (decision 0008). */
    static const GestureKind kinds[] = {
        GESTURE_CLICK, GESTURE_HOLD, GESTURE_TURN, GESTURE_BUTTON1_DOWN, GESTURE_BUTTON1_UP,
        GESTURE_SHIFT_ENCODER, GESTURE_SHIFT_BUTTON0, GESTURE_SHIFT_BUTTON1, GESTURE_SHIFT_CHORD};
    uint32_t rng = 777u;
    reset();
    for (int step = 0; step < 20000; ++step) {
        rng = rng * 1103515245u + 12345u;
        const uint32_t r = rng >> 8;
        clear_logs();
        switch (r % 12u) {
        case 0:
            fake.session_active = !fake.session_active;
            fake.band_allowed = !fake.session_active;
            Context_OnSessionChanged();
            break;
        case 1: advance((r >> 4) % 6000u); break;
        case 2: if (r % 31u == 0u) { Context_OnReconcile(); } break;
        case 3: fake.utility_at_root = (r & 0x10u) != 0u; break;
        default:
            gesture(kinds[(r >> 4) % (sizeof(kinds) / sizeof(kinds[0]))], (uint8_t)((r >> 8) % 4u),
                    (int8_t)((int)((r >> 10) % 5u) - 2));
            break;
        }
        const CtxStatus s = status();
        const bool in_menu = s.state == CTX_STATE_ENGINE_MENU || s.state == CTX_STATE_BAND_MENU;
        if (s.state > CTX_STATE_UTILITY || (s.menu != CTX_MENU_NONE) != in_menu ||
            Context_OnPage() == (in_menu || s.state == CTX_STATE_UTILITY)) {
            printf("FAIL %s: step %d: state %u with menu %u\n", current_test, step,
                   (unsigned)s.state, (unsigned)s.menu);
            ++failures;
            return;
        }
        if (fake.session_active &&
            (count_commands(CTX_CMD_SET_MODE) != 0u || count_commands(CTX_CMD_SWITCH_BAND) != 0u ||
             count_commands(CTX_CMD_LOAD_CAPTURE) != 0u)) {
            printf("FAIL %s: step %d: mode or band change during a session\n", current_test, step);
            ++failures;
            return;
        }
    }
}

#define RUN(test)              \
    do {                       \
        current_test = #test;  \
        test();                \
    } while (0)

int main(void)
{
    RUN(boots_in_field_classic_on_a_page);
    RUN(classic_page_bindings);
    RUN(manual_page_bindings);
    RUN(page_advance_wraps_after_the_last_page);
    RUN(pages_are_kept_per_engine_across_navigation);
    RUN(manual_quick_jump_and_return);
    RUN(manual_chosen_from_the_menu_has_no_return);
    RUN(choosing_an_engine_clears_the_return_slot);
    RUN(closing_the_engine_menu_keeps_the_return_slot);
    RUN(engine_menu_opens_on_hold_and_selects_on_click);
    RUN(unavailable_engines_are_not_offered);
    RUN(encoder1_click_closes_a_menu_without_a_change);
    RUN(menus_close_after_the_inactivity_timeout);
    RUN(band_menu_switches_to_the_highlighted_band);
    RUN(a_menu_ignores_page_and_shift_gestures);
    RUN(band_menu_is_rejected_while_band_changes_are_blocked);
    RUN(a_session_start_withdraws_the_band_menu);
    RUN(mode_switch_and_return_restore_the_field_page);
    RUN(mode_switch_is_rejected_during_a_session);
    RUN(capture_save_and_chord);
    RUN(chord_during_a_session_saves_and_rejects_the_switch);
    RUN(quick_jump_is_allowed_during_a_session);
    RUN(utility_opens_and_restores_the_page_on_encoder3_hold);
    RUN(utility_back_leaves_only_from_the_root);
    RUN(utility_from_instrument_returns_to_instrument);
    RUN(ptt_is_ignored_in_a_utility_but_always_ends);
    RUN(reconcile_ends_ptt_and_closes_menus_without_committing);
    RUN(random_gestures_keep_the_invariants);
    if (failures != 0) {
        printf("context_test: %d failure(s)\n", failures);
        return 1;
    }
    puts("context_test: all scenarios passed");
    return 0;
}
