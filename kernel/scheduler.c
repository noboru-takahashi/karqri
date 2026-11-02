#include "karqri/scheduler.h"

#include <stddef.h>

#define KARQRI_CONTAINER_OF(pointer, type, member) \
    ((type *)((unsigned char *)(pointer) - offsetof(type, member)))

static bool task_is_linked(const KarqriTask *task)
{
    return task->queue_node.next != &task->queue_node;
}

static KarqriTask *task_from_node(KarqriQueueNode *node)
{
    return KARQRI_CONTAINER_OF(node, KarqriTask, queue_node);
}

static void insert_ready_fifo(
    KarqriScheduler *scheduler,
    KarqriTask *task)
{
    KarqriQueueNode *position = scheduler->ready_tasks.next;

    while (position != &scheduler->ready_tasks &&
           task_from_node(position)->priority >= task->priority) {
        position = position->next;
    }
    karqri_queue_insert_before(position, &task->queue_node);
}

void karqri_scheduler_init(KarqriScheduler *scheduler)
{
    karqri_queue_init(&scheduler->ready_tasks);
    karqri_queue_init(&scheduler->suspended_tasks);
    scheduler->current_task = NULL;
    scheduler->reschedule_suppressed = false;
}

KarqriTask *karqri_scheduler_current(const KarqriScheduler *scheduler)
{
    return scheduler->current_task;
}

KarqriTask *karqri_scheduler_candidate(const KarqriScheduler *scheduler)
{
    if (karqri_queue_is_empty(&scheduler->ready_tasks)) {
        return NULL;
    }
    return task_from_node(scheduler->ready_tasks.next);
}

bool karqri_scheduler_reschedule(KarqriScheduler *scheduler)
{
    KarqriTask *candidate;

    if (scheduler->reschedule_suppressed) {
        return false;
    }

    candidate = karqri_scheduler_candidate(scheduler);
    if (candidate == scheduler->current_task) {
        return false;
    }

    scheduler->current_task = candidate;
    if (candidate != NULL) {
        karqri_task_state_replace(
            candidate, KARQRI_TASK_STATE_RUNNING);
    }
    return true;
}

bool karqri_scheduler_set_reschedule_suppressed(
    KarqriScheduler *scheduler,
    bool suppressed)
{
    const bool previous = scheduler->reschedule_suppressed;

    scheduler->reschedule_suppressed = suppressed;
    return previous;
}

void karqri_scheduler_add_ready(
    KarqriScheduler *scheduler,
    KarqriTask *task)
{
    if (task_is_linked(task)) {
        karqri_queue_remove(&task->queue_node);
    }
    insert_ready_fifo(scheduler, task);
    karqri_task_state_set(task, KARQRI_TASK_STATE_READY);
    (void)karqri_scheduler_reschedule(scheduler);
}

void karqri_scheduler_begin_wait(
    KarqriScheduler *scheduler,
    KarqriTask *task,
    KarqriTaskState wait_state)
{
    karqri_task_state_replace(task, wait_state);
    if (task_is_linked(task)) {
        karqri_queue_remove(&task->queue_node);
    }
    (void)karqri_scheduler_reschedule(scheduler);
}

void karqri_scheduler_ready(
    KarqriScheduler *scheduler,
    KarqriTask *task,
    KarqriTaskState completed_wait_state)
{
    karqri_task_state_clear(task, completed_wait_state);
    if (!karqri_task_is_waiting(task)) {
        if (task_is_linked(task)) {
            karqri_queue_remove(&task->queue_node);
        }
        insert_ready_fifo(scheduler, task);
        karqri_task_state_set(task, KARQRI_TASK_STATE_READY);
    }
    (void)karqri_scheduler_reschedule(scheduler);
}

bool karqri_scheduler_suspend(
    KarqriScheduler *scheduler,
    KarqriTask *task)
{
    if (task == NULL) {
        task = scheduler->current_task;
    }
    if (task == NULL || karqri_task_state_has_any(
            task, KARQRI_TASK_STATE_SUSPENDED)) {
        return false;
    }

    karqri_task_state_set(task, KARQRI_TASK_STATE_SUSPENDED);
    if (!karqri_task_is_waiting(task)) {
        karqri_task_state_clear(task, KARQRI_TASK_STATE_READY);
        if (task_is_linked(task)) {
            karqri_queue_remove(&task->queue_node);
        }
        karqri_queue_push_back(
            &scheduler->suspended_tasks, &task->queue_node);
    }
    (void)karqri_scheduler_reschedule(scheduler);
    return true;
}

bool karqri_scheduler_resume(
    KarqriScheduler *scheduler,
    KarqriTask *task)
{
    if (task == NULL || !karqri_task_state_has_any(
            task, KARQRI_TASK_STATE_SUSPENDED)) {
        return false;
    }

    karqri_scheduler_ready(
        scheduler, task, KARQRI_TASK_STATE_SUSPENDED);
    return true;
}

uint32_t karqri_scheduler_set_priority(
    KarqriScheduler *scheduler,
    KarqriTask *task,
    uint32_t priority)
{
    uint32_t previous;

    if (task == NULL) {
        task = scheduler->current_task;
    }
    if (karqri_task_priority(task) == priority) {
        return priority;
    }
    previous = karqri_task_set_priority(task, priority);
    karqri_scheduler_ready(scheduler, task, 0U);
    return previous;
}

bool karqri_scheduler_rotate_timeslice(KarqriScheduler *scheduler)
{
    KarqriTask *task = scheduler->current_task;

    if (task == NULL || !task_is_linked(task)) {
        return false;
    }

    return karqri_scheduler_rotate_task_timeslice(scheduler, task);
}

bool karqri_scheduler_rotate_task_timeslice(
    KarqriScheduler *scheduler,
    KarqriTask *task)
{
    KarqriQueueNode *position;

    if (task == NULL || !task_is_linked(task)) {
        return false;
    }

    karqri_queue_remove(&task->queue_node);
    position = scheduler->ready_tasks.next;
    while (position != &scheduler->ready_tasks &&
           task_from_node(position)->priority >= task->priority) {
        position = position->next;
    }
    karqri_queue_insert_before(position, &task->queue_node);
    return karqri_scheduler_reschedule(scheduler);
}

void karqri_scheduler_remove_task(
    KarqriScheduler *scheduler,
    KarqriTask *task)
{
    if (task_is_linked(task)) {
        karqri_queue_remove(&task->queue_node);
    }
    if (scheduler->current_task == task) {
        scheduler->current_task = NULL;
    }
    karqri_task_state_replace(task, 0U);
    (void)karqri_scheduler_reschedule(scheduler);
}
