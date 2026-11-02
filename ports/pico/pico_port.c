#include "pico_port.h"
#include "pico_trace.h"

#include <stddef.h>

#include "hardware/exception.h"
#include "hardware/regs/m0plus.h"
#include "hardware/structs/scb.h"
#include "pico/stdlib.h"

KarqriPicoTaskContext *karqri_pico_active_context;
KarqriPicoTaskContext *karqri_pico_pending_context;
volatile uint32_t karqri_pico_switch_count;
volatile uint32_t karqri_pico_fault_reason;

KarqriPicoSoftwareFrame karqri_pico_main_registers;
uint32_t karqri_pico_main_exc_return;

static KarqriPicoPort *active_port;

extern void karqri_pico_pendsv_handler(void);
extern void karqri_pico_svc_handler(void);
extern void karqri_pico_initial_dispatch_start(void);

static KarqriPicoTaskContext *context_for_task(
    KarqriPicoPort *port,
    const KarqriTask *task)
{
    size_t index;

    for (index = 0U; index < port->context_count; index++) {
        if (port->contexts[index]->task == task &&
            !port->contexts[index]->finished) {
            return port->contexts[index];
        }
    }
    return NULL;
}

static void pend_context_switch(void)
{
    __asm volatile("dmb" ::: "memory");
    scb_hw->icsr = M0PLUS_ICSR_PENDSVSET_BITS;
    __asm volatile("dsb\nisb" ::: "memory");
}

_Noreturn void karqri_pico_fault(uint32_t reason)
{
    karqri_pico_fault_reason = reason;
    __asm volatile("dsb\nudf #0" ::: "memory");
    __builtin_unreachable();
}

/* Both RAM state and the ICSR write are protected by the caller's PRIMASK. */
static void cancel_context_switch(void)
{
#if KARQRI_PICO_TRACE
    KarqriPicoTaskContext *old_pending = karqri_pico_pending_context;
    const bool had_request = old_pending != NULL ||
        (scb_hw->icsr & M0PLUS_ICSR_PENDSVSET_BITS) != 0U;
#endif
    karqri_pico_pending_context = NULL;
    __asm volatile("dmb" ::: "memory");
    scb_hw->icsr = M0PLUS_ICSR_PENDSVCLR_BITS;
    __asm volatile("dsb\nisb" ::: "memory");
#if KARQRI_PICO_TRACE
    if (had_request) {
        KARQRI_TRACE(KARQRI_TRACE_CANCEL, KARQRI_TRACE_UNKNOWN, old_pending, 0U);
    }
#endif
}

static bool thread_uses_stack(uint32_t control_bits)
{
    uint32_t ipsr;
    uint32_t control;

    __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    __asm volatile("mrs %0, control" : "=r"(control));
    return ipsr == 0U && (control & 3U) == control_bits;
}

void karqri_pico_require_running(KarqriPicoPort *port)
{
    if (port == NULL || port != active_port || !port->started ||
        port->core_num != get_core_num()) {
        karqri_pico_fault(KARQRI_PICO_FAULT_PORT_STATE);
    }
}

void karqri_pico_port_init(
    KarqriPicoPort *port,
    KarqriScheduler *scheduler,
    uint32_t slice_ticks)
{
    size_t index;

    port->scheduler = scheduler;
    for (index = 0U; index < KARQRI_PICO_MAX_TASKS; index++) {
        port->contexts[index] = NULL;
    }
    port->context_count = 0U;
    port->slice_ticks = slice_ticks;
    port->core_num = get_core_num();
    port->started = false;
}

bool karqri_pico_task_context_create(
    KarqriPicoPort *port,
    KarqriPicoTaskContext *context,
    KarqriTask *task,
    uint32_t *stack_base,
    size_t stack_words)
{
    if (port->context_count >= KARQRI_PICO_MAX_TASKS ||
        !karqri_pico_context_build(
            context,
            task,
            stack_base,
            stack_words,
            karqri_pico_task_bootstrap,
            karqri_pico_task_return_trap)) {
        return false;
    }

    context->remaining_slice_ticks = port->slice_ticks;
    port->contexts[port->context_count] = context;
    port->context_count++;
    karqri_scheduler_add_ready(port->scheduler, task);
    return true;
}

