/*
 * Host tests for the M7 application event queue (decision 0007): ordering,
 * admission by class, overrun, reconcile after rejected input, bounded dispatch,
 * run-to-completion and counters. Host results only; not hardware evidence.
 */

#include <stdio.h>
#include <string.h>

#include "event_queue.h"

static int failures;
static const char *current_test;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            printf("FAIL %s (line %d): %s\n", current_test, __LINE__, #cond);        \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

#define LOG_CAPACITY 256u

typedef struct Recorder {
    EventQueue *queue;
    unsigned count;
    EvqEvent events[LOG_CAPACITY];
    /* Optional re-post from inside dispatch, to test run-to-completion. */
    unsigned repost_per_event;
    unsigned repost_limit; /* how many handled events repost; 0 means all */
    unsigned reposting_events;
    uint16_t repost_type;
    bool nested_seen_by_this_call;
    unsigned depth;
    unsigned max_depth;
} Recorder;

static void record(void *context, const EvqEvent *event)
{
    Recorder *r = context;
    ++r->depth;
    if (r->depth > r->max_depth) {
        r->max_depth = r->depth;
    }
    if (r->count < LOG_CAPACITY) {
        r->events[r->count] = *event;
    }
    ++r->count;
    const bool repost = r->repost_limit == 0 || r->reposting_events < r->repost_limit;
    if (repost && r->repost_per_event > 0) {
        ++r->reposting_events;
    }
    for (unsigned i = 0; repost && i < r->repost_per_event; ++i) {
        (void)EventQueue_Post(r->queue, EVQ_CLASS_INTERNAL, r->repost_type, event->sequence, i,
                              event->posted_ms);
    }
    --r->depth;
}

static EvqStats stats(const EventQueue *q)
{
    EvqStats s;
    EventQueue_GetStats(q, &s);
    return s;
}

static void events_are_dispatched_in_post_order_with_sequence_and_time(void)
{
    EventQueue q;
    EventQueue_Init(&q);
    Recorder r = {.queue = &q};
    CHECK(EventQueue_Post(&q, EVQ_CLASS_INPUT, 11, 1, 2, 100));
    CHECK(EventQueue_Post(&q, EVQ_CLASS_EXTERNAL_COMMAND, 22, 3, 4, 101));
    CHECK(EventQueue_Post(&q, EVQ_CLASS_INTERNAL, 33, 5, 6, 102));
    CHECK(EventQueue_Service(&q, 110, record, &r) == 3);
    CHECK(r.count == 3);
    CHECK(r.events[0].type == 11 && r.events[1].type == 22 && r.events[2].type == 33);
    CHECK(r.events[0].event_class == EVQ_CLASS_INPUT);
    CHECK(r.events[1].event_class == EVQ_CLASS_EXTERNAL_COMMAND);
    CHECK(r.events[2].event_class == EVQ_CLASS_INTERNAL);
    CHECK(r.events[0].sequence + 1 == r.events[1].sequence);
    CHECK(r.events[1].sequence + 1 == r.events[2].sequence);
    CHECK(r.events[2].arg0 == 5 && r.events[2].arg1 == 6 && r.events[2].posted_ms == 102);
    const EvqStats s = stats(&q);
    CHECK(s.count == 0 && s.posted == 3 && s.dispatched == 3 && s.high_water == 3);
    CHECK(s.max_wait_ms == 10);
}

static void a_zero_initialized_queue_is_empty_and_usable(void)
{
    static EventQueue q; /* static storage: zero-initialized, as in firmware */
    Recorder r = {.queue = &q};
    CHECK(EventQueue_Service(&q, 0, record, &r) == 0);
    CHECK(EventQueue_Post(&q, EVQ_CLASS_INPUT, 1, 0, 0, 0));
    CHECK(EventQueue_Service(&q, 0, record, &r) == 1);
    const EvqStats s = stats(&q);
    CHECK(s.capacity == EVENT_QUEUE_CAPACITY && s.reserve == EVENT_QUEUE_RESERVE);
}

static void input_and_commands_leave_the_reserve_for_internal_events(void)
{
    EventQueue q;
    EventQueue_Init(&q);
    const unsigned open = EVENT_QUEUE_CAPACITY - EVENT_QUEUE_RESERVE;
    for (unsigned i = 0; i < open - 1; ++i) {
        CHECK(EventQueue_Post(&q, EVQ_CLASS_INPUT, 1, i, 0, 0));
    }
    CHECK(EventQueue_Post(&q, EVQ_CLASS_EXTERNAL_COMMAND, 2, 0, 0, 0));
    /* The reserve is now all that is free. */
    CHECK(!EventQueue_Post(&q, EVQ_CLASS_EXTERNAL_COMMAND, 2, 1, 0, 0));
    for (unsigned i = 0; i < EVENT_QUEUE_RESERVE; ++i) {
        CHECK(EventQueue_Post(&q, EVQ_CLASS_INTERNAL, 3, i, 0, 0));
    }
    CHECK(!EventQueue_Post(&q, EVQ_CLASS_INTERNAL, 3, 99, 0, 0));
    const EvqStats s = stats(&q);
    CHECK(s.count == EVENT_QUEUE_CAPACITY && s.high_water == EVENT_QUEUE_CAPACITY);
    CHECK(s.rejected[EVQ_CLASS_EXTERNAL_COMMAND] == 1);
    CHECK(s.rejected[EVQ_CLASS_INTERNAL] == 1);
    CHECK(s.rejected[EVQ_CLASS_INPUT] == 0);
}

static void overrun_rejects_the_newest_and_keeps_queued_events(void)
{
    EventQueue q;
    EventQueue_Init(&q);
    Recorder r = {.queue = &q};
    for (unsigned i = 0; i < EVENT_QUEUE_CAPACITY; ++i) {
        CHECK(EventQueue_Post(&q, EVQ_CLASS_INTERNAL, 5, i, 0, 0));
    }
    CHECK(!EventQueue_Post(&q, EVQ_CLASS_INTERNAL, 6, 0, 0, 0));
    CHECK(EventQueue_Service(&q, 0, record, &r) == EVENT_QUEUE_CAPACITY);
    bool in_order = true;
    for (unsigned i = 0; i < EVENT_QUEUE_CAPACITY; ++i) {
        in_order = in_order && r.events[i].type == 5 && r.events[i].arg0 == i;
    }
    CHECK(in_order);
}

static void rejected_input_is_followed_by_one_reconcile_before_more_input(void)
{
    EventQueue q;
    EventQueue_Init(&q);
    Recorder r = {.queue = &q};
    const unsigned open = EVENT_QUEUE_CAPACITY - EVENT_QUEUE_RESERVE;
    for (unsigned i = 0; i < open; ++i) {
        CHECK(EventQueue_Post(&q, EVQ_CLASS_INPUT, 1, i, 0, 0));
    }
    CHECK(!EventQueue_Post(&q, EVQ_CLASS_INPUT, 1, 100, 0, 0)); /* lost */
    CHECK(stats(&q).reconcile_pending);
    /* The reserve has room, so the next call posts the reconcile first, behind the
     * events already queued, and dispatches it in the same call. */
    CHECK(EventQueue_Service(&q, 1, record, &r) == open + 1);
    CHECK(r.events[0].type == 1 && r.events[0].arg0 == 0);
    CHECK(r.events[open - 1].type == 1 && r.events[open - 1].arg0 == open - 1);
    CHECK(r.events[open].type == EVQ_TYPE_RECONCILE);
    CHECK(r.events[open].event_class == EVQ_CLASS_INTERNAL);
    EvqStats s = stats(&q);
    CHECK(!s.reconcile_pending && s.reconciles == 1 && s.count == 0);
    /* Input is admitted again after the reconcile. */
    CHECK(EventQueue_Post(&q, EVQ_CLASS_INPUT, 1, 200, 0, 2));
    s = stats(&q);
    CHECK(s.rejected[EVQ_CLASS_INPUT] == 1 && s.rejected[EVQ_CLASS_INTERNAL] == 0);
    CHECK(s.reconciles == 1);
}

static void input_stays_rejected_until_the_reconcile_is_posted(void)
{
    EventQueue q;
    EventQueue_Init(&q);
    Recorder r = {.queue = &q};
    /* Fill every slot with internal events so not even the reconcile fits. */
    for (unsigned i = 0; i < EVENT_QUEUE_CAPACITY; ++i) {
        CHECK(EventQueue_Post(&q, EVQ_CLASS_INTERNAL, 7, i, 0, 0));
    }
    CHECK(!EventQueue_Post(&q, EVQ_CLASS_INPUT, 1, 0, 0, 0));
    CHECK(!EventQueue_Post(&q, EVQ_CLASS_INPUT, 1, 1, 0, 0));
    EvqStats s = stats(&q);
    CHECK(s.reconcile_pending && s.reconciles == 0 && s.rejected[EVQ_CLASS_INPUT] == 2);
    /* Dispatch frees every slot; the reconcile is posted at the end of the call and
     * dispatched in the next one. Only one reconcile for the whole loss. */
    CHECK(EventQueue_Service(&q, 5, record, &r) == EVENT_QUEUE_CAPACITY);
    s = stats(&q);
    CHECK(!s.reconcile_pending && s.reconciles == 1 && s.count == 1);
    r.count = 0;
    CHECK(EventQueue_Service(&q, 6, record, &r) == 1 && r.events[0].type == EVQ_TYPE_RECONCILE);
}

static void a_sustained_burst_posts_one_reconcile_and_keeps_the_reserve(void)
{
    /* full_spooky_proto-8lw.17: without a service call, each rejected input used to
     * add another reconcile until the internal reserve was full. */
    EventQueue q;
    EventQueue_Init(&q);
    const unsigned open = EVENT_QUEUE_CAPACITY - EVENT_QUEUE_RESERVE;
    unsigned admitted = 0;
    for (unsigned i = 0; i < open + 40u; ++i) {
        admitted += EventQueue_Post(&q, EVQ_CLASS_INPUT, 1, i, 0, 0) ? 1u : 0u;
    }
    EvqStats s = stats(&q);
    CHECK(admitted == open);
    CHECK(s.reconciles == 1 && !s.reconcile_pending);
    CHECK(s.rejected[EVQ_CLASS_INPUT] == 40u);
    /* The rest of the reserve is still free for internal events. */
    for (unsigned i = 0; i < EVENT_QUEUE_RESERVE - 1u; ++i) {
        CHECK(EventQueue_Post(&q, EVQ_CLASS_INTERNAL, 9, i, 0, 0));
    }
    s = stats(&q);
    CHECK(s.rejected[EVQ_CLASS_INTERNAL] == 0);
    /* The reconcile is dispatched after every lost input, then input flows again. */
    Recorder r = {.queue = &q};
    CHECK(EventQueue_Service(&q, 1, record, &r) == EVENT_QUEUE_CAPACITY);
    CHECK(r.events[open].type == EVQ_TYPE_RECONCILE);
    CHECK(EventQueue_Post(&q, EVQ_CLASS_INPUT, 1, 500, 0, 2));
    CHECK(stats(&q).reconciles == 1);
}

static void input_lost_after_the_reconcile_is_dispatched_gets_a_new_one(void)
{
    /* A reconcile covers only losses before its dispatch. */
    EventQueue q;
    EventQueue_Init(&q);
    const unsigned open = EVENT_QUEUE_CAPACITY - EVENT_QUEUE_RESERVE;
    for (unsigned i = 0; i <= open; ++i) {
        (void)EventQueue_Post(&q, EVQ_CLASS_INPUT, 1, i, 0, 0);
    }
    (void)EventQueue_Post(&q, EVQ_CLASS_INTERNAL, 9, 0, 0, 0); /* posts the reconcile */
    Recorder r = {.queue = &q};
    (void)EventQueue_Service(&q, 1, record, &r);
    CHECK(stats(&q).reconciles == 1 && stats(&q).count == 0);
    for (unsigned i = 0; i <= open; ++i) {
        (void)EventQueue_Post(&q, EVQ_CLASS_INPUT, 1, i, 0, 2);
    }
    CHECK(stats(&q).reconcile_pending);
    (void)EventQueue_Service(&q, 3, record, &r);
    CHECK(stats(&q).reconciles == 2);
}

static void a_loss_after_input_admitted_behind_a_reconcile_gets_a_new_one(void)
{
    /* full_spooky_proto-8lw.18: a reconcile left queued at the end of a service
     * call, then a press admitted behind it, then its release lost. The press is
     * dispatched after the first reconcile, so a second one must follow it. */
    EventQueue q;
    EventQueue_Init(&q);
    Recorder r = {.queue = &q};
    for (unsigned i = 0; i < EVENT_QUEUE_CAPACITY; ++i) {
        CHECK(EventQueue_Post(&q, EVQ_CLASS_INTERNAL, 7, i, 0, 0));
    }
    CHECK(!EventQueue_Post(&q, EVQ_CLASS_INPUT, 1, 0, 0, 0));
    (void)EventQueue_Service(&q, 1, record, &r); /* the reconcile is posted at the end */
    CHECK(stats(&q).count == 1 && stats(&q).reconciles == 1);
    CHECK(EventQueue_Post(&q, EVQ_CLASS_INPUT, 2, 0, 0, 2)); /* the press */
    unsigned detents = 0;
    while (EventQueue_Post(&q, EVQ_CLASS_INPUT, 3, detents, 0, 2)) {
        ++detents;
    }
    CHECK(stats(&q).reconcile_pending); /* the rejected detent is not covered */
    CHECK(!EventQueue_Post(&q, EVQ_CLASS_INPUT, 4, 0, 0, 2)); /* the release, lost */
    r.count = 0;
    (void)EventQueue_Service(&q, 3, record, &r);
    CHECK(stats(&q).reconciles == 2);
    unsigned press_at = 0;
    unsigned last_reconcile_at = 0;
    for (unsigned i = 0; i < r.count && i < LOG_CAPACITY; ++i) {
        if (r.events[i].type == 2) {
            press_at = i;
        }
        if (r.events[i].type == EVQ_TYPE_RECONCILE) {
            last_reconcile_at = i;
        }
    }
    CHECK(last_reconcile_at > press_at);
}

static void a_reconcile_is_posted_on_the_next_post_of_any_class(void)
{
    EventQueue q;
    EventQueue_Init(&q);
    for (unsigned i = 0; i < EVENT_QUEUE_CAPACITY - EVENT_QUEUE_RESERVE; ++i) {
        CHECK(EventQueue_Post(&q, EVQ_CLASS_INPUT, 1, i, 0, 0));
    }
    CHECK(!EventQueue_Post(&q, EVQ_CLASS_INPUT, 1, 0, 0, 0));
    /* The reserve is free, so an internal post first posts the reconcile. */
    CHECK(EventQueue_Post(&q, EVQ_CLASS_INTERNAL, 9, 0, 0, 0));
    Recorder r = {.queue = &q};
    CHECK(EventQueue_Service(&q, 0, record, &r) == EVENT_QUEUE_CAPACITY - EVENT_QUEUE_RESERVE + 2);
    const unsigned n = r.count;
    CHECK(r.events[n - 2].type == EVQ_TYPE_RECONCILE && r.events[n - 1].type == 9);
}

static void dispatch_handles_only_events_queued_when_it_started(void)
{
    EventQueue q;
    EventQueue_Init(&q);
    Recorder r = {.queue = &q, .repost_per_event = 2, .repost_type = 50};
    CHECK(EventQueue_Post(&q, EVQ_CLASS_INPUT, 1, 0, 0, 0));
    CHECK(EventQueue_Post(&q, EVQ_CLASS_INPUT, 2, 0, 0, 0));
    CHECK(EventQueue_Service(&q, 0, record, &r) == 2);
    CHECK(r.count == 2);
    /* Posts from dispatch wait for the next call, in order. */
    EvqStats s = stats(&q);
    CHECK(s.count == 4);
    r.repost_per_event = 0;
    CHECK(EventQueue_Service(&q, 0, record, &r) == 4);
    CHECK(r.events[2].type == 50 && r.events[2].arg0 == r.events[0].sequence && r.events[2].arg1 == 0);
    CHECK(r.events[3].type == 50 && r.events[3].arg0 == r.events[0].sequence && r.events[3].arg1 == 1);
    CHECK(r.events[4].arg0 == r.events[1].sequence);
}

static void dispatch_is_run_to_completion(void)
{
    /* A chain where every event posts another never re-enters dispatch and never
     * runs away: one event per call. */
    EventQueue q;
    EventQueue_Init(&q);
    Recorder r = {.queue = &q, .repost_per_event = 1, .repost_type = 60};
    CHECK(EventQueue_Post(&q, EVQ_CLASS_INTERNAL, 60, 0, 0, 0));
    for (unsigned i = 0; i < 10; ++i) {
        CHECK(EventQueue_Service(&q, i, record, &r) == 1);
    }
    CHECK(r.max_depth == 1 && r.count == 10 && stats(&q).count == 1);
}

static void the_reserve_absorbs_internal_posts_during_a_full_dispatch(void)
{
    /* Decision 0007 item 13 sizes the reserve for four events per call that each
     * post two internal events. An event leaves the queue before it is handled, so
     * each such event uses one net slot: the reserve of 8 absorbs 8 of them with
     * input and commands filling their whole share, and the 9th loses an event. */
    for (unsigned reposting = EVENT_QUEUE_RESERVE; reposting <= EVENT_QUEUE_RESERVE + 1;
         ++reposting) {
        EventQueue q;
        EventQueue_Init(&q);
        Recorder r = {.queue = &q, .repost_per_event = 2, .repost_limit = reposting,
                      .repost_type = 70};
        for (unsigned i = 0; i < EVENT_QUEUE_CAPACITY - EVENT_QUEUE_RESERVE; ++i) {
            CHECK(EventQueue_Post(&q, EVQ_CLASS_INPUT, 1, i, 0, 0));
        }
        CHECK(EventQueue_Service(&q, 0, record, &r) == EVENT_QUEUE_CAPACITY - EVENT_QUEUE_RESERVE);
        const EvqStats s = stats(&q);
        const uint32_t lost = s.rejected[EVQ_CLASS_INTERNAL];
        CHECK(lost == (reposting > EVENT_QUEUE_RESERVE ? 1u : 0u));
    }
}

static void wait_time_survives_tick_wrap(void)
{
    EventQueue q;
    EventQueue_Init(&q);
    Recorder r = {.queue = &q};
    CHECK(EventQueue_Post(&q, EVQ_CLASS_INTERNAL, 1, 0, 0, UINT32_MAX - 4u));
    CHECK(EventQueue_Service(&q, 5u, record, &r) == 1);
    CHECK(stats(&q).max_wait_ms == 10u);
}

static void sequence_numbers_wrap(void)
{
    EventQueue q;
    EventQueue_Init(&q);
    q.next_sequence = UINT32_MAX;
    Recorder r = {.queue = &q};
    CHECK(EventQueue_Post(&q, EVQ_CLASS_INTERNAL, 1, 0, 0, 0));
    CHECK(EventQueue_Post(&q, EVQ_CLASS_INTERNAL, 1, 0, 0, 0));
    CHECK(EventQueue_Service(&q, 0, record, &r) == 2);
    CHECK(r.events[0].sequence == UINT32_MAX && r.events[1].sequence == 0u);
}

static void ring_indices_wrap_over_many_cycles(void)
{
    EventQueue q;
    EventQueue_Init(&q);
    Recorder r = {.queue = &q};
    uint32_t next = 0;
    bool ordered = true;
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        const unsigned n = 1u + cycle % (EVENT_QUEUE_CAPACITY - EVENT_QUEUE_RESERVE);
        for (unsigned i = 0; i < n; ++i) {
            CHECK(EventQueue_Post(&q, EVQ_CLASS_INPUT, 1, next + i, 0, 0));
        }
        r.count = 0;
        CHECK(EventQueue_Service(&q, 0, record, &r) == n);
        for (unsigned i = 0; i < n; ++i) {
            ordered = ordered && r.events[i].arg0 == next + i;
        }
        next += n;
    }
    CHECK(ordered);
    const EvqStats s = stats(&q);
    CHECK(s.posted == next && s.dispatched == next && s.count == 0);
}

