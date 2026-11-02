#include "posix_kernel.h"

uintptr_t karqri_posix_wait_event(
    KarqriPosixPort *port,
    KarqriScheduledEventWaiter *waiter,
    KarqriEvent *event,
    uint32_t requested_events,
    KarqriEventWaitMode mode)
{
    KarqriTask *task = karqri_posix_executing_task(port);
    KarqriEventWaitResult result;

    if (task == NULL) {
        return 0U;
    }

    result = karqri_scheduler_wait_event(
        port->scheduler,
        task,
        waiter,
        event,
        requested_events,
        mode);
    if (result.disposition == KARQRI_EVENT_WAIT_COMPLETED) {
        return (uintptr_t)result.events;
    }

    (void)karqri_posix_dispatch(port);
    if (!karqri_task_has_delayed_return(task)) {
        return 0U;
    }
    return karqri_task_delayed_return(task);
}

void karqri_posix_signal_event(
    KarqriPosixPort *port,
    KarqriEvent *event,
    uint32_t events)
{
    karqri_event_signal(event, events);
    (void)karqri_posix_dispatch(port);
}

void karqri_posix_timer_tick(
    KarqriPosixPort *port,
    KarqriTimerQueue *timer_queue)
{
    karqri_timer_tick(timer_queue);
    (void)karqri_posix_dispatch(port);
}

bool karqri_posix_suspend_task(
    KarqriPosixPort *port,
    KarqriTask *task)
{
    const bool suspended = karqri_scheduler_suspend(
        port->scheduler, task);

    if (suspended) {
        (void)karqri_posix_dispatch(port);
    }
    return suspended;
}

bool karqri_posix_resume_task(
    KarqriPosixPort *port,
    KarqriTask *task)
{
    const bool resumed = karqri_scheduler_resume(
        port->scheduler, task);

    if (resumed) {
        (void)karqri_posix_dispatch(port);
    }
    return resumed;
}