bool karqri_pico_start(KarqriPicoPort *port)
{
    exception_handler_t old_svc_handler;
    exception_handler_t old_pendsv_handler;
    KarqriPicoTaskContext *initial;
    uint32_t primask = karqri_pico_interrupt_save_and_disable();

    if (primask != 0U || !thread_uses_stack(0U) ||
        port->core_num != get_core_num()) {
        karqri_pico_fault(KARQRI_PICO_FAULT_PORT_CONTEXT);
    }
    if (active_port != NULL || port->started ||
        port->scheduler->current_task == NULL) {
        karqri_pico_interrupt_restore(primask);
        return false;
    }
    initial = context_for_task(port, port->scheduler->current_task);
    if (initial == NULL) {
        karqri_pico_interrupt_restore(primask);
        return false;
    }

    active_port = port;
    karqri_pico_active_context = initial;
    cancel_context_switch();
    karqri_pico_switch_count = 0U;
    old_svc_handler = exception_set_exclusive_handler(
        SVCALL_EXCEPTION, karqri_pico_svc_handler);
    old_pendsv_handler = exception_set_exclusive_handler(
        PENDSV_EXCEPTION, karqri_pico_pendsv_handler);
    exception_set_priority(
        PENDSV_EXCEPTION, PICO_LOWEST_EXCEPTION_PRIORITY);
    /* The first bootstrap publishes RUNNING after SVC establishes PSP. */
    karqri_pico_interrupt_restore(primask);

    karqri_pico_initial_dispatch_start();

    primask = karqri_pico_interrupt_save_and_disable();
    if (port->started) {
        karqri_pico_fault(KARQRI_PICO_FAULT_PORT_STATE);
    }
    cancel_context_switch();
    exception_restore_handler(PENDSV_EXCEPTION, old_pendsv_handler);
    exception_restore_handler(SVCALL_EXCEPTION, old_svc_handler);
    karqri_pico_active_context = NULL;
    karqri_pico_pending_context = NULL;
    active_port = NULL;
    karqri_pico_interrupt_restore(primask);
    return true;
}

static bool update_switch_request_locked(KarqriPicoPort *port
    KARQRI_TRACE_ARG(uint8_t source))
{
    KarqriPicoTaskContext *active = karqri_pico_active_context;
    KarqriTask *candidate;
    KarqriPicoTaskContext *next;

    if (port->core_num != get_core_num()) {
        karqri_pico_fault(KARQRI_PICO_FAULT_PORT_CONTEXT);
    }
    if (port != active_port) {
        if (active_port != NULL) {
            karqri_pico_fault(KARQRI_PICO_FAULT_PORT_STATE);
        }
        return false; /* Do not touch another runtime's PendSV state. */
    }
    if (!port->started) {
        cancel_context_switch();
        return false;
    }
    if (port->scheduler->reschedule_suppressed) {
        if (active != NULL && active->delayed_return_pending) {
            karqri_pico_fault(KARQRI_PICO_FAULT_SUPPRESSION);
        }
        cancel_context_switch();
        return false;
    }
    candidate = karqri_scheduler_current(port->scheduler);
    next = candidate == NULL ? NULL : context_for_task(port, candidate);
    if (next == NULL) {
        if (active != NULL && active->delayed_return_pending) {
            karqri_pico_fault(KARQRI_PICO_FAULT_HANDOFF);
        }
        cancel_context_switch();
        return false;
    }
    if (next->delayed_return_pending &&
        !karqri_task_has_delayed_return(next->task)) {
        karqri_pico_fault(KARQRI_PICO_FAULT_DELAYED_RETURN);
    }
    if (next == active && !next->delayed_return_pending) {
        cancel_context_switch();
        return false;
    }
    /* A completed active waiter still needs a self-PendSV to deliver R0.
     * Never patch active->saved_psp here: its current frame is not saved yet.
     */
#if KARQRI_PICO_TRACE
    KarqriPicoTaskContext *old_pending = karqri_pico_pending_context;
    const bool changed = old_pending != next ||
        (scb_hw->icsr & M0PLUS_ICSR_PENDSVSET_BITS) == 0U;
#endif
    karqri_pico_pending_context = next;
    pend_context_switch();
#if KARQRI_PICO_TRACE
    if (changed) {
        KARQRI_TRACE(old_pending == NULL ? KARQRI_TRACE_SET : KARQRI_TRACE_REPLACE,
                     source, old_pending, 0U);
    }
#endif
    return true;
}

bool karqri_pico_request_switch(KarqriPicoPort *port)
{
    const uint32_t primask = karqri_pico_interrupt_save_and_disable();
    const bool requested = update_switch_request_locked(port
        KARQRI_TRACE_ARG(KARQRI_TRACE_UNKNOWN));

    karqri_pico_interrupt_restore(primask);
    return requested;
}

KarqriPicoTaskContext *karqri_pico_deliver_event_return(
    KarqriPicoTaskContext *next)
{
    if (next->delayed_return_pending) {
        if (!karqri_task_has_delayed_return(next->task)) {
            karqri_pico_fault(KARQRI_PICO_FAULT_DELAYED_RETURN);
        }
        next->saved_psp[KARQRI_PICO_R0_OFFSET / sizeof(uint32_t)] =
            (uint32_t)karqri_task_delayed_return(next->task);
        __asm volatile("dmb" ::: "memory");
        next->delayed_return_pending = false;
    }
    return next;
}

