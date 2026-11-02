#ifndef KARQRI_POSIX_KERNEL_H
#define KARQRI_POSIX_KERNEL_H

#include <stdbool.h>
#include <stdint.h>

#include "karqri/event.h"
#include "karqri/scheduler_event.h"
#include "karqri/task.h"
#include "karqri/timer.h"
#include "posix_port.h"

/* Owner-thread, synchronous integration wrappers; posix_port.h lifetime and
 * execution restrictions apply. No foreign-thread or signal-handler access,
 * even for notification/tick/read operations. Task-side shared primitives are
 * not made thread-safe by these wrappers. Callbacks run synchronously: do not
 * reenter an Event/Timer operation in a way that invalidates its traversal, or
 * dispatch before the enclosing operation finishes. In particular, Timer expiry
 * adapters may update Event/Scheduler state, but this timer_tick wrapper performs
 * the CPU dispatch only after the entire Timer tick and its callbacks return.
 */
/* Wait only from an executing Task, leaving another runnable context, with
 * suppression false. Unsupported waits are not guaranteed a diagnostic here.
 */
uintptr_t karqri_posix_wait_event(
    KarqriPosixPort *port,
    KarqriScheduledEventWaiter *waiter,
    KarqriEvent *event,
    uint32_t requested_events,
    KarqriEventWaitMode mode);

void karqri_posix_signal_event(
    KarqriPosixPort *port,
    KarqriEvent *event,
    uint32_t events);
void karqri_posix_timer_tick(
    KarqriPosixPort *port,
    KarqriTimerQueue *timer_queue);

bool karqri_posix_suspend_task(
    KarqriPosixPort *port,
    KarqriTask *task);
bool karqri_posix_resume_task(
    KarqriPosixPort *port,
    KarqriTask *task);

#endif
