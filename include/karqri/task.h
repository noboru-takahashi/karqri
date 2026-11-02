#ifndef KARQRI_TASK_H
#define KARQRI_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "karqri/queue.h"

/*
 * Task owns logical attributes, state bits and a delayed result only.
 * The caller owns its storage: initialize before use and keep its address
 * stable while referenced by a scheduler, waiter or port context. Do not move
 * or reinitialize a registered/referenced task. Keep entry and its argument
 * valid for execution; argument may be NULL when the entry permits it.
 *
 * Scheduler manages ready queues, priority ordering and execution candidates;
 * ports manage stacks, CPU contexts and actual context switches. These Task
 * operations do not update scheduler queues or dispatch. Use port APIs that
 * cooperate with the scheduler to control executing tasks; do not directly
 * mutate a registered task in ways that invalidate scheduler/port bookkeeping.
 *
 * No internal synchronization: callers must protect shared Task access,
 * including reads, against competing task/interrupt contexts. Task pointers
 * must be valid and non-NULL; misuse is not checked here.
 */
typedef void (*KarqriTaskEntry)(void *argument);

/* Historical bitmask semantics are retained: READY may coexist with
 * SUSPENDED, and RUNNING may remain on a task no longer selected. State alone
 * is not proof of current execution or ready-queue membership. SEND_WAIT,
 * RECEIVE_WAIT and JAM_WAIT bits do not imply implemented communication
 * services. is_waiting tests only KARQRI_TASK_WAIT_STATES below.
 */
typedef uint32_t KarqriTaskState;

enum {
    KARQRI_TASK_STATE_EVENT_WAIT = UINT32_C(1) << 0,
    KARQRI_TASK_STATE_RECEIVE_WAIT = UINT32_C(1) << 1,
    KARQRI_TASK_STATE_SEND_WAIT = UINT32_C(1) << 2,
    KARQRI_TASK_STATE_SUSPENDED = UINT32_C(1) << 3,
    KARQRI_TASK_STATE_READY = UINT32_C(1) << 4,
    KARQRI_TASK_STATE_RUNNING = UINT32_C(1) << 5,
    KARQRI_TASK_STATE_JAM_WAIT = UINT32_C(1) << 6
};

#define KARQRI_TASK_WAIT_STATES \
    (KARQRI_TASK_STATE_EVENT_WAIT | \
     KARQRI_TASK_STATE_RECEIVE_WAIT | \
     KARQRI_TASK_STATE_SEND_WAIT)

typedef struct KarqriTask {
    KarqriQueueNode queue_node;
    KarqriTaskEntry entry;
    void *entry_argument;
    uint32_t priority;
    KarqriTaskState state;
    uintptr_t delayed_return_value;
    bool delayed_return_ready;
} KarqriTask;

/* Initializes a detached node, logical READY state and an invalid zero result;
 * retains entry, argument and priority. Does not register or execute the task.
 */
void karqri_task_init(
    KarqriTask *task,
    KarqriTaskEntry entry,
    void *entry_argument,
    uint32_t priority);

KarqriTaskState karqri_task_state(const KarqriTask *task);
void karqri_task_state_replace(KarqriTask *task, KarqriTaskState state);
void karqri_task_state_set(KarqriTask *task, KarqriTaskState state_bits);
void karqri_task_state_clear(KarqriTask *task, KarqriTaskState state_bits);
bool karqri_task_state_has_any(
    const KarqriTask *task,
    KarqriTaskState state_bits);
bool karqri_task_state_has_all(
    const KarqriTask *task,
    KarqriTaskState state_bits);
bool karqri_task_is_waiting(const KarqriTask *task);

/* State-only suspend/resume; neither operation changes scheduler queues. */
bool karqri_task_suspend(KarqriTask *task);
bool karqri_task_resume(KarqriTask *task);

uint32_t karqri_task_priority(const KarqriTask *task);
/* Stores the uint32_t value and returns the old value; no queue reordering. */
uint32_t karqri_task_set_priority(KarqriTask *task, uint32_t priority);

/* set stores a value (including zero), replacing any prior result, and marks
 * it valid. Check has_delayed_return before using it as a completed result.
 * Reading does not consume it. clear invalidates the flag but retains the
 * stored value; a later set makes a new result valid.
 */
void karqri_task_set_delayed_return(
    KarqriTask *task,
    uintptr_t value);
bool karqri_task_has_delayed_return(const KarqriTask *task);
uintptr_t karqri_task_delayed_return(const KarqriTask *task);
void karqri_task_clear_delayed_return(KarqriTask *task);

#endif
