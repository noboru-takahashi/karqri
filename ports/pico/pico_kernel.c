#include "pico_kernel.h"

#include <stddef.h>

#include "pico_kernel_offsets.h"

_Static_assert(sizeof(void *) == sizeof(uint32_t),
               "RP2040 event wait assembly requires 32-bit pointers");
_Static_assert(offsetof(KarqriPicoEventWaitCall, blocked) ==
                   KARQRI_PICO_EVENT_WAIT_BLOCKED_OFFSET,
               "RP2040 event wait blocked offset changed");

uint32_t karqri_pico_event_wait_prepare(KarqriPicoEventWaitCall *call)
{
    KarqriPicoTaskContext *context = karqri_pico_active_context;
    KarqriEventWaitResult result;

    /* Called only by the assembly wrapper with interrupts masked. */
    karqri_pico_require_running(call->port);
    if (context == NULL || context->finished || !call->port->started ||
        context->delayed_return_pending) {
        karqri_pico_fault(KARQRI_PICO_FAULT_WAIT_CONTEXT);
    }
    call->blocked = 0U;
    result = karqri_scheduler_wait_event(
        call->port->scheduler,
        context->task,
        call->waiter,
        call->event,
        call->requested_events,
        call->mode);
    if (result.disposition == KARQRI_EVENT_WAIT_COMPLETED) {
        return result.events;
    }

    context->delayed_return_pending = true;
    call->blocked = 1U;
    if (!karqri_pico_request_switch(call->port)) {
        karqri_pico_fault(KARQRI_PICO_FAULT_HANDOFF);
    }
    return 0U;
}

void karqri_pico_event_signal(
    KarqriPicoPort *port,
    KarqriEvent *event,
    uint32_t events)
{
    const uint32_t primask = karqri_pico_interrupt_save_and_disable();

    karqri_pico_require_running(port);
    karqri_event_signal(event, events);
    (void)karqri_pico_request_switch(port);
    karqri_pico_interrupt_restore(primask);
}

bool karqri_pico_suspend_task(
    KarqriPicoPort *port,
    KarqriTask *task)
{
    const uint32_t primask = karqri_pico_interrupt_save_and_disable();
    bool suspended;

    karqri_pico_require_running(port);
    suspended = karqri_scheduler_suspend(
        port->scheduler, task);

    if (suspended) {
        (void)karqri_pico_request_switch(port);
    }
    karqri_pico_interrupt_restore(primask);
    return suspended;
}

bool karqri_pico_resume_task(
    KarqriPicoPort *port,
    KarqriTask *task)
{
    const uint32_t primask = karqri_pico_interrupt_save_and_disable();
    bool resumed;

    karqri_pico_require_running(port);
    resumed = karqri_scheduler_resume(
        port->scheduler, task);

    if (resumed) {
        (void)karqri_pico_request_switch(port);
    }
    karqri_pico_interrupt_restore(primask);
    return resumed;
}
