#include "posix_port.h"

#include <stdlib.h>

static KarqriPosixTaskContext *trampoline_slots[
    KARQRI_POSIX_MAX_TASKS];

static KarqriPosixTaskContext *find_context(
    KarqriPosixPort *port,
    const KarqriTask *task)
{
    size_t index;

    for (index = 0U; index < port->context_count; index++) {
        KarqriPosixTaskContext *context = port->contexts[index];

        if (context->active && context->task == task) {
            return context;
        }
    }
    return NULL;
}

static size_t allocate_trampoline_slot(void)
{
    size_t slot;

    for (slot = 0U; slot < KARQRI_POSIX_MAX_TASKS; slot++) {
        if (trampoline_slots[slot] == NULL) {
            return slot;
        }
    }
    return KARQRI_POSIX_MAX_TASKS;
}

static void task_finished(KarqriPosixTaskContext *context)
{
    KarqriPosixPort *port = context->port;
    KarqriTask *next_task;
    KarqriPosixTaskContext *next_context;

    context->finished = true;
    karqri_scheduler_remove_task(port->scheduler, context->task);
    next_task = karqri_scheduler_current(port->scheduler);

    if (next_task != NULL) {
        next_context = find_context(port, next_task);
        if (next_context == NULL) {
            abort();
        }
        port->executing_context = next_context;
        setcontext(&next_context->native_context);
        abort();
    }

    port->executing_context = NULL;
    port->running = false;
    setcontext(&port->host_context);
    abort();
}

static void task_trampoline(int slot)
{
    KarqriPosixTaskContext *context;

    if (slot < 0 || (size_t)slot >= KARQRI_POSIX_MAX_TASKS) {
        abort();
    }
    context = trampoline_slots[(size_t)slot];
    if (context == NULL || !context->active) {
        abort();
    }

    context->task->entry(context->task->entry_argument);
    task_finished(context);
}

void karqri_posix_port_init(
    KarqriPosixPort *port,
    KarqriScheduler *scheduler)
{
    size_t index;

    port->scheduler = scheduler;
    for (index = 0U; index < KARQRI_POSIX_MAX_TASKS; index++) {
        port->contexts[index] = NULL;
    }
    port->context_count = 0U;
    port->executing_context = NULL;
    port->running = false;
}

bool karqri_posix_task_context_create(
    KarqriPosixPort *port,
    KarqriPosixTaskContext *context,
    KarqriTask *task,
    size_t stack_size)
{
    const size_t slot = allocate_trampoline_slot();

    if (port->context_count >= KARQRI_POSIX_MAX_TASKS ||
        slot == KARQRI_POSIX_MAX_TASKS || stack_size == 0U) {
        return false;
    }

    context->stack = malloc(stack_size);
    if (context->stack == NULL) {
        return false;
    }
    if (getcontext(&context->native_context) != 0) {
        free(context->stack);
        context->stack = NULL;
        return false;
    }

    context->port = port;
    context->task = task;
    context->stack_size = stack_size;
    context->slot = slot;
    context->active = true;
    context->finished = false;
    context->native_context.uc_stack.ss_sp = context->stack;
    context->native_context.uc_stack.ss_size = stack_size;
    context->native_context.uc_stack.ss_flags = 0;
    context->native_context.uc_link = &port->host_context;
    trampoline_slots[slot] = context;
    makecontext(
        &context->native_context,
        (void (*)(void))task_trampoline,
        1,
        (int)slot);

    port->contexts[port->context_count] = context;
    port->context_count++;
    karqri_scheduler_add_ready(port->scheduler, task);
    return true;
}

void karqri_posix_task_context_destroy(
    KarqriPosixTaskContext *context)
{
    if (!context->active) {
        return;
    }
    trampoline_slots[context->slot] = NULL;
    free(context->stack);
    context->stack = NULL;
    context->active = false;
}

KarqriTask *karqri_posix_executing_task(const KarqriPosixPort *port)
{
    if (port->executing_context == NULL) {
        return NULL;
    }
    return port->executing_context->task;
}

bool karqri_posix_dispatch(KarqriPosixPort *port)
{
    KarqriTask *target_task = karqri_scheduler_current(port->scheduler);
    KarqriPosixTaskContext *target_context;
    KarqriPosixTaskContext *previous_context;

    if (target_task == NULL) {
        return false;
    }
    target_context = find_context(port, target_task);
    if (target_context == NULL) {
        return false;
    }
    previous_context = port->executing_context;
    if (previous_context == target_context) {
        return false;
    }

    port->executing_context = target_context;
    if (previous_context == NULL) {
        port->running = true;
        if (swapcontext(
                &port->host_context,
                &target_context->native_context) != 0) {
            abort();
        }
        port->executing_context = NULL;
        port->running = false;
    } else {
        if (swapcontext(
                &previous_context->native_context,
                &target_context->native_context) != 0) {
            abort();
        }
        port->executing_context = previous_context;
    }
    return true;
}

bool karqri_posix_yield(KarqriPosixPort *port)
{
    if (!karqri_scheduler_rotate_timeslice(port->scheduler)) {
        return false;
    }
    return karqri_posix_dispatch(port);
}

bool karqri_posix_run(KarqriPosixPort *port)
{
    if (port->running) {
        return false;
    }
    return karqri_posix_dispatch(port);
}
