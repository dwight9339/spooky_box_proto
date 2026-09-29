/*
 * Host tests of InputResolution and Context together, connected through the real M7
 * event queue (decisions 0007 and 0009). Inputs are posted as input events,
 * gestures travel between the machines as internal events, and a rejected input
 * leads to one reconcile event that both machines take. The router here stands in
 * for the firmware routing table, which is wired with the input service
 * (full_spooky_proto-54w.4 and 54w.5). Host results only; not hardware evidence.
 */

#include <stdio.h>
#include <string.h>

#include "context_port.h"
#include "event_queue.h"
#include "input_resolution_port.h"

enum {
    T_RECONCILE = EVQ_TYPE_RECONCILE,
    T_INPUT,
    T_GESTURE,
    T_TICK
};

static EventQueue queue;
static uint32_t now;
static bool session_active;
static unsigned internal_rejections;

/* --- Integration shared by both machines --------------------------------------- */

void inp_integration_emit(Gesture gesture)
{
    if (!EventQueue_Post(&queue, EVQ_CLASS_INTERNAL, T_GESTURE, Gesture_Pack(gesture), 0u, now)) {
        ++internal_rejections;
    }
}
void inp_integration_release_all(void) {}
bool inp_integration_on_page(void) { return Context_OnPage(); }
bool inp_integration_session_active(void) { return session_active; }
uint32_t inp_integration_now_ms(void) { return now; }
void inp_integration_issue(InpCommand command) { (void)command; }
void inp_integration_publish(InpPublished event) { (void)event; }

static unsigned ptt_commands;
static unsigned engine_selections;

bool ctx_integration_session_active(void) { return session_active; }
bool ctx_integration_band_change_allowed(void) { return !session_active; }
bool ctx_integration_engine_available(CtxEngine engine) { (void)engine; return true; }
uint8_t ctx_integration_page_count(CtxEngine engine) { (void)engine; return 1u; }
CtxBand ctx_integration_current_band(void) { return CTX_BAND_FM; }
bool ctx_integration_utility_at_root(void) { return true; }
uint32_t ctx_integration_now_ms(void) { return now; }
void ctx_integration_command(CtxCommand command, int32_t arg)
{
    (void)arg;
    ptt_commands += command == CTX_CMD_MONITOR_PTT ? 1u : 0u;
    engine_selections += command == CTX_CMD_SELECT_ENGINE ? 1u : 0u;
}
void ctx_integration_publish(CtxPublished event, int32_t arg) { (void)event; (void)arg; }

/* --- Router: the static routing table of decision 0007 ------------------------- */

static void route(void *context, const EvqEvent *event)
{
    (void)context;
    switch (event->type) {
    case T_INPUT: {
        const InpInput input = {event->sequence, event->posted_ms, (uint8_t)(event->arg0 & 0xFFu),
                                (uint8_t)((event->arg0 >> 8) & 0xFFu),
                                (int8_t)(uint8_t)((event->arg0 >> 16) & 0xFFu), 0u};
        InputResolution_OnInput(&input);
        break;
    }
    case T_GESTURE:
        Context_OnGesture(Gesture_Unpack(event->arg0));
        break;
    case T_TICK:
        Context_OnTick();
        InputResolution_OnTick();
        break;
    case T_RECONCILE: /* order: Session, Radio, Context, InputResolution */
        Context_OnReconcile();
        InputResolution_OnReconcile();
        break;
    default:
        break;
    }
}

/* One foreground loop pass: post a tick when a threshold is due, then drain. */
static void loop_pass(void)
{
    if (InputResolution_TickDue(now) || Context_TickDue(now)) {
        (void)EventQueue_Post(&queue, EVQ_CLASS_INTERNAL, T_TICK, 0u, 0u, now);
    }
    (void)EventQueue_Service(&queue, now, route, NULL);
}

static void run_for(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 5u) {
        now += 5u;
        loop_pass();
    }
}

static bool post_input(InpInputKind kind, InpControl index, int8_t detents)
{
    const uint32_t arg = (uint32_t)kind | ((uint32_t)index << 8) | ((uint32_t)(uint8_t)detents << 16);
    return EventQueue_Post(&queue, EVQ_CLASS_INPUT, T_INPUT, arg, 0u, now);
}

static void press(InpControl c) { (void)post_input(INP_INPUT_PRESS, c, 0); run_for(20u); }
static void release(InpControl c) { (void)post_input(INP_INPUT_RELEASE, c, 0); run_for(20u); }
static void turn(uint8_t encoder, int8_t detents)
{
    (void)post_input(INP_INPUT_DETENTS, (InpControl)encoder, detents);
    run_for(20u);
}

static void reset(void)
{
    EventQueue_Init(&queue);
    now = 10000u;
    session_active = false;
    internal_rejections = 0;
    ptt_commands = 0;
    engine_selections = 0;
    InputResolution_Init();
    Context_Init();
}

static CtxStatus context(void)
{
    CtxStatus s;
    Context_GetStatus(&s);
    return s;
}

