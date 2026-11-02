#ifndef KARQRI_EVENT_H
#define KARQRI_EVENT_H

#include <stdint.h>

#include "karqri/queue.h"

/*
 * Public bit-set primitive: 32 pending bits, not notification counts.
 * The caller owns all storage. Initialize the event before use. Keep the event,
 * registered waiter, owner (if non-NULL) and operations valid at stable addresses
 * through their use and completion callbacks. Do not move, reinitialize or
 * register an already registered waiter; after completion and detachment it
 * may be reused. Mid-wait cancellation is outside the initial-release contract.
 * Reinitialize an event only with no waiters and after external references and
 * notifications (including timers) have stopped. There is no destroy operation.
 *
 * Event, waiter and operations pointers and all three callbacks must be valid
 * and non-NULL, even for immediate completion. owner may be NULL only when the
 * callbacks support it. Misuse is not checked.
 *
 * No internal synchronization or CPU context switching. Callers/ports must
 * serialize shared access, including reads and the entire wait/signal operation
 * with callbacks. Callbacks run synchronously in the caller's task/interrupt
 * context. Reentry that changes the same event or invalidates its traversal is
 * outside the contract; scheduler updates by scheduler_event are permitted.
 * Such logical current updates do not authorize a CPU context switch before
 * this operation (and any enclosing Timer tick) completes. Ports must dispatch
 * while preserving the outer operations' serialization/non-reentry conditions.
 */
typedef enum KarqriEventWaitMode {
    KARQRI_EVENT_WAIT_ANY,
    KARQRI_EVENT_WAIT_ALL
} KarqriEventWaitMode;

typedef enum KarqriEventWaitDisposition {
    KARQRI_EVENT_WAIT_COMPLETED,
    KARQRI_EVENT_WAIT_BLOCKED
} KarqriEventWaitDisposition;

typedef struct KarqriEventWaitResult {
    KarqriEventWaitDisposition disposition;
    uint32_t events;
} KarqriEventWaitResult;

/* Blocked wait: wait_started runs before queue registration. On signal:
 * return_value_set runs while linked, then requested bits are consumed and the
 * waiter is detached, then wait_completed runs. Immediate waits call none.
 * Callbacks must return without invalidating storage still used by the operation.
 */
typedef struct KarqriEventWaitOperations {
    void (*wait_started)(void *owner);
    void (*return_value_set)(void *owner, uint32_t events);
    void (*wait_completed)(void *owner);
} KarqriEventWaitOperations;

typedef struct KarqriEventWaiter {
    KarqriQueueNode wait_node;
    void *owner;
    const KarqriEventWaitOperations *operations;
    uint32_t requested_events;
    KarqriEventWaitMode mode;
} KarqriEventWaiter;

typedef struct KarqriEvent {
    uint32_t pending_events;
    KarqriQueueNode waiters;
} KarqriEvent;

/* Clears pending and initializes an empty waiter queue; no waiter notifications. */
void karqri_event_init(KarqriEvent *event);

/* requested_events must be nonzero and mode must be ANY or ALL. ANY needs at
 * least one requested bit; ALL needs every requested bit. Immediate completion
 * returns the full pre-consumption pending snapshot and consumes requested bits
 * only, without touching the waiter. Otherwise initializes the waiter's node,
 * calls wait_started and appends it FIFO, returning BLOCKED with events == 0.
 * BLOCKED reports registration; the adapter/port implements actual blocking.
 */
KarqriEventWaitResult karqri_event_wait(
    KarqriEvent *event,
    KarqriEventWaiter *waiter,
    uint32_t requested_events,
    KarqriEventWaitMode mode,
    void *owner,
    const KarqriEventWaitOperations *operations);

/* ORs bits into pending; zero is allowed and adds no bits. Scans waiters FIFO,
 * skipping unsatisfied waiters. All use the same pending|signal snapshot, even
 * after earlier consumption: one signal may complete multiple matching waiters.
 * Each receives that full snapshot and consumes only its requested bits.
 */
void karqri_event_signal(KarqriEvent *event, uint32_t signaled_events);
/* Non-consuming read of pending bits. */
uint32_t karqri_event_pending(const KarqriEvent *event);

#endif
