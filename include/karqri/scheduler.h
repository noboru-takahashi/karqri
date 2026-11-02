#ifndef KARQRI_SCHEDULER_H
#define KARQRI_SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

#include "karqri/queue.h"
#include "karqri/task.h"

/*
 * Internal/integration API; initially one instance per execution environment.
 * The caller owns the scheduler and tasks. Initialize before use; keep their
 * addresses stable and storage alive while linked or referenced by a waiter
 * or port. A registered task belongs to one scheduler only; do not directly
 * mutate its node, state or priority in ways that invalidate that ownership.
 * Task state alone does not establish queue membership or actual execution.
 *
 * Ready contains execution candidates, including the selected task, in
 * descending uint32_t priority and equal-priority FIFO order. Suspended holds
 * non-waiting tasks moved there by suspend, not every task with SUSPENDED set.
 * Historical stale RUNNING and READY|SUSPENDED semantics remain intentional.
 *
 * No allocation, CPU context switching or synchronization is provided here.
 * Ports must serialize shared reads/updates against tasks and interrupts,
 * including the complete event-wait registration and context-switch handoff.
 * Reschedule suppression is not interrupt protection. When a task blocks or
 * self-suspends, another executable task with a port context must remain;
 * the caller supplies an idle task if needed. All tasks may return normally.
 * Do not block/self-suspend under suppression: the port must be able to switch.
 * Logical state tests without executing CPU contexts may suppress selection
 * while establishing wait states; they do not demonstrate safe port blocking.
 *
 * Scheduler pointers must be valid and non-NULL. Task arguments must be valid,
 * initialized and owned by this scheduler, except initial add_ready and the
 * explicitly documented NULL cases. Misuse is not checked. Combinations such
 * as priority changes while waiting/suspended are outside this contract.
 */
typedef struct KarqriScheduler {
    KarqriQueueNode ready_tasks;
    KarqriQueueNode suspended_tasks;
    KarqriTask *current_task;
    bool reschedule_suppressed;
} KarqriScheduler;

/* Initialize an unused instance; do not reinitialize while still referenced. */
void karqri_scheduler_init(KarqriScheduler *scheduler);

/* current is the last scheduler selection, not necessarily the executing CPU
 * context; candidate is the ready head, or NULL. They may differ under
 * suppression. reschedule selects candidate unless suppressed, replaces a
 * newly selected task's state with RUNNING, and reports a selection change.
 * It does not rewrite the old task's state or perform a context switch.
 */
KarqriTask *karqri_scheduler_current(const KarqriScheduler *scheduler);
KarqriTask *karqri_scheduler_candidate(const KarqriScheduler *scheduler);
bool karqri_scheduler_reschedule(KarqriScheduler *scheduler);

/* Returns the previous bool (not a nesting count). Queue updates continue;
 * clearing suppression does not reschedule. The integration caller must
 * reschedule and arrange the actual switch after restoring the previous mode.
 */
bool karqri_scheduler_set_reschedule_suppressed(
    KarqriScheduler *scheduler,
    bool suppressed);

/* Register an initialized, detached task, or requeue an existing ready task.
 * Inserts at its equal-priority group's tail and requests rescheduling.
 * Requeueing is not an idempotent registration operation.
 */
void karqri_scheduler_add_ready(
    KarqriScheduler *scheduler,
    KarqriTask *task);
/* Start a wait on a ready task. wait_state must describe the intended wait;
 * it replaces all state bits. The adapter owns waiter registration/lifetime
 * and must serialize it with this removal from Ready and rescheduling.
 */
void karqri_scheduler_begin_wait(
    KarqriScheduler *scheduler,
    KarqriTask *task,
    KarqriTaskState wait_state);
/* Complete the specified wait/suspend bits on a registered task. Remaining
 * wait bits keep it waiting; otherwise requeue at the equal-priority tail.
 * SUSPENDED is not a wait bit: historical READY|SUSPENDED is retained, and
 * candidate selection does not filter state bits. Requests rescheduling.
 */
void karqri_scheduler_ready(
    KarqriScheduler *scheduler,
    KarqriTask *task,
    KarqriTaskState completed_wait_state);

/* NULL means current; false if no current or already suspended. A waiting
 * task retains its wait registration; a non-waiting task moves to Suspended.
 */
bool karqri_scheduler_suspend(
    KarqriScheduler *scheduler,
    KarqriTask *task);
/* NULL or not suspended returns false. Clears SUSPENDED; an outstanding wait
 * continues. Otherwise requeues through ready(), requesting rescheduling.
 */
bool karqri_scheduler_resume(
    KarqriScheduler *scheduler,
    KarqriTask *task);

/* Ready tasks only; NULL requires an existing current. Returns old priority.
 * Same value is a no-op, including queue position and selection. A changed
 * value requeues at the new group's tail and requests rescheduling.
 */
uint32_t karqri_scheduler_set_priority(
    KarqriScheduler *scheduler,
    KarqriTask *task,
    uint32_t priority);

/* Rotate current, or an explicitly supplied ready-queue task (e.g. interrupted
 * task), to its group's tail. NULL/detached targets return false. The explicit
 * target must not belong to Suspended or any other queue. Returns whether
 * selection changed, not whether queue order changed; no peer may mean false.
 */
bool karqri_scheduler_rotate_timeslice(KarqriScheduler *scheduler);
bool karqri_scheduler_rotate_task_timeslice(
    KarqriScheduler *scheduler,
    KarqriTask *task);
/* Remove a registered task, clear its state and reschedule; current is cleared
 * first if it is the target. External lifetime obligations must already be met.
 * In particular, an Event wait must complete normally before removal; this API
 * does not permit manual detachment of a registered Event waiter or deletion
 * of a waiting task. It provides no waiter cancellation, automatic task cleanup,
 * port-context destruction, storage release or delayed-result clearing. The
 * port handles execution termination/final-task return and context lifetime.
 */
void karqri_scheduler_remove_task(
    KarqriScheduler *scheduler,
    KarqriTask *task);

#endif