static int failures;
static const char *current_test;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            printf("FAIL %s (line %d): %s\n", current_test, __LINE__, #cond);        \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

/* --- Scenarios ----------------------------------------------------------------- */

static void shift_mode_switch_travels_through_the_queue(void)
{
    reset();
    press(INP_ENC3_BUTTON);
    run_for(INP_DEFAULT_HOLD_MS);
    CHECK(InputResolution_ShiftActive());
    press(INP_BUTTON0);
    release(INP_BUTTON0);
    CHECK(context().state == CTX_STATE_INSTRUMENT);
    release(INP_ENC3_BUTTON);
    CHECK(!InputResolution_ShiftActive());
    CHECK(internal_rejections == 0);
}

static void engine_menu_hold_changes_what_encoder0_means(void)
{
    reset();
    press(INP_ENC2_BUTTON);
    run_for(INP_DEFAULT_HOLD_MS);
    release(INP_ENC2_BUTTON);
    CHECK(context().state == CTX_STATE_ENGINE_MENU);
    turn(0, 1);
    press(INP_ENC0_BUTTON);
    release(INP_ENC0_BUTTON);
    CHECK(context().state == CTX_STATE_MANUAL && engine_selections == 1u);
    /* Shift is unavailable while the menu was open, and available again now. */
    press(INP_ENC3_BUTTON);
    run_for(INP_DEFAULT_HOLD_MS);
    CHECK(InputResolution_ShiftActive());
    release(INP_ENC3_BUTTON);
}

static void a_lost_release_after_overflow_cannot_latch_ptt_or_shift(void)
{
    reset();
    press(INP_BUTTON1);
    CHECK(context().ptt == 1u);
    press(INP_ENC3_BUTTON);
    run_for(INP_DEFAULT_HOLD_MS);
    CHECK(InputResolution_ShiftActive());

    /* A burst of input without a loop pass overflows the queue; the releases of
     * Button 1 and Encoder 3 are among the rejected events. */
    unsigned rejected = 0;
    for (int i = 0; i < 40; ++i) {
        rejected += post_input(INP_INPUT_DETENTS, (InpControl)1, 1) ? 0u : 1u;
    }
    rejected += post_input(INP_INPUT_RELEASE, INP_BUTTON1, 0) ? 0u : 1u;
    rejected += post_input(INP_INPUT_RELEASE, INP_ENC3_BUTTON, 0) ? 0u : 1u;
    CHECK(rejected >= 2u);

    run_for(100u);
    EvqStats stats;
    EventQueue_GetStats(&queue, &stats);
    /* Decision 0007 item 6: a reconcile follows the rejected input. The queue posts
     * one as soon as a reserved slot is free and then clears its flag, so a sustained
     * burst can post several; each is idempotent. */
    CHECK(stats.reconciles >= 1u);
    printf("note: burst of 42 inputs posted %u reconcile event(s)\n", (unsigned)stats.reconciles);
    CHECK(context().ptt == 0u);
    CHECK(!InputResolution_ShiftActive());
    CHECK(ptt_commands == 2u);
    CHECK(internal_rejections == 0);

    /* Controls work normally afterwards. */
    press(INP_BUTTON1);
    CHECK(context().ptt == 1u);
    release(INP_BUTTON1);
    CHECK(context().ptt == 0u);
}

static void random_traffic_never_latches_and_never_rejects_internal_events(void)
{
    uint32_t rng = 4242u;
    for (int run = 0; run < 100; ++run) {
        reset();
        bool down[INP_CONTROL_COUNT] = {false};
        for (int step = 0; step < 400; ++step) {
            rng = rng * 1103515245u + 12345u;
            const uint32_t r = rng >> 8;
            switch (r % 9u) {
            case 0: run_for((r >> 4) % 600u); break;
            case 1: session_active = !session_active; Context_OnSessionChanged(); InputResolution_OnSessionChanged(); break;
            case 2: (void)post_input(INP_INPUT_DETENTS, (InpControl)((r >> 4) % 4u), (int8_t)((int)((r >> 6) % 5u) - 2)); break;
            default: {
                const InpControl c = (InpControl)((r >> 4) % INP_CONTROL_COUNT);
                if (post_input(down[c] ? INP_INPUT_RELEASE : INP_INPUT_PRESS, c, 0)) {
                    down[c] = !down[c];
                }
                if ((r & 0x3u) != 0u) {
                    loop_pass();
                }
                break;
            }
            }
        }
        for (int c = 0; c < (int)INP_CONTROL_COUNT; ++c) {
            if (down[c]) {
                release((InpControl)c);
            }
        }
        run_for(INP_DEFAULT_SESSION_HOLD_MS + CTX_DEFAULT_MENU_TIMEOUT_MS);
        if (context().ptt != 0u || InputResolution_ShiftActive() || internal_rejections != 0) {
            printf("FAIL %s: run %d: ptt %u shift %d internal rejections %u\n", current_test, run,
                   (unsigned)context().ptt, (int)InputResolution_ShiftActive(), internal_rejections);
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
    RUN(shift_mode_switch_travels_through_the_queue);
    RUN(engine_menu_hold_changes_what_encoder0_means);
    RUN(a_lost_release_after_overflow_cannot_latch_ptt_or_shift);
    RUN(random_traffic_never_latches_and_never_rejects_internal_events);
    if (failures != 0) {
        printf("input_context_test: %d failure(s)\n", failures);
        return 1;
    }
    puts("input_context_test: all scenarios passed");
    return 0;
}
