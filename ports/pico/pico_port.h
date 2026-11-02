#ifndef KARQRI_PICO_PORT_H
#define KARQRI_PICO_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "karqri/scheduler.h"
#include "karqri/task.h"
#include "pico_context.h"
#include "pico_fault.h"
#include "pico_interrupt.h"

#define KARQRI_PICO_MAX_TASKS 16U

typedef struct KarqriPicoPort {
    KarqriScheduler *scheduler;
    KarqriPicoTaskContext *contexts[KARQRI_PICO_MAX_TASKS];
    size_t context_count;
    uint32_t slice_ticks;
    uint32_t core_num;
    /* RUNNING only: false through initial SVC and before final SVC. */
    bool started;
} KarqriPicoPort;

extern KarqriPicoTaskContext *karqri_pico_active_context;
extern KarqriPicoTaskContext *karqri_pico_pending_context;
extern volatile uint32_t karqri_pico_switch_count;

void karqri_pico_port_init(
    KarqriPicoPort *port,
    KarqriScheduler *scheduler,
    uint32_t slice_ticks);
bool karqri_pico_task_context_create(
    KarqriPicoPort *port,
    KarqriPicoTaskContext *context,
    KarqriTask *task,
    uint32_t *stack_base,
    size_t stack_words);

bool karqri_pico_start(KarqriPicoPort *port);
/* Internal guard for port wrappers; caller holds PRIMASK. */
void karqri_pico_require_running(KarqriPicoPort *port);
/* Serializes selection and the complete pending/ICSR update. Callers that
 * mutate shared kernel state must protect the enclosing operation as well.
 */
bool karqri_pico_request_switch(KarqriPicoPort *port);
/* Assembly-only handoff helper: PRIMASK is set and old->saved_psp is current.
 * Returns the same next pointer; never schedules or invokes callbacks.
 */
KarqriPicoTaskContext *karqri_pico_deliver_event_return(
    KarqriPicoTaskContext *next);
bool karqri_pico_yield(KarqriPicoPort *port);
KarqriTask *karqri_pico_executing_task(void);

bool karqri_pico_set_reschedule_suppressed(
    KarqriPicoPort *port,
    bool suppressed);

void karqri_pico_task_bootstrap(KarqriPicoTaskContext *context);
void karqri_pico_task_return_trap(void);

#endif