bool karqri_pico_yield(KarqriPicoPort *port)
{
    const uint32_t primask = karqri_pico_interrupt_save_and_disable();
    bool requested = false;

    karqri_pico_require_running(port);
    KARQRI_TRACE(KARQRI_TRACE_YIELD, KARQRI_TRACE_FROM_YIELD, NULL, 0U);
    if (karqri_scheduler_rotate_timeslice(port->scheduler)) {
        requested = update_switch_request_locked(port
            KARQRI_TRACE_ARG(KARQRI_TRACE_FROM_YIELD));
    }
    karqri_pico_interrupt_restore(primask);
    return requested;
}

KarqriTask *karqri_pico_executing_task(void)
{
    if (karqri_pico_active_context == NULL) {
        return NULL;
    }
    return karqri_pico_active_context->task;
}

bool karqri_pico_set_reschedule_suppressed(
    KarqriPicoPort *port,
    bool suppressed)
{
    const uint32_t primask = karqri_pico_interrupt_save_and_disable();
    bool previous;

    karqri_pico_require_running(port);
    /* An IRQ must not cancel a wrapper's outstanding handoff. Diagnose
     * before changing either the suppression bool or the pending request.
     */
    if (suppressed && karqri_pico_active_context != NULL &&
        karqri_pico_active_context->delayed_return_pending) {
        karqri_pico_fault(KARQRI_PICO_FAULT_SUPPRESSION);
    }
    previous = karqri_scheduler_set_reschedule_suppressed(
        port->scheduler, suppressed);

    if (!suppressed) {
        (void)karqri_scheduler_reschedule(port->scheduler);
    }
    (void)update_switch_request_locked(port KARQRI_TRACE_ARG(KARQRI_TRACE_UNKNOWN));
    karqri_pico_interrupt_restore(primask);
    return previous;
}

void karqri_pico_task_bootstrap(KarqriPicoTaskContext *context)
{
    uint32_t primask = karqri_pico_interrupt_save_and_disable();
    uintptr_t psp;
    size_t index;

    __asm volatile("mrs %0, psp" : "=r"(psp));
    if (primask != 0U || !thread_uses_stack(2U) || active_port == NULL ||
        active_port->core_num != get_core_num() ||
        karqri_pico_active_context != context || context->finished ||
        psp < (uintptr_t)context->stack_low ||
        psp > (uintptr_t)context->stack_high) {
        karqri_pico_fault(KARQRI_PICO_FAULT_PORT_CONTEXT);
    }
    if (!active_port->started) {
        if (active_port->scheduler->current_task != context->task ||
            karqri_pico_pending_context != NULL) {
            karqri_pico_fault(KARQRI_PICO_FAULT_PORT_STATE);
        }
        active_port->started = true;
        __asm volatile("dmb" ::: "memory");
    }
    KARQRI_TRACE(KARQRI_TRACE_BOOTSTRAP, KARQRI_TRACE_UNKNOWN, context, 0U);
    karqri_pico_interrupt_restore(primask);
    context->task->entry(context->task->entry_argument);

    primask = karqri_pico_interrupt_save_and_disable();
    karqri_pico_require_running(active_port);
    if (primask != 0U || !thread_uses_stack(2U) ||
        karqri_pico_active_context != context || context->finished ||
        context->delayed_return_pending) {
        karqri_pico_fault(KARQRI_PICO_FAULT_PORT_CONTEXT);
    }
    if (active_port->scheduler->reschedule_suppressed) {
        karqri_pico_fault(KARQRI_PICO_FAULT_SUPPRESSION);
    }
    KARQRI_TRACE(KARQRI_TRACE_RETURN, KARQRI_TRACE_FROM_RETURN, context, 0U);
    context->finished = true;
    karqri_scheduler_remove_task(active_port->scheduler, context->task);
    if (active_port->scheduler->current_task != NULL) {
        if (!update_switch_request_locked(active_port
                KARQRI_TRACE_ARG(KARQRI_TRACE_FROM_RETURN))) {
            karqri_pico_fault(KARQRI_PICO_FAULT_HANDOFF);
        }
        karqri_pico_interrupt_restore(primask);
        __asm volatile("isb" ::: "memory");
        karqri_pico_fault(KARQRI_PICO_FAULT_HANDOFF);
    }

    for (index = 0U; index < active_port->context_count; index++) {
        if (!active_port->contexts[index]->finished) {
            karqri_pico_fault(KARQRI_PICO_FAULT_UNFINISHED_TASKS);
        }
    }
    active_port->started = false;
    cancel_context_switch();
    KARQRI_TRACE(KARQRI_TRACE_STOP, KARQRI_TRACE_FROM_RETURN, context, 0U);
    karqri_pico_interrupt_restore(primask);
    __asm volatile("svc #1");
    __builtin_unreachable();
}

void karqri_pico_task_return_trap(void)
{
    for (;;) {
        tight_loop_contents();
    }
}
