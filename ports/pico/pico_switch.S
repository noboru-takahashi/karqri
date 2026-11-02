#include "pico_context_offsets.h"
#include "pico_kernel_offsets.h"
#include "pico_fault.h"
#ifndef KARQRI_PICO_TRACE
#define KARQRI_PICO_TRACE 0
#endif

    .syntax unified
    .cpu cortex-m0plus
    .thumb

    .section .text.karqri_pico_switch, "ax", %progbits

    .global karqri_pico_pendsv_handler
    .type karqri_pico_pendsv_handler, %function
    .thumb_func
karqri_pico_pendsv_handler:
    /* Handler mode uses MSP. Keep the saved PRIMASK and EXC_RETURN in
     * an eight-byte slot without touching the task's R4-R11.
     */
    mrs     r2, primask
    cpsid   i
    push    {r2, lr}

    ldr     r2, =karqri_pico_pending_context
    ldr     r3, [r2]
    cmp     r3, #0
    beq     .Lpendsv_return

    /* PSP exception entry does not align MSP. The initial SVC leaves an
     * aligned main frame on MSP; check that invariant before calling C.
     */
    mov     r0, sp
    movs    r1, #7
    tst     r0, r1
    bne     .Lpendsv_bad_msp

    mrs     r0, psp
    subs    r0, #KARQRI_PICO_R0_OFFSET
    str     r4, [r0, #KARQRI_PICO_R4_OFFSET]
    str     r5, [r0, #KARQRI_PICO_R5_OFFSET]
    str     r6, [r0, #KARQRI_PICO_R6_OFFSET]
    str     r7, [r0, #KARQRI_PICO_R7_OFFSET]
    mov     r1, r8
    str     r1, [r0, #KARQRI_PICO_R8_OFFSET]
    mov     r1, r9
    str     r1, [r0, #KARQRI_PICO_R9_OFFSET]
    mov     r1, r10
    str     r1, [r0, #KARQRI_PICO_R10_OFFSET]
    mov     r1, r11
    str     r1, [r0, #KARQRI_PICO_R11_OFFSET]

    ldr     r1, =karqri_pico_active_context
    ldr     r2, [r1]
    str     r0, [r2, #KARQRI_PICO_CONTEXT_SAVED_PSP_OFFSET]
#if KARQRI_PICO_TRACE
    ldr     r0, =karqri_pico_trace_old_context
    str     r2, [r0]
#endif
    /* R3 retains the pending context sampled under PRIMASK protection. */
    ldr     r2, =karqri_pico_pending_context
    str     r3, [r1]
    movs    r1, #0
    str     r1, [r2]

    /* old->saved_psp is now current, including for a self-switch. LR is
     * saved on MSP; the helper returns next in R0, not the task's R0 value.
     */
    mov     r0, r3
    bl      karqri_pico_deliver_event_return
    ldr     r0, [r0, #KARQRI_PICO_CONTEXT_SAVED_PSP_OFFSET]
    ldr     r1, [r0, #KARQRI_PICO_R8_OFFSET]
    mov     r8, r1
    ldr     r1, [r0, #KARQRI_PICO_R9_OFFSET]
    mov     r9, r1
    ldr     r1, [r0, #KARQRI_PICO_R10_OFFSET]
    mov     r10, r1
    ldr     r1, [r0, #KARQRI_PICO_R11_OFFSET]
    mov     r11, r1
    ldr     r4, [r0, #KARQRI_PICO_R4_OFFSET]
    ldr     r5, [r0, #KARQRI_PICO_R5_OFFSET]
    ldr     r6, [r0, #KARQRI_PICO_R6_OFFSET]
    ldr     r7, [r0, #KARQRI_PICO_R7_OFFSET]
    adds    r0, #KARQRI_PICO_R0_OFFSET
    msr     psp, r0

    ldr     r0, =karqri_pico_switch_count
    ldr     r1, [r0]
    adds    r1, #1
    str     r1, [r0]

#if KARQRI_PICO_TRACE
    bl      karqri_pico_trace_commit
#endif
.Lpendsv_return:
    pop     {r2, r3}
    mov     lr, r3
    /* Handoff is complete. An IRQ may now publish a new request; do not
     * change any context, task registers or switch state after restoring.
     */
    msr     primask, r2
    bx      lr

.Lpendsv_bad_msp:
    movs    r0, #KARQRI_PICO_FAULT_MSP_ALIGNMENT
    b       .Lpico_fault
    .size karqri_pico_pendsv_handler, .-karqri_pico_pendsv_handler

    .global karqri_pico_initial_dispatch_start
    .type karqri_pico_initial_dispatch_start, %function
    .thumb_func
karqri_pico_initial_dispatch_start:
    svc     #0
    bx      lr
    .size karqri_pico_initial_dispatch_start, .-karqri_pico_initial_dispatch_start

    .global karqri_pico_svc_handler
    .type karqri_pico_svc_handler, %function
    .thumb_func
karqri_pico_svc_handler:
    mov     r2, lr
    movs    r3, #4
    tst     r2, r3
    beq     1f
    mrs     r0, psp
    b       2f
1:
    mrs     r0, msp
2:
    ldr     r1, [r0, #KARQRI_PICO_PC_OFFSET - KARQRI_PICO_R0_OFFSET]
    subs    r1, #2
    ldrb    r1, [r1]
    cmp     r1, #0
    beq     .Lsvc_start
    cmp     r1, #1
    beq     .Lsvc_return
    b       .

.Lsvc_start:
    ldr     r0, =karqri_pico_main_registers
    str     r4, [r0, #KARQRI_PICO_R4_OFFSET]
    str     r5, [r0, #KARQRI_PICO_R5_OFFSET]
    str     r6, [r0, #KARQRI_PICO_R6_OFFSET]
    str     r7, [r0, #KARQRI_PICO_R7_OFFSET]
    mov     r1, r8
    str     r1, [r0, #KARQRI_PICO_R8_OFFSET]
    mov     r1, r9
    str     r1, [r0, #KARQRI_PICO_R9_OFFSET]
    mov     r1, r10
    str     r1, [r0, #KARQRI_PICO_R10_OFFSET]
    mov     r1, r11
    str     r1, [r0, #KARQRI_PICO_R11_OFFSET]
    ldr     r0, =karqri_pico_main_exc_return
    mov     r1, lr
    str     r1, [r0]

    ldr     r0, =karqri_pico_active_context
    ldr     r0, [r0]
    ldr     r1, [r0]
    ldr     r2, [r1, #KARQRI_PICO_R8_OFFSET]
    mov     r8, r2
    ldr     r2, [r1, #KARQRI_PICO_R9_OFFSET]
    mov     r9, r2
    ldr     r2, [r1, #KARQRI_PICO_R10_OFFSET]
    mov     r10, r2
    ldr     r2, [r1, #KARQRI_PICO_R11_OFFSET]
    mov     r11, r2
    ldr     r4, [r1, #KARQRI_PICO_R4_OFFSET]
    ldr     r5, [r1, #KARQRI_PICO_R5_OFFSET]
    ldr     r6, [r1, #KARQRI_PICO_R6_OFFSET]
    ldr     r7, [r1, #KARQRI_PICO_R7_OFFSET]
    adds    r1, #KARQRI_PICO_R0_OFFSET
    msr     psp, r1
    mrs     r1, control
    movs    r2, #2
    orrs    r1, r2
    msr     control, r1
    isb
    ldr     r1, =0xfffffffd
    mov     lr, r1
    bx      lr

.Lsvc_return:
    mrs     r0, control
    movs    r1, #2
    bics    r0, r1
    msr     control, r0
    isb
    ldr     r0, =karqri_pico_main_registers
    ldr     r1, [r0, #KARQRI_PICO_R8_OFFSET]
    mov     r8, r1
    ldr     r1, [r0, #KARQRI_PICO_R9_OFFSET]
    mov     r9, r1
    ldr     r1, [r0, #KARQRI_PICO_R10_OFFSET]
    mov     r10, r1
    ldr     r1, [r0, #KARQRI_PICO_R11_OFFSET]
    mov     r11, r1
    ldr     r4, [r0, #KARQRI_PICO_R4_OFFSET]
    ldr     r5, [r0, #KARQRI_PICO_R5_OFFSET]
    ldr     r6, [r0, #KARQRI_PICO_R6_OFFSET]
    ldr     r7, [r0, #KARQRI_PICO_R7_OFFSET]
    ldr     r0, =karqri_pico_main_exc_return
    ldr     r0, [r0]
    mov     lr, r0
    bx      lr
    .size karqri_pico_svc_handler, .-karqri_pico_svc_handler

    .global karqri_pico_event_wait
    .type karqri_pico_event_wait, %function
    .thumb_func
karqri_pico_event_wait:
    push    {r4, r5, r6, lr}
    mrs     r5, primask
    cmp     r5, #0
    bne     .Lwait_bad_primask
    mrs     r1, ipsr
    cmp     r1, #0
    bne     .Lwait_bad_context
    mrs     r1, control
    movs    r2, #3
    ands    r1, r2
    cmp     r1, #2              /* privileged Thread using PSP */
    bne     .Lwait_bad_context

    cpsid   i
    mov     r4, r0
    ldr     r6, =karqri_pico_active_context
    ldr     r6, [r6]
    bl      karqri_pico_event_wait_prepare
    ldr     r1, [r4, #KARQRI_PICO_EVENT_WAIT_BLOCKED_OFFSET]
    cmp     r1, #0
    beq     .Levent_wait_immediate

    /* C has published P/H under the outer mask. Recognize eligible pending
     * exceptions before continuing, without changing the eventual R0 value.
     */
    msr     primask, r5
    isb
    ldrb    r1, [r6, #KARQRI_PICO_CONTEXT_DELAYED_RETURN_PENDING_OFFSET]
    cmp     r1, #0
    bne     .Lwait_bad_handoff  /* one diagnostic check, never a wait loop */
    pop     {r4, r5, r6, pc}

.Levent_wait_immediate:
    msr     primask, r5
    isb
    pop     {r4, r5, r6, pc}

.Lwait_bad_primask:
    movs    r0, #KARQRI_PICO_FAULT_WAIT_PRIMASK
    b       .Lpico_fault
.Lwait_bad_context:
    movs    r0, #KARQRI_PICO_FAULT_WAIT_CONTEXT
    b       .Lpico_fault
.Lwait_bad_handoff:
    movs    r0, #KARQRI_PICO_FAULT_HANDOFF
    b       .Lpico_fault
    .size karqri_pico_event_wait, .-karqri_pico_event_wait

/* No C call: also usable when MSP is not suitable for an ABI call. */
.Lpico_fault:
    ldr     r1, =karqri_pico_fault_reason
    str     r0, [r1]
    dsb
    udf     #0

    .ltorg
