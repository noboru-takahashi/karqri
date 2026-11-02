#include "pico_context.h"

#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(KarqriPicoHardwareFrame) == 8U * sizeof(uint32_t),
               "RP2040 hardware frame must contain eight words");
_Static_assert(sizeof(KarqriPicoSoftwareFrame) == 8U * sizeof(uint32_t),
               "RP2040 software frame must contain R4-R11");
_Static_assert(sizeof(KarqriPicoInitialFrame) == KARQRI_PICO_FRAME_SIZE,
               "RP2040 initial frame size mismatch");
_Static_assert(sizeof(void *) == sizeof(uint32_t),
               "RP2040 context assembly requires 32-bit pointers");
_Static_assert(offsetof(KarqriPicoTaskContext, saved_psp) ==
                   KARQRI_PICO_CONTEXT_SAVED_PSP_OFFSET,
               "RP2040 context saved_psp offset mismatch");
_Static_assert(KARQRI_PICO_CONTEXT_SAVED_PSP_OFFSET == 0U,
               "PendSV requires saved_psp at context offset zero");
_Static_assert(sizeof(((KarqriPicoTaskContext *)0)->saved_psp) ==
                   sizeof(uint32_t),
               "PendSV requires a one-word saved_psp");
_Static_assert(offsetof(KarqriPicoTaskContext, delayed_return_pending) ==
                   KARQRI_PICO_CONTEXT_DELAYED_RETURN_PENDING_OFFSET,
               "RP2040 context delayed-return flag offset mismatch");
_Static_assert(sizeof(((KarqriPicoTaskContext *)0)->delayed_return_pending) == 1U,
               "RP2040 context delayed-return flag requires byte access");

#define CHECK_FRAME_OFFSET(member, expected) \
    _Static_assert(offsetof(KarqriPicoInitialFrame, member) == (expected), \
                   "RP2040 frame offset mismatch: " #member)
CHECK_FRAME_OFFSET(software.r4, KARQRI_PICO_R4_OFFSET);
CHECK_FRAME_OFFSET(software.r5, KARQRI_PICO_R5_OFFSET);
CHECK_FRAME_OFFSET(software.r6, KARQRI_PICO_R6_OFFSET);
CHECK_FRAME_OFFSET(software.r7, KARQRI_PICO_R7_OFFSET);
CHECK_FRAME_OFFSET(software.r8, KARQRI_PICO_R8_OFFSET);
CHECK_FRAME_OFFSET(software.r9, KARQRI_PICO_R9_OFFSET);
CHECK_FRAME_OFFSET(software.r10, KARQRI_PICO_R10_OFFSET);
CHECK_FRAME_OFFSET(software.r11, KARQRI_PICO_R11_OFFSET);
CHECK_FRAME_OFFSET(hardware.r0, KARQRI_PICO_R0_OFFSET);
CHECK_FRAME_OFFSET(hardware.r1, KARQRI_PICO_R1_OFFSET);
CHECK_FRAME_OFFSET(hardware.r2, KARQRI_PICO_R2_OFFSET);
CHECK_FRAME_OFFSET(hardware.r3, KARQRI_PICO_R3_OFFSET);
CHECK_FRAME_OFFSET(hardware.r12, KARQRI_PICO_R12_OFFSET);
CHECK_FRAME_OFFSET(hardware.lr, KARQRI_PICO_LR_OFFSET);
CHECK_FRAME_OFFSET(hardware.pc, KARQRI_PICO_PC_OFFSET);
CHECK_FRAME_OFFSET(hardware.xpsr, KARQRI_PICO_XPSR_OFFSET);

static uint32_t thumb_code_address(uintptr_t address)
{
    return (uint32_t)(address & ~(uintptr_t)1U);
}

bool karqri_pico_context_build(
    KarqriPicoTaskContext *context,
    KarqriTask *task,
    uint32_t *stack_base,
    size_t stack_words,
    void (*bootstrap)(KarqriPicoTaskContext *context),
    void (*return_trap)(void))
{
    uintptr_t bottom;
    uintptr_t top;
    KarqriPicoInitialFrame *frame;

    if (context == NULL || task == NULL || stack_base == NULL ||
        bootstrap == NULL || return_trap == NULL) {
        return false;
    }

    bottom = (uintptr_t)stack_base;
    top = bottom + stack_words * sizeof(uint32_t);
    top &= ~(uintptr_t)7U;
    if (top < bottom || top - bottom < sizeof(*frame)) {
        return false;
    }

    frame = (KarqriPicoInitialFrame *)(top - sizeof(*frame));
    frame->software.r4 = 0U;
    frame->software.r5 = 0U;
    frame->software.r6 = 0U;
    frame->software.r7 = 0U;
    frame->software.r8 = 0U;
    frame->software.r9 = 0U;
    frame->software.r10 = 0U;
    frame->software.r11 = 0U;
    frame->hardware.r0 = (uint32_t)(uintptr_t)context;
    frame->hardware.r1 = 0U;
    frame->hardware.r2 = 0U;
    frame->hardware.r3 = 0U;
    frame->hardware.r12 = 0U;
    frame->hardware.lr = (uint32_t)((uintptr_t)return_trap | 1U);
    frame->hardware.pc = thumb_code_address((uintptr_t)bootstrap);
    frame->hardware.xpsr = KARQRI_PICO_XPSR_THUMB;

    context->saved_psp = (uint32_t *)frame;
    context->task = task;
    context->stack_low = stack_base;
    context->stack_high = (uint32_t *)top;
    context->remaining_slice_ticks = 0U;
    context->delayed_return_pending = false;
    context->finished = false;
    return true;
}

bool karqri_pico_context_contains_psp(
    const KarqriPicoTaskContext *context)
{
    return context->saved_psp >= context->stack_low &&
           context->saved_psp < context->stack_high &&
           ((uintptr_t)context->saved_psp & (uintptr_t)7U) == 0U;
}
