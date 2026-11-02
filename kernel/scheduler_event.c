#include "karqri/scheduler_event.h"

static void scheduled_event_wait_started(void *owner)
{
    KarqriScheduledEventWaiter *waiter = owner;

    karqri_task_clear_delayed_return(waiter->task);
    karqri_scheduler_begin_wait(
        waiter->scheduler,
        waiter->task,
        KARQRI_TASK_STATE_EVENT_WAIT);
}

static void scheduled_event_return_value_set(
    void *owner,
    uint32_t events)
{
    KarqriScheduledEventWaiter *waiter = owner;

    karqri_task_set_delayed_return(
        waiter->task, (uintptr_t)events);
}

static void scheduled_event_wait_completed(void *owner)
{
    KarqriScheduledEventWaiter *waiter = owner;

    karqri_scheduler_ready(
        waiter->scheduler,
        waiter->task,
        KARQRI_TASK_STATE_EVENT_WAIT);
}

static const KarqriEventWaitOperations scheduled_event_operations = {
    scheduled_event_wait_started,
    scheduled_event_return_value_set,
    scheduled_event_wait_completed,
};

KarqriEventWaitResult karqri_scheduler_wait_event(
    KarqriScheduler *scheduler,
    KarqriTask *task,
    KarqriScheduledEventWaiter *waiter,
    KarqriEvent *event,
    uint32_t requested_events,
    KarqriEventWaitMode mode)
{
    waiter->scheduler = scheduler;
    waiter->task = task;
    return karqri_event_wait(
        event,
        &waiter->event_waiter,
        requested_events,
        mode,
        waiter,
        &scheduled_event_operations);
}
