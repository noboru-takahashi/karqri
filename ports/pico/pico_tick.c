#include "pico_tick.h"

#include "hardware/timer.h"
#include "hardware/irq.h"
#include "pico/time.h"

static KarqriPicoTick *ticks[NUM_ALARMS];

_Static_assert(KARQRI_PICO_TICK_MAX_CONSECUTIVE_MISSES > 0U &&
                   KARQRI_PICO_TICK_MAX_CONSECUTIVE_MISSES < UINT32_MAX,
               "tick miss budget must be positive and leave room to increment");

static void require_tick_thread(void)
{
    uint32_t ipsr;
    uint32_t control;

    __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    __asm volatile("mrs %0, control" : "=r"(control));
    if (ipsr != 0U || (control & 1U) != 0U) {
        karqri_pico_fault(KARQRI_PICO_FAULT_TICK_CONTEXT);
    }
}

/* Caller holds PRIMASK on the registration core. Unclaim only after both
 * SDK and port callback state are detached and the old IRQ is cleared.
 */
static void stop_tick_locked(KarqriPicoTick *tick, uint alarm_num)
{
    hardware_alarm_cancel(alarm_num);
    hardware_alarm_set_callback(alarm_num, NULL);
    /* SDK cancel clears its pending flag but leaves peripheral IRQ causes,
     * notably a forced IRQ. Clear both causes before releasing the alarm.
     */
    PICO_DEFAULT_TIMER_INSTANCE()->intr = 1U << alarm_num;
    hw_clear_bits(&PICO_DEFAULT_TIMER_INSTANCE()->intf, 1U << alarm_num);
    __asm volatile("dsb" ::: "memory");
    irq_clear(hardware_alarm_get_irq_num(alarm_num));
    ticks[alarm_num] = NULL;
    tick->alarm_num = -1;
    hardware_alarm_unclaim(alarm_num);
}

/* Callback entry has already satisfied the SDK's target_hi check. Initial
 * arm must not use this recovery: a previous owner's target_hi may remain.
 */
static void arm_tick_locked(KarqriPicoTick *tick, uint alarm_num)
{
    if (hardware_alarm_set_target(
            alarm_num, make_timeout_time_us(tick->period_us))) {
        tick->missed_rearms++;
        tick->consecutive_misses++;
        if (tick->consecutive_misses >
            KARQRI_PICO_TICK_MAX_CONSECUTIVE_MISSES) {
            karqri_pico_fault(KARQRI_PICO_FAULT_TICK_MISSED);
        }
        hardware_alarm_force_irq(alarm_num);
    } else {
        tick->consecutive_misses = 0U;
    }
}

static void tick_alarm_callback(uint alarm_num)
{
    const uint32_t primask = karqri_pico_interrupt_save_and_disable();
    KarqriPicoTick *tick = ticks[alarm_num];
    KarqriPicoTaskContext *interrupted_context;
    KarqriTask *interrupted_task;

    if (tick == NULL) {
        karqri_pico_interrupt_restore(primask);
        return;
    }
    if (tick->port->core_num != get_core_num()) {
        karqri_pico_fault(KARQRI_PICO_FAULT_TICK_CONTEXT);
    }
    tick->irq_count++;
    if (tick->port->started) {
        tick->kernel_ticks++;
        interrupted_context = karqri_pico_active_context;
        interrupted_task = interrupted_context == NULL
                               ? NULL
                               : interrupted_context->task;

        karqri_timer_tick(tick->timer_queue);

        if (interrupted_context != NULL && tick->port->slice_ticks != 0U) {
            interrupted_context->remaining_slice_ticks--;
            if (interrupted_context->remaining_slice_ticks == 0U) {
                interrupted_context->remaining_slice_ticks =
                    tick->port->slice_ticks;
                (void)karqri_scheduler_rotate_task_timeslice(
                    tick->port->scheduler, interrupted_task);
            }
        }
        if (karqri_pico_request_switch(tick->port)) {
            tick->switch_requests++;
        }
    }

    /* Preserve the existing alarm phase at RUNNING entry. No elapsed-time
     * compensation or callback-local retry loop; a miss schedules a new IRQ.
     */
    arm_tick_locked(tick, alarm_num);
    karqri_pico_interrupt_restore(primask);
}

bool karqri_pico_tick_start(
    KarqriPicoTick *tick,
    KarqriPicoPort *port,
    KarqriTimerQueue *timer_queue,
    uint32_t period_us)
{
    int alarm_num;
    uint index;
    uint32_t primask;

    require_tick_thread();
    if (tick == NULL || port == NULL || timer_queue == NULL ||
        period_us == 0U) {
        return false;
    }
    if (port->core_num != get_core_num()) {
        karqri_pico_fault(KARQRI_PICO_FAULT_TICK_CONTEXT);
    }
    primask = karqri_pico_interrupt_save_and_disable();
    for (index = 0U; index < NUM_ALARMS; index++) {
        if (ticks[index] == tick) {
            karqri_pico_interrupt_restore(primask);
            return false;
        }
    }
    alarm_num = hardware_alarm_claim_unused(false);
    if (alarm_num < 0) {
        karqri_pico_interrupt_restore(primask);
        return false;
    }

    tick->port = port;
    tick->timer_queue = timer_queue;
    tick->period_us = period_us;
    tick->alarm_num = alarm_num;
    tick->irq_count = 0U;
    tick->kernel_ticks = 0U;
    tick->switch_requests = 0U;
    tick->missed_rearms = 0U;
    tick->consecutive_misses = 0U;
    ticks[(uint)alarm_num] = tick;
    hardware_alarm_set_callback((uint)alarm_num, tick_alarm_callback);
    if (hardware_alarm_set_target(
            (uint)alarm_num, make_timeout_time_us(period_us))) {
        stop_tick_locked(tick, (uint)alarm_num);
        karqri_pico_interrupt_restore(primask);
        return false;
    }
    karqri_pico_interrupt_restore(primask);
    return true;
}

void karqri_pico_tick_stop(KarqriPicoTick *tick)
{
    uint alarm_num;
    uint32_t primask;

    require_tick_thread();
    primask = karqri_pico_interrupt_save_and_disable();
    /* Registry lookup also makes stop safe after failed/unattempted start;
     * do not read an uninitialized tick->alarm_num or release someone else's.
     */
    for (alarm_num = 0U; alarm_num < NUM_ALARMS; alarm_num++) {
        if (tick != NULL && ticks[alarm_num] == tick) {
            if (tick->port->core_num != get_core_num()) {
                karqri_pico_fault(KARQRI_PICO_FAULT_TICK_CONTEXT);
            }
            stop_tick_locked(tick, alarm_num);
            break;
        }
    }
    karqri_pico_interrupt_restore(primask);
}
