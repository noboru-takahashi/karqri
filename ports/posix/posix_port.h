#ifndef KARQRI_POSIX_PORT_H
#define KARQRI_POSIX_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <ucontext.h>

#include "karqri/scheduler.h"
#include "karqri/task.h"

/* Initial-release port/integration contract (see README.md in this directory):
 * One owner OS thread performs all initialization, creation, execution, access
 * and teardown for an execution environment. Concurrent kernel instances,
 * foreign-thread access (including reads), and signal-handler API calls are not
 * supported. There is no locking or async dispatch; these caller obligations
 * are not generally enforced by the runtime.
 *
 * Use a fixed group of contexts: create, run all tasks to normal completion,
 * then destroy on the host side. A waiting/suspended task must still be made
 * runnable and complete before the final executable task returns. Supply a
 * driver/idle task when needed; a permanent idle task is not required.
 * Return from an entry function only with reschedule suppression false. Never
 * block/self-suspend under suppression. Restoring suppression alone does not
 * reschedule: the integration caller must arrange reselection and dispatch.
 *
 * Keep port, scheduler, tasks and context objects at stable addresses while
 * referenced. Do not reinitialize a live port or move registered objects.
 * Stop notification sources and satisfy Event/Timer lifetime contracts before
 * teardown. Execution-time cancellation/destruction and host-driven resumption
 * of an unfinished group are outside the initial-release guarantee.
 */
#define KARQRI_POSIX_MAX_TASKS 32U

typedef struct KarqriPosixPort KarqriPosixPort;

typedef struct KarqriPosixTaskContext {
    ucontext_t native_context;
    KarqriPosixPort *port;
    KarqriTask *task;
    void *stack;
    size_t stack_size;
    size_t slot;
    bool active;   /* Owns an allocated native context/stack, not CPU execution. */
    bool finished; /* Entry returned normally; resources are not yet released. */
} KarqriPosixTaskContext;

struct KarqriPosixPort {
    KarqriScheduler *scheduler;
    ucontext_t host_context;
    KarqriPosixTaskContext *contexts[KARQRI_POSIX_MAX_TASKS];
    size_t context_count;
    KarqriPosixTaskContext *executing_context;
    bool running;
};

void karqri_posix_port_init(
    KarqriPosixPort *port,
    KarqriScheduler *scheduler);

bool karqri_posix_task_context_create(
    KarqriPosixPort *port,
    KarqriPosixTaskContext *context,
    KarqriTask *task,
    size_t stack_size);
/* Only successfully created contexts, after the whole group has finished and
 * execution has returned to its owner host thread. Clears the trampoline slot
 * and frees the stack; does not remove the task from Scheduler, cancel waits,
 * stop timers, or remove the context pointer from port->contexts. Normal entry
 * return performs Scheduler removal separately. Keep context/task storage alive
 * until the old port is retired or reinitialized after all contexts are destroyed.
 * A failed create does not authorize destroy of an otherwise uninitialized object.
 */
void karqri_posix_task_context_destroy(
    KarqriPosixTaskContext *context);

KarqriTask *karqri_posix_executing_task(const KarqriPosixPort *port);
bool karqri_posix_dispatch(KarqriPosixPort *port);
bool karqri_posix_yield(KarqriPosixPort *port);
/* true reports a dispatch, not a join/all-contexts-finished result. Check every
 * context's finished flag and external references before teardown. The current
 * implementation can return to host with unfinished wait/suspend contexts, or
 * with Ready tasks under suppression; those paths are not supported completion.
 */
bool karqri_posix_run(KarqriPosixPort *port);

#endif
