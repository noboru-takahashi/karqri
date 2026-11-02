#include "karqri/task.h"

void karqri_task_init(
    KarqriTask *task,
    KarqriTaskEntry entry,
    void *entry_argument,
    uint32_t priority)
{
    karqri_queue_init(&task->queue_node);
    task->entry = entry;
    task->entry_argument = entry_argument;
    task->priority = priority;
    task->state = KARQRI_TASK_STATE_READY;
    task->delayed_return_value = 0U;
    task->delayed_return_ready = false;
}

KarqriTaskState karqri_task_state(const KarqriTask *task)
{
    return task->state;
}

void karqri_task_state_replace(KarqriTask *task, KarqriTaskState state)
{
    task->state = state;
}

void karqri_task_state_set(KarqriTask *task, KarqriTaskState state_bits)
{
    task->state |= state_bits;
}

void karqri_task_state_clear(KarqriTask *task, KarqriTaskState state_bits)
{
    task->state &= ~state_bits;
}

bool karqri_task_state_has_any(
    const KarqriTask *task,
    KarqriTaskState state_bits)
{
    return (task->state & state_bits) != 0U;
}

bool karqri_task_state_has_all(
    const KarqriTask *task,
    KarqriTaskState state_bits)
{
    return (task->state & state_bits) == state_bits;
}

bool karqri_task_is_waiting(const KarqriTask *task)
{
    return karqri_task_state_has_any(task, KARQRI_TASK_WAIT_STATES);
}

bool karqri_task_suspend(KarqriTask *task)
{
    if (karqri_task_state_has_any(
            task, KARQRI_TASK_STATE_SUSPENDED)) {
        return false;
    }

    karqri_task_state_set(task, KARQRI_TASK_STATE_SUSPENDED);
    if (!karqri_task_is_waiting(task)) {
        karqri_task_state_clear(task, KARQRI_TASK_STATE_READY);
    }
    return true;
}

bool karqri_task_resume(KarqriTask *task)
{
    if (!karqri_task_state_has_any(
            task, KARQRI_TASK_STATE_SUSPENDED)) {
        return false;
    }

    karqri_task_state_clear(task, KARQRI_TASK_STATE_SUSPENDED);
    if (!karqri_task_is_waiting(task)) {
        karqri_task_state_set(task, KARQRI_TASK_STATE_READY);
    }
    return true;
}

uint32_t karqri_task_priority(const KarqriTask *task)
{
    return task->priority;
}

uint32_t karqri_task_set_priority(KarqriTask *task, uint32_t priority)
{
    const uint32_t old_priority = task->priority;

    task->priority = priority;
    return old_priority;
}

void karqri_task_set_delayed_return(
    KarqriTask *task,
    uintptr_t value)
{
    task->delayed_return_value = value;
    task->delayed_return_ready = true;
}

bool karqri_task_has_delayed_return(const KarqriTask *task)
{
    return task->delayed_return_ready;
}

uintptr_t karqri_task_delayed_return(const KarqriTask *task)
{
    return task->delayed_return_value;
}

void karqri_task_clear_delayed_return(KarqriTask *task)
{
    task->delayed_return_ready = false;
}
