#ifndef KARQRI_TRACK_DEMO_H
#define KARQRI_TRACK_DEMO_H

/* Demo-private workload/backend boundary; not a Kernel or port API. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "karqri/event.h"
#include "karqri/scheduler.h"
#include "karqri/scheduler_event.h"
#include "karqri/task.h"
#include "karqri/timer.h"

#define TASK_COUNT 6U
#define TRACK_COUNT 3U
#define WIDTH 42
#define HEIGHT 26
#define TICK_PERIOD_US UINT32_C(25000)
#define TICK_LIMIT UINT32_C(6000)
#define TASK_PRIORITY UINT32_C(1)
#define CRITICAL_WIDTH 13
#define CRITICAL_TOP_ROW 3
#define CRITICAL_BOTTOM_ROW 9
#define EVENT_GATE UINT32_C(0x01)

#ifndef TRACK_DEMO_COOPERATIVE
#define TRACK_DEMO_COOPERATIVE 0
#endif

typedef struct DemoTask {
    char label;
    int track;
    volatile int position;
    int speed;
    volatile int delay;
    volatile int x;
    volatile int y;
    volatile uint32_t executions;
    volatile uint32_t moves;
    volatile bool in_critical;
    volatile uintptr_t local_address;
    KarqriScheduledEventWaiter gate_waiter;
} DemoTask;

typedef struct TrackDemo {
    KarqriScheduler scheduler;
    KarqriTimerQueue timer_queue;
    KarqriTimer unused_timer;
    KarqriEvent gate_event;
    KarqriTask tasks[TASK_COUNT];
    DemoTask movers[TASK_COUNT];
    volatile int critical_owner;
    volatile uint32_t critical_entries;
    volatile uint32_t critical_releases;
    volatile uint32_t critical_waits;
    volatile uint32_t overlap_errors;
} TrackDemo;

extern TrackDemo demo;
extern volatile uint32_t *const track_demo_tick_count;
extern const char track_demo_title[];
extern const char track_demo_scheduling[];
extern const char track_demo_stacks[];

void track_demo_initialize(void);
void track_demo_task(void *argument);
void track_demo_draw(void);
void track_demo_render(size_t running_index);
bool track_demo_group_quiescent(void);
bool track_demo_results_valid(void);
void track_demo_summary(bool passed);

void track_demo_wait_gate(DemoTask *mover);
void track_demo_signal_gate(void);
bool track_demo_suppress(bool suppressed);
void track_demo_metrics(void);
void track_demo_summary_metrics(void);
void track_demo_properties(void);
bool track_demo_backend_valid(void);
#if TRACK_DEMO_COOPERATIVE
void track_demo_checkpoint(void);
#endif

#endif