static void malformed_calls_are_harmless(void)
{
    EventQueue q;
    EventQueue_Init(&q);
    CHECK(!EventQueue_Post(NULL, EVQ_CLASS_INPUT, 1, 0, 0, 0));
    CHECK(!EventQueue_Post(&q, (EvqClass)7, 1, 0, 0, 0));
    const EvqStats s = stats(&q);
    CHECK(s.rejected[EVQ_CLASS_INTERNAL] == 1 && s.count == 0);
    CHECK(EventQueue_Service(&q, 0, NULL, NULL) == 0);
    CHECK(EventQueue_Service(NULL, 0, record, NULL) == 0);
    EventQueue_GetStats(&q, NULL);
}

#define RUN(test)              \
    do {                       \
        current_test = #test;  \
        test();                \
    } while (0)

int main(void)
{
    RUN(events_are_dispatched_in_post_order_with_sequence_and_time);
    RUN(a_zero_initialized_queue_is_empty_and_usable);
    RUN(input_and_commands_leave_the_reserve_for_internal_events);
    RUN(overrun_rejects_the_newest_and_keeps_queued_events);
    RUN(rejected_input_is_followed_by_one_reconcile_before_more_input);
    RUN(input_stays_rejected_until_the_reconcile_is_posted);
    RUN(a_sustained_burst_posts_one_reconcile_and_keeps_the_reserve);
    RUN(input_lost_after_the_reconcile_is_dispatched_gets_a_new_one);
    RUN(a_loss_after_input_admitted_behind_a_reconcile_gets_a_new_one);
    RUN(a_reconcile_is_posted_on_the_next_post_of_any_class);
    RUN(dispatch_handles_only_events_queued_when_it_started);
    RUN(dispatch_is_run_to_completion);
    RUN(the_reserve_absorbs_internal_posts_during_a_full_dispatch);
    RUN(wait_time_survives_tick_wrap);
    RUN(sequence_numbers_wrap);
    RUN(ring_indices_wrap_over_many_cycles);
    RUN(malformed_calls_are_harmless);
    if (failures != 0) {
        printf("event_queue_test: %d failure(s)\n", failures);
        return 1;
    }
    puts("event_queue_test: all tests passed");
    return 0;
}
