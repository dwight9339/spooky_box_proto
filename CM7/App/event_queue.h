#ifndef SPOOKY_EVENT_QUEUE_H
#define SPOOKY_EVENT_QUEUE_H

/*
 * The M7 application event queue (decision 0007): one fixed-capacity FIFO with one
 * consumer, fed only from foreground code. Admission is by class, with slots
 * reserved for internal events; overrun always rejects the newest event.
 *
 * Portable C with no HAL calls; callers pass the time. Not reentrant and not for
 * ISRs: post and service from the foreground loop only. A zero-initialized queue is
 * valid and empty.
 */

#include <stdbool.h>
#include <stdint.h>

/* Starting sizes from decision 0007 item 13; configurable until bench trials fix
 * them. */
#ifndef EVENT_QUEUE_CAPACITY
#define EVENT_QUEUE_CAPACITY 32u
#endif
#ifndef EVENT_QUEUE_RESERVE
#define EVENT_QUEUE_RESERVE 8u
#endif

_Static_assert(EVENT_QUEUE_RESERVE < EVENT_QUEUE_CAPACITY,
               "the reserve must leave room for input and external commands");

/* Event type 0 is the reconcile event the queue posts after a rejected input:
 * every machine that tracks held controls releases them. */
#define EVQ_TYPE_RECONCILE 0u

typedef enum EvqClass {
    EVQ_CLASS_INPUT = 0,        /* input events from the input adapter */
    EVQ_CLASS_EXTERNAL_COMMAND, /* commands from the CLI */
    EVQ_CLASS_INTERNAL,         /* service, timer, cross-region and machine-issued events */
    EVQ_CLASS_COUNT
} EvqClass;

typedef struct EvqEvent {
    uint16_t type;
    uint8_t event_class; /* EvqClass */
    uint8_t reserved;
    uint32_t sequence;   /* assigned at admission, modulo 2^32 */
    uint32_t posted_ms;
    uint32_t arg0;
    uint32_t arg1;
} EvqEvent;

_Static_assert(sizeof(EvqEvent) == 20u, "event queue entries are 20 bytes");

typedef struct EvqStats {
    uint32_t capacity;
    uint32_t reserve;
    uint32_t count;
    uint32_t high_water;
    uint32_t posted;     /* admitted events, including reconcile events */
    uint32_t dispatched;
    uint32_t rejected[EVQ_CLASS_COUNT];
    uint32_t reconciles; /* reconcile events posted after rejected input */
    uint32_t max_wait_ms; /* longest time between post and dispatch */
    bool reconcile_pending;
} EvqStats;

typedef struct EventQueue {
    EvqEvent entries[EVENT_QUEUE_CAPACITY];
    uint32_t head;
    uint32_t count;
    uint32_t next_sequence;
    bool reconcile_pending;
    uint32_t reconciles_queued; /* reconcile events queued and not yet dispatched */
    bool reconcile_is_last_input; /* no input admitted behind the newest queued reconcile */
    uint32_t high_water;
    uint32_t posted;
    uint32_t dispatched;
    uint32_t rejected[EVQ_CLASS_COUNT];
    uint32_t reconciles;
    uint32_t max_wait_ms;
} EventQueue;

/* Handles one event to completion, including every machine it is routed to. It may
 * post; those events wait for the next EventQueue_Service call. */
typedef void (*EvqDispatch)(void *context, const EvqEvent *event);

void EventQueue_Init(EventQueue *queue);

/* Admits the event if its class may use a free slot. Input and external commands
 * need more than EVENT_QUEUE_RESERVE free slots; internal events need one. Returns
 * false if the event was rejected. A rejected input sets the reconcile flag: further
 * input is rejected until the queue has posted one reconcile event, which it does
 * as soon as a slot is free. Input rejected while the newest queued reconcile has no
 * input admitted behind it does not set the flag again: that reconcile is dispatched
 * after every admitted input and after the loss, and releases every control, so a
 * sustained burst posts one reconcile rather than filling the internal reserve
 * (full_spooky_proto-8lw.17). Once an input is admitted behind it, a later loss
 * needs a new reconcile (full_spooky_proto-8lw.18). Other producers that lose input,
 * such as the product
 * IPC on restart or staleness, post EVQ_TYPE_RECONCILE as an internal event. */
bool EventQueue_Post(EventQueue *queue, EvqClass event_class, uint16_t type,
                     uint32_t arg0, uint32_t arg1, uint32_t now_ms);

/* Dispatches, in order, at most the events queued when the call started, and
 * returns how many it dispatched. */
uint32_t EventQueue_Service(EventQueue *queue, uint32_t now_ms, EvqDispatch dispatch,
                            void *context);

void EventQueue_GetStats(const EventQueue *queue, EvqStats *stats);

#endif /* SPOOKY_EVENT_QUEUE_H */
