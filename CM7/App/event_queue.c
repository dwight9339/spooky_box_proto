#include "event_queue.h"

#include <stddef.h>
#include <string.h>

static uint32_t free_slots(const EventQueue *queue)
{
    return EVENT_QUEUE_CAPACITY - queue->count;
}

static void enqueue(EventQueue *queue, EvqClass event_class, uint16_t type,
                    uint32_t arg0, uint32_t arg1, uint32_t now_ms)
{
    const uint32_t tail = (queue->head + queue->count) % EVENT_QUEUE_CAPACITY;
    queue->entries[tail] = (EvqEvent){
        .type = type,
        .event_class = (uint8_t)event_class,
        .reserved = 0u,
        .sequence = queue->next_sequence++,
        .posted_ms = now_ms,
        .arg0 = arg0,
        .arg1 = arg1,
    };
    ++queue->count;
    ++queue->posted;
    if (type == EVQ_TYPE_RECONCILE) {
        ++queue->reconciles_queued;
    }
    if (queue->count > queue->high_water) {
        queue->high_water = queue->count;
    }
}

/* Decision 0007 item 6: after a rejected input, post one reconcile event as soon as
 * any slot is free. */
static void post_pending_reconcile(EventQueue *queue, uint32_t now_ms)
{
    if (queue->reconcile_pending && free_slots(queue) > 0u) {
        enqueue(queue, EVQ_CLASS_INTERNAL, EVQ_TYPE_RECONCILE, 0u, 0u, now_ms);
        ++queue->reconciles;
        queue->reconcile_pending = false;
    }
}

void EventQueue_Init(EventQueue *queue)
{
    if (queue != NULL) {
        memset(queue, 0, sizeof(*queue));
    }
}

bool EventQueue_Post(EventQueue *queue, EvqClass event_class, uint16_t type,
                     uint32_t arg0, uint32_t arg1, uint32_t now_ms)
{
    if (queue == NULL) {
        return false;
    }
    if ((unsigned)event_class >= (unsigned)EVQ_CLASS_COUNT) {
        event_class = EVQ_CLASS_INTERNAL; /* count a malformed post as a design error */
        ++queue->rejected[event_class];
        return false;
    }
    post_pending_reconcile(queue, now_ms);

    bool admit;
    if (event_class == EVQ_CLASS_INTERNAL) {
        admit = free_slots(queue) > 0u;
    } else {
        admit = free_slots(queue) > EVENT_QUEUE_RESERVE;
    }
    if (event_class == EVQ_CLASS_INPUT && queue->reconcile_pending) {
        admit = false;
    }
    if (!admit) {
        ++queue->rejected[event_class];
        /* A reconcile already queued is dispatched after this loss and releases
         * every control, so it covers this input too (full_spooky_proto-8lw.17). */
        if (event_class == EVQ_CLASS_INPUT && queue->reconciles_queued == 0u) {
            queue->reconcile_pending = true;
        }
        return false;
    }
    enqueue(queue, event_class, type, arg0, arg1, now_ms);
    return true;
}

uint32_t EventQueue_Service(EventQueue *queue, uint32_t now_ms, EvqDispatch dispatch,
                            void *context)
{
    if (queue == NULL || dispatch == NULL) {
        return 0u;
    }
    post_pending_reconcile(queue, now_ms);
    /* Only events queued now; anything posted during dispatch waits (item 3). */
    const uint32_t limit = queue->count;
    for (uint32_t i = 0u; i < limit; ++i) {
        const EvqEvent event = queue->entries[queue->head];
        queue->head = (queue->head + 1u) % EVENT_QUEUE_CAPACITY;
        --queue->count;
        if (event.type == EVQ_TYPE_RECONCILE && queue->reconciles_queued > 0u) {
            --queue->reconciles_queued;
        }
        const uint32_t wait = now_ms - event.posted_ms; /* modulo 2^32 */
        if (wait > queue->max_wait_ms) {
            queue->max_wait_ms = wait;
        }
        ++queue->dispatched;
        dispatch(context, &event);
    }
    post_pending_reconcile(queue, now_ms);
    return limit;
}

void EventQueue_GetStats(const EventQueue *queue, EvqStats *stats)
{
    if (queue == NULL || stats == NULL) {
        return;
    }
    *stats = (EvqStats){
        .capacity = EVENT_QUEUE_CAPACITY,
        .reserve = EVENT_QUEUE_RESERVE,
        .count = queue->count,
        .high_water = queue->high_water,
        .posted = queue->posted,
        .dispatched = queue->dispatched,
        .reconciles = queue->reconciles,
        .max_wait_ms = queue->max_wait_ms,
        .reconcile_pending = queue->reconcile_pending,
    };
    for (unsigned i = 0u; i < (unsigned)EVQ_CLASS_COUNT; ++i) {
        stats->rejected[i] = queue->rejected[i];
    }
}
