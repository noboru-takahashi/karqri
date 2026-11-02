#ifndef KARQRI_PICO_INTERRUPT_H
#define KARQRI_PICO_INTERRUPT_H

#include <stdint.h>

/* Internal, calling-core protection only. These helpers do not arrange a
 * context switch. Restore the matching saved value, including when nested;
 * do not wait for SVC/PendSV while interrupts remain masked.
 */
static inline uint32_t karqri_pico_interrupt_save_and_disable(void)
{
    uint32_t primask;

    __asm volatile("mrs %0, primask\ncpsid i"
                   : "=r"(primask) :: "memory");
    return primask;
}

static inline void karqri_pico_interrupt_restore(uint32_t primask)
{
    __asm volatile("msr primask, %0" :: "r"(primask) : "memory");
}

#endif
