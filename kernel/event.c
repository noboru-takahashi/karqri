#include "karqri/event.h"

#include <stdbool.h>
#include <stddef.h>

#define KARQRI_CONTAINER_OF(pointer, type, member) \
    ((type *)((unsigned char *)(pointer) - offsetof(type, member)))

static bool event_condition_is_satisfied(
    uint32_t pending_events,
    uint32_t requested_events,
    KarqriEventWaitMode mode)
{
    if (mode == KARQRI_EVENT_WAIT_ANY) {
        return (pending_events & requested_events) != 0U;
    }

    return (pending_events & requested_events) == requested_events;
}

void karqri_event_init(KarqriEvent *event)
{
    event->pending_events = 0U;
    karqri_queue_init(&event->waiters);
}

KarqriEventWaitResult karqri_event_wait(
    KarqriEvent *event,
    KarqriEventWaiter *waiter,
    uint32_t requested_events,
    KarqriEventWaitMode mode,
    void *owner,
    const KarqriEventWaitOperations *operations)
{
    KarqriEventWaitResult result;

    result.events = event->pending_events;
    if (event_condition_is_satisfied(
            result.events, requested_events, mode)) {
        event->pending_events &= ~requested_events;
        result.disposition = KARQRI_EVENT_WAIT_COMPLETED;
        return result;
    }

    waiter->owner = owner;
    waiter->operations = operations;
    waiter->requested_events = requested_events;
    waiter->mode = mode;
    karqri_queue_init(&waiter->wait_node);

    operations->wait_started(owner);
    karqri_queue_push_back(&event->waiters, &waiter->wait_node);

    result.disposition = KARQRI_EVENT_WAIT_BLOCKED;
    result.events = 0U;
    return result;
}

void karqri_event_signal(KarqriEvent *event, uint32_t signaled_events)
{
    KarqriQueueNode *position;
    const uint32_t detected_events =
        event->pending_events | signaled_events;

    event->pending_events = detected_events;
    position = event->waiters.next;

    while (position != &event->waiters) {
        KarqriQueueNode *next = position->next;
        KarqriEventWaiter *waiter = KARQRI_CONTAINER_OF(
            position, KarqriEventWaiter, wait_node);

        if (event_condition_is_satisfied(
                detected_events,
                waiter->requested_events,
                waiter->mode)) {
            waiter->operations->return_value_set(
                waiter->owner, detected_events);
            event->pending_events &= ~waiter->requested_events;
            karqri_queue_remove(&waiter->wait_node);
            waiter->operations->wait_completed(waiter->owner);
        }

        position = next;
    }
}

uint32_t karqri_event_pending(const KarqriEvent *event)
{
    return event->pending_events;
}
