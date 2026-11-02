#ifndef KARQRI_PICO_TICK_H
#define KARQRI_PICO_TICK_H

#include <stdbool.h>
#include <stdint.h>

#include "karqri/timer.h"
#include "pico_port.h"

/* Recovery budget, not a timing guarantee: allow eight forced callbacks for
 * transient rearm misses, then diagnose persistent overload on the ninth.
 * Override at build time after measuring the target's IRQ/lock latency.
 */
#ifndef KARQRI_PICO_TICK_MAX_CONSECUTIVE_MISSES
#define KARQRI_PICO_TICK_MAX_CONSECUTIVE_MISSES 8U
#endif

typedef struct KarqriPicoTick {
    KarqriPicoPort *port;
    KarqriTimerQueue *timer_queue;
    uint32_t period_us;
    int alarm_num;
    volatile uint32_t irq_count;
    volatile uint32_t kernel_ticks;
    volatile uint32_t switch_requests;
    volatile uint32_t missed_rearms;
    volatile uint32_t consecutive_misses;
} KarqriPicoTick;

/* Same-core privileged Thread only. Port/tick storage must remain alive until
 * stop returns. Reentry from callbacks/IRQs and cross-core access diagnose.
 * Start/stop preserve the caller's PRIMASK, including an existing mask.
 */
bool karqri_pico_tick_start(
    KarqriPicoTick *tick,
    KarqriPicoPort *port,
    KarqriTimerQueue *timer_queue,
    uint32_t period_us);
void karqri_pico_tick_stop(KarqriPicoTick *tick);

#endif
