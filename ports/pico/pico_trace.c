#include "pico_trace.h"

#if KARQRI_PICO_TRACE
#include <stdio.h>
#include "hardware/timer.h"
#include "hardware/structs/scb.h"
#include "hardware/regs/m0plus.h"

_Static_assert(sizeof(KarqriPicoTraceRecord) == 32U, "trace record ABI");
_Static_assert(sizeof(uintptr_t) == 4U, "trace stores RP2040 pointers");
_Static_assert((KARQRI_PICO_TRACE_CAPACITY & (KARQRI_PICO_TRACE_CAPACITY - 1U)) == 0U,
               "trace ring capacity must be a power of two");

static KarqriPicoTraceRecord records[KARQRI_PICO_TRACE_CAPACITY];
static struct {
    KarqriPicoPort *port;
    uint32_t total;
    uint32_t overwritten;
    uint32_t pending_seq;
    bool capturing;
    bool seq_overflow;
    uint64_t start_us;
    uint64_t end_us;
} trace;
KarqriPicoTaskContext *karqri_pico_trace_old_context;

void karqri_pico_trace_begin(KarqriPicoPort *port)
{
    const uint32_t primask = karqri_pico_interrupt_save_and_disable();
    trace.port = port;
    trace.total = 0U;
    trace.overwritten = 0U;
    trace.pending_seq = 0U;
    trace.seq_overflow = false;
    trace.start_us = time_us_64();
    trace.end_us = trace.start_us;
    trace.capturing = true;
    karqri_pico_interrupt_restore(primask);
}

void karqri_pico_trace_freeze(void)
{
    const uint32_t primask = karqri_pico_interrupt_save_and_disable();
    trace.capturing = false;
    trace.end_us = time_us_64();
    karqri_pico_interrupt_restore(primask);
}

void karqri_pico_trace_record(uint16_t event, uint8_t source,
                              uintptr_t subject, uint32_t value)
{
    const uint32_t primask = karqri_pico_interrupt_save_and_disable();
    KarqriPicoTaskContext *active;
    KarqriPicoTraceRecord *record;
    uint32_t ipsr;
    if (!trace.capturing) {
        karqri_pico_interrupt_restore(primask);
        return;
    }
    if (trace.total == UINT32_MAX) {
        trace.seq_overflow = true;
        trace.capturing = false;
        karqri_pico_interrupt_restore(primask);
        return;
    }
    active = karqri_pico_active_context;
    record = &records[trace.total & (KARQRI_PICO_TRACE_CAPACITY - 1U)];
    record->seq = trace.total + 1U;
    record->time_us_low = time_us_32();
    record->active_context = (uint32_t)(uintptr_t)active;
    record->scheduler_current = (uint32_t)(uintptr_t)trace.port->scheduler->current_task;
    record->pending_context = (uint32_t)(uintptr_t)karqri_pico_pending_context;
    record->subject = (uint32_t)subject;
    record->value = value;
    record->event = event;
    record->source = source;
    __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    /* H, suppression, active D, active V, handler mode. */
    record->flags = ((scb_hw->icsr & M0PLUS_ICSR_PENDSVSET_BITS) != 0U ? 1U : 0U)
        | (trace.port->scheduler->reschedule_suppressed ? 2U : 0U)
        | (active != NULL && active->delayed_return_pending ? 4U : 0U)
        | (active != NULL && karqri_task_has_delayed_return(active->task) ? 8U : 0U)
        | (ipsr != 0U ? 16U : 0U);
    if (event == KARQRI_TRACE_SET || event == KARQRI_TRACE_REPLACE) {
        trace.pending_seq = record->seq;
    } else if (event == KARQRI_TRACE_CANCEL) {
        trace.pending_seq = 0U;
    }
    if (trace.total >= KARQRI_PICO_TRACE_CAPACITY) {
        trace.overwritten++;
    }
    __asm volatile("dmb" ::: "memory");
    trace.total++;
    karqri_pico_interrupt_restore(primask);
}

/* Called on MSP after next's R4-R11/PSP and switch count are restored, but
 * before PRIMASK. AAPCS preserves R4-R11; EXC_RETURN is in the handler's slot.
 * This observes readiness to exception-return, not a next-Task instruction.
 */
void karqri_pico_trace_commit(void)
{
    karqri_pico_trace_record(KARQRI_TRACE_COMMIT, KARQRI_TRACE_UNKNOWN,
        (uintptr_t)karqri_pico_trace_old_context, trace.pending_seq);
    trace.pending_seq = 0U;
}

void karqri_pico_trace_dump(const char *labels)
{
    static const char *const names[] = {
        "INVALID", "THREAD_BOOTSTRAP", "TASK_MARK", "YIELD", "REQUEST_SET",
        "REPLACE", "CANCEL", "HANDOFF_COMMIT", "TASK_RETURN", "RUN_STOP"
    };
    const uint32_t count = trace.total < KARQRI_PICO_TRACE_CAPACITY
        ? trace.total : KARQRI_PICO_TRACE_CAPACITY;
    uint32_t index;
    if (trace.capturing || trace.port == NULL) {
        return; /* No live consumer; freeze on main before printing. */
    }
    printf("PICO_TRACE_BEGIN version=1 phase=cooperative_three_task count=%lu "
           "overwritten=%lu seq_overflow=%u time_valid=%u start_us=%llu\n",
           (unsigned long)count, (unsigned long)trace.overwritten,
           trace.seq_overflow ? 1U : 0U,
           trace.end_us - trace.start_us < (UINT64_C(1) << 32U) ? 1U : 0U,
           (unsigned long long)trace.start_us);
    for (index = 0U; index < trace.port->context_count; index++) {
        const KarqriPicoTaskContext *context = trace.port->contexts[index];
        printf("PICO_TRACE_TASK name=%c context=0x%08lx task=0x%08lx\n",
               labels[index], (unsigned long)(uintptr_t)context,
               (unsigned long)(uintptr_t)context->task);
    }
    for (index = trace.total - count; index < trace.total; index++) {
        const KarqriPicoTraceRecord *r = &records[index & (KARQRI_PICO_TRACE_CAPACITY - 1U)];
        printf("PICO_TRACE seq=%lu us=%lu event=%s source=%u active=0x%08lx "
               "current=0x%08lx pending=0x%08lx subject=0x%08lx value=%lu flags=0x%02x\n",
               (unsigned long)r->seq, (unsigned long)(r->time_us_low - (uint32_t)trace.start_us),
               names[r->event], r->source, (unsigned long)r->active_context,
               (unsigned long)r->scheduler_current, (unsigned long)r->pending_context,
               (unsigned long)r->subject, (unsigned long)r->value, r->flags);
    }
    printf("PICO_TRACE_END phase=cooperative_three_task count=%lu overwritten=%lu\n",
           (unsigned long)count, (unsigned long)trace.overwritten);
}
#endif
