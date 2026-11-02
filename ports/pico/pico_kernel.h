#ifndef KARQRI_PICO_KERNEL_H
#define KARQRI_PICO_KERNEL_H

#include <stdbool.h>
#include <stdint.h>

#include "karqri/event.h"
#include "karqri/scheduler_event.h"
#include "karqri/task.h"
#include "pico_port.h"

typedef struct KarqriPicoEventWaitCall {
    KarqriPicoPort *port;
    KarqriScheduledEventWaiter *waiter;
    KarqriEvent *event;
    uint32_t requested_events;
    KarqriEventWaitMode mode;
    uint32_t blocked;
} KarqriPicoEventWaitCall;

/* Synchronous wait from a privileged, started Thread/PSP context with entry
 * PRIMASK == 0. Blocking requires no suppression and another runnable context.
 * Unsupported handoffs diagnose in Release too; they do not return a false 0.
 */
uint32_t karqri_pico_event_wait(KarqriPicoEventWaitCall *call);
/* Assembly-only helper; the wrapper holds PRIMASK through request publication. */
uint32_t karqri_pico_event_wait_prepare(KarqriPicoEventWaitCall *call);

void karqri_pico_event_signal(
    KarqriPicoPort *port,
    KarqriEvent *event,
    uint32_t events);
bool karqri_pico_suspend_task(
    KarqriPicoPort *port,
    KarqriTask *task);
bool karqri_pico_resume_task(
    KarqriPicoPort *port,
    KarqriTask *task);

#endif
