#ifndef KARQRI_SCHEDULER_EVENT_H
#define KARQRI_SCHEDULER_EVENT_H

#include <stdint.h>

#include "karqri/event.h"
#include "karqri/scheduler.h"
#include "karqri/task.h"

/*
 * Internal/integration adapter; Event and Scheduler contracts also apply.
 * All pointers must be valid and non-NULL. task must be a ready-queue task
 * belonging to scheduler, with at most one active Event wait registration.
 * The caller owns the waiter; keep it, task, scheduler and event alive at stable
 * addresses through registration and completion. Reuse only after completion
 * and detachment. Mid-wait removal and deletion of a waiting task are outside
 * the initial-release contract; no cancellation API is provided here.
 *
 * Immediate completion preserves task state and any valid delayed result.
 * Blocking clears the delayed-result valid flag (not its stored value), replaces
 * task state with EVENT_WAIT and removes it from Ready. Completion stores the
 * full event snapshot as a valid delayed result, then clears EVENT_WAIT through
 * scheduler_ready. Historical state semantics and equal-priority Ready FIFO
 * remain unchanged, including READY|SUSPENDED on a suspended waiter's completion.
 *
 * Ports must protect the whole registration/Scheduler update/context-switch
 * handoff against competing task/interrupt access and arrange actual dispatch.
 * An executing task that blocks must leave another executable task with a port
 * context; do not block under reschedule suppression (R5-05). R6-01 implemented
 * Pico wait-registration serialization, protected switch requests/PendSV
 * handoff, and RUNNING-gated tick lifecycle. Special-path hardware verification
 * remains tracked in R7; normal-path PASS does not cover every interleaving.
 * Direct shared primitive access still requires caller/port serialization.
 */
typedef struct KarqriScheduledEventWaiter {
    KarqriEventWaiter event_waiter;
    KarqriScheduler *scheduler;
    KarqriTask *task;
} KarqriScheduledEventWaiter;

KarqriEventWaitResult karqri_scheduler_wait_event(
    KarqriScheduler *scheduler,
    KarqriTask *task,
    KarqriScheduledEventWaiter *waiter,
    KarqriEvent *event,
    uint32_t requested_events,
    KarqriEventWaitMode mode);

#endif
