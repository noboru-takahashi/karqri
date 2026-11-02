#ifndef KARQRI_TIMER_H
#define KARQRI_TIMER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "karqri/queue.h"

/*
 * Public one-shot timers using a caller-provided pool; no allocation/free.
 * The caller owns queue and pool. Keep them alive at stable addresses while
 * used, including during callbacks; do not share pool entries with other queues
 * or directly change their links/attributes. Free entries are still linked.
 * Reinitialize the entire owned queue/pool only after all external access,
 * including ticks and callbacks, stops and all old registrations/links are
 * abandoned. This rebuild is distinct from individually resetting linked nodes
 * or a sentinel in use, forbidden by Queue. No destroy operation is provided.
 *
 * No internal synchronization. Callers/ports must serialize shared operations,
 * including the whole tick and its callbacks, against task/interrupt access.
 * Callbacks run synchronously in the tick caller's context. Reentrant start,
 * cancel or tick on the same queue, reinitialization and pool destruction during
 * a callback are outside the contract. Other-subsystem operations (e.g. Event
 * signal) are allowed only when their contracts are met. R6-01 implemented
 * Pico wrapper/tick serialization, protected switch requests/PendSV handoff,
 * and RUNNING-gated tick lifecycle. Special-path hardware verification remains
 * tracked in R7, including alarm miss/stop/restart and IRQ boundary cases.
 * Direct shared Timer operations still require caller/port serialization.
 *
 * Queue pointers and expiry must be valid and non-NULL. destination must remain
 * valid for its callback's use; NULL is allowed only if the callback supports it.
 * destination and events are passed unchanged; Timer does not interpret events
 * as a bitmask. Misuse is not checked.
 */
typedef void (*KarqriTimerExpiry)(
    void *destination,
    uint32_t events);

typedef struct KarqriTimer {
    KarqriQueueNode timer_node;
    void *destination;
    KarqriTimerExpiry expiry;
    uint32_t events;
    uint32_t remaining_ticks;
} KarqriTimer;

typedef struct KarqriTimerQueue {
    KarqriQueueNode free_timers;
    KarqriQueueNode active_timers;
} KarqriTimerQueue;

/* Initializes empty active and full free queues. timers points to timer_count
 * entries; count == 0 is an empty pool and is the only case permitting NULL.
 */
void karqri_timer_queue_init(
    KarqriTimerQueue *queue,
    KarqriTimer *timers,
    size_t timer_count);

/* delay_ticks must be 1..UINT32_MAX; zero is outside the public contract.
 * Starts relative to now in tick calls, using a relative-delta queue. Equal
 * expiry times retain registration order. No callback runs here. Returns false
 * on pool exhaustion without changing active timers; true on registration.
 */
bool karqri_timer_start(
    KarqriTimerQueue *queue,
    void *destination,
    uint32_t events,
    uint32_t delay_ticks,
    KarqriTimerExpiry expiry);

/* Removes only the first active-order match of destination pointer AND full
 * events value, repairs deltas and returns the entry to free without a callback.
 * Returns true if removed, false if absent. expiry is not part of the key.
 */
bool karqri_timer_cancel(
    KarqriTimerQueue *queue,
    void *destination,
    uint32_t events);

/* Advances one tick; empty is a no-op. Expiries run in order, same-time FIFO.
 * Removes each expired entry from active, calls expiry, then returns it to free.
 * That entry is unavailable for reuse during its callback. No automatic restart.
 */
void karqri_timer_tick(KarqriTimerQueue *queue);

#endif
