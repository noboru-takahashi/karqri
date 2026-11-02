#ifndef KARQRI_PICO_TRACE_H
#define KARQRI_PICO_TRACE_H

/* Private validation instrumentation, not a Kernel/application API. */
#ifndef KARQRI_PICO_TRACE
#define KARQRI_PICO_TRACE 0
#endif

#ifndef __ASSEMBLER__
#include "pico_port.h"

enum {
    KARQRI_TRACE_BOOTSTRAP = 1, KARQRI_TRACE_MARK, KARQRI_TRACE_YIELD,
    KARQRI_TRACE_SET, KARQRI_TRACE_REPLACE, KARQRI_TRACE_CANCEL,
    KARQRI_TRACE_COMMIT, KARQRI_TRACE_RETURN, KARQRI_TRACE_STOP
};
enum { KARQRI_TRACE_UNKNOWN, KARQRI_TRACE_FROM_YIELD, KARQRI_TRACE_FROM_RETURN };

#if KARQRI_PICO_TRACE
#define KARQRI_TRACE_ARG(value) , value
#define KARQRI_TRACE(event, source, subject, value) \
    karqri_pico_trace_record(event, source, (uintptr_t)(subject), value)

#define KARQRI_PICO_TRACE_CAPACITY 256U
typedef struct KarqriPicoTraceRecord {
    uint32_t seq;
    uint32_t time_us_low;
    uint32_t active_context;
    uint32_t scheduler_current;
    uint32_t pending_context;
    uint32_t subject;
    uint32_t value;
    uint16_t event;
    uint8_t source;
    uint8_t flags;
} KarqriPicoTraceRecord;

/* Only PendSV writes this scratch pointer, under PRIMASK; never a Task register.
 * It is set after saving old and used before restoring the handler's mask.
 */
extern KarqriPicoTaskContext *karqri_pico_trace_old_context;
void karqri_pico_trace_begin(KarqriPicoPort *port);
void karqri_pico_trace_freeze(void);
void karqri_pico_trace_dump(const char *labels);
void karqri_pico_trace_record(uint16_t event, uint8_t source,
                              uintptr_t subject, uint32_t value);
void karqri_pico_trace_commit(void);
#else
#define KARQRI_TRACE_ARG(value)
#define KARQRI_TRACE(event, source, subject, value) ((void)0)
#define karqri_pico_trace_begin(port) ((void)0)
#define karqri_pico_trace_freeze() ((void)0)
#define karqri_pico_trace_dump(labels) ((void)0)
#endif
#endif
#endif
