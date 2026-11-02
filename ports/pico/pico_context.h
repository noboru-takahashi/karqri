#ifndef KARQRI_PICO_CONTEXT_H
#define KARQRI_PICO_CONTEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "karqri/task.h"
#include "pico_context_offsets.h"

#define KARQRI_PICO_XPSR_THUMB UINT32_C(0x01000000)
#define KARQRI_PICO_EXC_RETURN_THREAD_PSP UINT32_C(0xfffffffd)

typedef struct KarqriPicoHardwareFrame {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t xpsr;
} KarqriPicoHardwareFrame;

typedef struct KarqriPicoSoftwareFrame {
    uint32_t r4;
    uint32_t r5;
    uint32_t r6;
    uint32_t r7;
    uint32_t r8;
    uint32_t r9;
    uint32_t r10;
    uint32_t r11;
} KarqriPicoSoftwareFrame;

typedef struct KarqriPicoInitialFrame {
    KarqriPicoSoftwareFrame software;
    KarqriPicoHardwareFrame hardware;
} KarqriPicoInitialFrame;

typedef struct KarqriPicoTaskContext {
    uint32_t *saved_psp;
    KarqriTask *task;
    uint32_t *stack_low;
    uint32_t *stack_high;
    uint32_t remaining_slice_ticks;
    /* Set for a blocked wrapper; cleared only after PendSV patches its R0. */
    bool delayed_return_pending;
    bool finished;
} KarqriPicoTaskContext;

bool karqri_pico_context_build(
    KarqriPicoTaskContext *context,
    KarqriTask *task,
    uint32_t *stack_base,
    size_t stack_words,
    void (*bootstrap)(KarqriPicoTaskContext *context),
    void (*return_trap)(void));

bool karqri_pico_context_contains_psp(
    const KarqriPicoTaskContext *context);

#endif
