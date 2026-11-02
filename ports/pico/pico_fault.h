#ifndef KARQRI_PICO_FAULT_H
#define KARQRI_PICO_FAULT_H

/* Internal diagnostics shared with assembly; also enabled in Release. */
#define KARQRI_PICO_FAULT_WAIT_PRIMASK 1
#define KARQRI_PICO_FAULT_WAIT_CONTEXT 2
#define KARQRI_PICO_FAULT_HANDOFF 3
#define KARQRI_PICO_FAULT_MSP_ALIGNMENT 4
#define KARQRI_PICO_FAULT_SUPPRESSION 5
#define KARQRI_PICO_FAULT_DELAYED_RETURN 6
#define KARQRI_PICO_FAULT_PORT_CONTEXT 7
#define KARQRI_PICO_FAULT_PORT_STATE 8
#define KARQRI_PICO_FAULT_UNFINISHED_TASKS 9
#define KARQRI_PICO_FAULT_TICK_CONTEXT 10
#define KARQRI_PICO_FAULT_TICK_MISSED 11

#ifndef __ASSEMBLER__
#include <stdint.h>

extern volatile uint32_t karqri_pico_fault_reason;
_Noreturn void karqri_pico_fault(uint32_t reason);
#endif

#endif
