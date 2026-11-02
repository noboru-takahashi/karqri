#ifndef KARQRI_PICO_CONTEXT_OFFSETS_H
#define KARQRI_PICO_CONTEXT_OFFSETS_H

/* KarqriPicoTaskContext offsets, distinct from the saved frame below.
 * Checked against the C layout in pico_context.c. The delayed-return flag
 * occupies one byte and must be accessed with byte loads/stores.
 */
#define KARQRI_PICO_CONTEXT_SAVED_PSP_OFFSET               0
#define KARQRI_PICO_CONTEXT_DELAYED_RETURN_PENDING_OFFSET 20

#define KARQRI_PICO_R4_OFFSET     0
#define KARQRI_PICO_R5_OFFSET     4
#define KARQRI_PICO_R6_OFFSET     8
#define KARQRI_PICO_R7_OFFSET    12
#define KARQRI_PICO_R8_OFFSET    16
#define KARQRI_PICO_R9_OFFSET    20
#define KARQRI_PICO_R10_OFFSET   24
#define KARQRI_PICO_R11_OFFSET   28
#define KARQRI_PICO_R0_OFFSET    32
#define KARQRI_PICO_R1_OFFSET    36
#define KARQRI_PICO_R2_OFFSET    40
#define KARQRI_PICO_R3_OFFSET    44
#define KARQRI_PICO_R12_OFFSET   48
#define KARQRI_PICO_LR_OFFSET    52
#define KARQRI_PICO_PC_OFFSET    56
#define KARQRI_PICO_XPSR_OFFSET  60
#define KARQRI_PICO_FRAME_SIZE   64

#endif
