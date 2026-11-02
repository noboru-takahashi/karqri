#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "karqri/event.h"
#include "karqri/scheduler.h"
#include "karqri/scheduler_event.h"
#include "karqri/task.h"
#include "karqri/timer.h"
#include "karqri/timer_event.h"
#include "pico/stdlib.h"
#include "pico_kernel.h"
#include "pico_port.h"
#include "pico_tick.h"
#include "pico_trace.h"

#define STACK_WORDS 512U
#define GUARD_WORDS 4U
#define GUARD_VALUE UINT32_C(0xa55aa55a)
#define MAX_TRACE 64U

typedef struct TaskStack {
    uint32_t guard_low[GUARD_WORDS];
    _Alignas(8) uint32_t words[STACK_WORDS];
    uint32_t guard_high[GUARD_WORDS];
} TaskStack;

typedef struct Trace {
    volatile char entries[MAX_TRACE];
    volatile uint32_t count;
} Trace;

static uint32_t failures;

static void check(bool condition, const char *message)
{
    if (!condition) {
        printf("PICO_FAIL %s\n", message);
        failures++;
    }
}

static void initialize_stack(TaskStack *stack)
{
    size_t index;

    for (index = 0U; index < GUARD_WORDS; index++) {
        stack->guard_low[index] = GUARD_VALUE;
        stack->guard_high[index] = GUARD_VALUE;
    }
    for (index = 0U; index < STACK_WORDS; index++) {
        stack->words[index] = UINT32_C(0xcccccccc);
    }
}

static bool stack_guards_intact(const TaskStack *stack)
{
    size_t index;

    for (index = 0U; index < GUARD_WORDS; index++) {
        if (stack->guard_low[index] != GUARD_VALUE ||
            stack->guard_high[index] != GUARD_VALUE) {
            return false;
        }
    }
    return true;
}

static void trace_append(Trace *trace, char value)
{
    const uint32_t index = trace->count;

    if (index < MAX_TRACE) {
        trace->entries[index] = value;
        trace->count = index + 1U;
    }
}

static bool scheduler_empty(const KarqriScheduler *scheduler)
{
    return scheduler->current_task == NULL &&
           karqri_queue_is_empty(&scheduler->ready_tasks) &&
           karqri_queue_is_empty(&scheduler->suspended_tasks);
}

static volatile uint32_t single_task_calls;

static void single_task_entry(void *argument)
{
    volatile uint32_t local = UINT32_C(0x12345678);

    check(argument == (void *)&single_task_calls,
          "single task argument");
    check(local == UINT32_C(0x12345678), "single task local");
    single_task_calls++;
}

static void validate_single_task(void)
{
    KarqriScheduler scheduler;
    KarqriPicoPort port;
    KarqriTask task;
    KarqriPicoTaskContext context;
    static TaskStack stack;

    printf("PICO_PHASE single_task\n");
    single_task_calls = 0U;
    initialize_stack(&stack);
    karqri_scheduler_init(&scheduler);
    karqri_pico_port_init(&port, &scheduler, 0U);
    karqri_task_init(
        &task, single_task_entry, (void *)&single_task_calls, 10U);
    check(karqri_pico_task_context_create(
              &port, &context, &task, stack.words, STACK_WORDS),
          "single context create");
    check(karqri_pico_context_contains_psp(&context),
          "single initial PSP");
    check(karqri_pico_start(&port), "single initial dispatch");
    check(single_task_calls == 1U, "single task entry count");
    check(context.finished, "single task return");
    check(scheduler_empty(&scheduler), "single scheduler empty");
    check(stack_guards_intact(&stack), "single stack guard");
}

typedef struct CooperativeArgument {
    KarqriPicoPort *port;
    Trace *trace;
    char label;
    volatile uint32_t local_address;
    volatile uint32_t rounds;
} CooperativeArgument;

static void cooperative_task_entry(void *argument)
{
    CooperativeArgument *task = argument;
    uint32_t round;

    task->local_address = (uint32_t)(uintptr_t)&round;
    for (round = 0U; round < 3U; round++) {
        trace_append(task->trace, (char)(task->label + (char)round));
        KARQRI_TRACE(KARQRI_TRACE_MARK, KARQRI_TRACE_UNKNOWN, NULL,
                     (uint32_t)(task->label + (char)round));
        task->rounds = round + 1U;
        if (round < 2U) {
            check(karqri_pico_yield(task->port), "cooperative yield");
            check(task->rounds == round + 1U, "cooperative local state");
        }
    }
}

static void validate_cooperative_tasks(void)
{
    KarqriScheduler scheduler;
    KarqriPicoPort port;
    KarqriTask tasks[3];
    KarqriPicoTaskContext contexts[3];
    CooperativeArgument arguments[3];
    static TaskStack stacks[3];
    Trace trace = {{0}, 0U};
    const char labels[3] = {'A', 'D', 'G'};
    size_t index;

    printf("PICO_PHASE cooperative_three_task\n");
    karqri_scheduler_init(&scheduler);
    karqri_pico_port_init(&port, &scheduler, 0U);
    for (index = 0U; index < 3U; index++) {
        initialize_stack(&stacks[index]);
        arguments[index].port = &port;
        arguments[index].trace = &trace;
        arguments[index].label = labels[index];
        arguments[index].local_address = 0U;
        arguments[index].rounds = 0U;
        karqri_task_init(
            &tasks[index], cooperative_task_entry, &arguments[index], 20U);
    }
    for (index = 0U; index < 3U; index++) {
        check(karqri_pico_task_context_create(
                  &port,
                  &contexts[index],
                  &tasks[index],
                  stacks[index].words,
                  STACK_WORDS),
              "cooperative context create");
    }
    karqri_pico_trace_begin(&port);
    check(karqri_pico_start(&port), "cooperative start");
    karqri_pico_trace_freeze();
    check(trace.count == 9U, "cooperative trace count");
    check(trace.entries[0] == 'A' && trace.entries[1] == 'D' &&
              trace.entries[2] == 'G' && trace.entries[3] == 'B' &&
              trace.entries[4] == 'E' && trace.entries[5] == 'H' &&
              trace.entries[6] == 'C' && trace.entries[7] == 'F' &&
              trace.entries[8] == 'I',
          "cooperative equal-priority rotation");
    check(arguments[0].local_address != arguments[1].local_address &&
              arguments[0].local_address != arguments[2].local_address &&
              arguments[1].local_address != arguments[2].local_address,
          "cooperative independent PSP");
    for (index = 0U; index < 3U; index++) {
        check(arguments[index].rounds == 3U, "cooperative rounds");
        check(contexts[index].finished, "cooperative return");
        check(stack_guards_intact(&stacks[index]),
              "cooperative stack guard");
    }
    check(karqri_pico_switch_count >= 6U, "cooperative switch count");
    check(scheduler_empty(&scheduler), "cooperative scheduler empty");
    karqri_pico_trace_dump(labels);
}

typedef struct EventScenario {
    KarqriPicoPort *port;
    KarqriEvent *event;
    KarqriScheduledEventWaiter waiter;
    Trace *trace;
    volatile uint32_t result;
} EventScenario;

static void event_waiter_entry(void *argument)
{
    EventScenario *scenario = argument;
    KarqriPicoEventWaitCall call = {
        scenario->port,
        &scenario->waiter,
        scenario->event,
        UINT32_C(0x01),
        KARQRI_EVENT_WAIT_ANY,
        0U,
    };

    trace_append(scenario->trace, 'A');
    scenario->result = karqri_pico_event_wait(&call);
    trace_append(scenario->trace, 'C');
}

static void event_signaler_entry(void *argument)
{
    EventScenario *scenario = argument;

    trace_append(scenario->trace, 'B');
    karqri_pico_event_signal(
        scenario->port, scenario->event, UINT32_C(0x05));
    trace_append(scenario->trace, 'D');
}

static void validate_event_resume(void)
{
    KarqriScheduler scheduler;
    KarqriPicoPort port;
    KarqriEvent event;
    KarqriTask tasks[2];
    KarqriPicoTaskContext contexts[2];
    static TaskStack stacks[2];
    Trace trace = {{0}, 0U};
    EventScenario scenario = {0};

    printf("PICO_PHASE event_delayed_r0\n");
    initialize_stack(&stacks[0]);
    initialize_stack(&stacks[1]);
    karqri_scheduler_init(&scheduler);
    karqri_pico_port_init(&port, &scheduler, 0U);
    karqri_event_init(&event);
    scenario.port = &port;
    scenario.event = &event;
    scenario.trace = &trace;
    karqri_task_init(&tasks[1], event_signaler_entry, &scenario, 20U);
    karqri_task_init(&tasks[0], event_waiter_entry, &scenario, 40U);
    check(karqri_pico_task_context_create(
              &port, &contexts[1], &tasks[1], stacks[1].words, STACK_WORDS),
          "event signaler context");
    check(karqri_pico_task_context_create(
              &port, &contexts[0], &tasks[0], stacks[0].words, STACK_WORDS),
          "event waiter context");
    check(karqri_pico_start(&port), "event start");
    check(trace.count == 4U && trace.entries[0] == 'A' &&
              trace.entries[1] == 'B' && trace.entries[2] == 'C' &&
              trace.entries[3] == 'D',
          "event execution trace");
    check(scenario.result == UINT32_C(0x05), "event delayed R0");
    check(karqri_event_pending(&event) == UINT32_C(0x04),
          "event pending bits");
    check(stack_guards_intact(&stacks[0]) &&
              stack_guards_intact(&stacks[1]),
          "event stack guard");
}

typedef struct SuspendScenario {
    KarqriPicoPort *port;
    KarqriTask *high;
    KarqriTask *middle;
    Trace *trace;
} SuspendScenario;

static void suspend_high_entry(void *argument)
{
    SuspendScenario *scenario = argument;

    trace_append(scenario->trace, 'A');
    check(karqri_pico_suspend_task(scenario->port, scenario->middle),
          "suspend READY task");
    trace_append(scenario->trace, 'B');
    check(karqri_pico_suspend_task(scenario->port, NULL),
          "suspend RUNNING task");
    trace_append(scenario->trace, 'H');
}

static void suspend_middle_entry(void *argument)
{
    SuspendScenario *scenario = argument;

    trace_append(scenario->trace, 'M');
    check(karqri_pico_resume_task(scenario->port, scenario->high),
          "resume high task");
    trace_append(scenario->trace, 'N');
}

static void suspend_low_entry(void *argument)
{
    SuspendScenario *scenario = argument;

    trace_append(scenario->trace, 'L');
    check(karqri_pico_resume_task(scenario->port, scenario->middle),
          "resume middle task");
    trace_append(scenario->trace, 'Q');
}

static void validate_suspend_resume(void)
{
    KarqriScheduler scheduler;
    KarqriPicoPort port;
    KarqriTask tasks[3];
    KarqriPicoTaskContext contexts[3];
    static TaskStack stacks[3];
    Trace trace = {{0}, 0U};
    SuspendScenario scenario = {&port, &tasks[0], &tasks[1], &trace};
    size_t index;

    printf("PICO_PHASE suspend_resume\n");
    karqri_scheduler_init(&scheduler);
    karqri_pico_port_init(&port, &scheduler, 0U);
    for (index = 0U; index < 3U; index++) {
        initialize_stack(&stacks[index]);
    }
    karqri_task_init(&tasks[2], suspend_low_entry, &scenario, 10U);
    karqri_task_init(&tasks[1], suspend_middle_entry, &scenario, 20U);
    karqri_task_init(&tasks[0], suspend_high_entry, &scenario, 30U);
    for (index = 0U; index < 3U; index++) {
        check(karqri_pico_task_context_create(
                  &port,
                  &contexts[index],
                  &tasks[index],
                  stacks[index].words,
                  STACK_WORDS),
              "suspend context create");
    }
    check(karqri_pico_start(&port), "suspend start");
    check(trace.count == 7U, "suspend trace count");
    check(trace.entries[0] == 'A' && trace.entries[1] == 'B' &&
              trace.entries[2] == 'L' && trace.entries[3] == 'M' &&
              trace.entries[4] == 'H' && trace.entries[5] == 'N' &&
              trace.entries[6] == 'Q',
          "suspend/resume execution order");
    check(scheduler_empty(&scheduler), "suspend scheduler empty");
    for (index = 0U; index < 3U; index++) {
        check(stack_guards_intact(&stacks[index]), "suspend stack guard");
    }
}

typedef struct PreemptScenario {
    KarqriPicoPort *port;
    KarqriPicoTick *tick;
    KarqriEvent *event;
    KarqriScheduledEventWaiter waiter;
    volatile uint32_t high_result;
    volatile uint32_t high_done;
    volatile uint32_t counters[2];
    volatile uint32_t critical_owner;
    volatile uint32_t overlap;
} PreemptScenario;

typedef struct WorkerArgument {
    PreemptScenario *scenario;
    uint32_t worker_id;
} WorkerArgument;

static void preempt_high_entry(void *argument)
{
    PreemptScenario *scenario = argument;
    KarqriPicoEventWaitCall call = {
        scenario->port,
        &scenario->waiter,
        scenario->event,
        UINT32_C(0x03),
        KARQRI_EVENT_WAIT_ALL,
        0U,
    };

    scenario->high_result = karqri_pico_event_wait(&call);
    scenario->high_done = 1U;
}

static void preempt_worker_entry(void *argument)
{
    WorkerArgument *worker = argument;
    PreemptScenario *scenario = worker->scenario;

    while (scenario->high_done == 0U || scenario->tick->kernel_ticks < 30U) {
        scenario->counters[worker->worker_id]++;
        if ((scenario->counters[worker->worker_id] & UINT32_C(0x3f)) == 0U) {
            (void)karqri_pico_set_reschedule_suppressed(
                scenario->port, true);
            if (scenario->critical_owner != 0U) {
                scenario->overlap++;
            }
            scenario->critical_owner = worker->worker_id + 1U;
            __asm volatile("nop\nnop\nnop" ::: "memory");
            scenario->critical_owner = 0U;
            (void)karqri_pico_set_reschedule_suppressed(
                scenario->port, false);
        }
    }
}

static void validate_preemption(void)
{
    KarqriScheduler scheduler;
    KarqriPicoPort port;
    KarqriPicoTick tick;
    KarqriTimerQueue timer_queue;
    KarqriTimer timer;
    KarqriEvent event;
    KarqriTask tasks[3];
    KarqriPicoTaskContext contexts[3];
    static TaskStack stacks[3];
    PreemptScenario scenario = {0};
    WorkerArgument workers[2];
    size_t index;

    printf("PICO_PHASE hardware_preemption\n");
    karqri_scheduler_init(&scheduler);
    karqri_pico_port_init(&port, &scheduler, 1U);
    karqri_event_init(&event);
    karqri_timer_queue_init(&timer_queue, &timer, 1U);
    check(karqri_timer_start(
              &timer_queue,
              &event,
              UINT32_C(0x03),
              5U,
              karqri_timer_signal_event),
          "preempt timer start");
    scenario.port = &port;
    scenario.tick = &tick;
    scenario.event = &event;
    workers[0].scenario = &scenario;
    workers[0].worker_id = 0U;
    workers[1].scenario = &scenario;
    workers[1].worker_id = 1U;
    for (index = 0U; index < 3U; index++) {
        initialize_stack(&stacks[index]);
    }
    karqri_task_init(&tasks[2], preempt_worker_entry, &workers[1], 10U);
    karqri_task_init(&tasks[1], preempt_worker_entry, &workers[0], 10U);
    karqri_task_init(&tasks[0], preempt_high_entry, &scenario, 40U);
    for (index = 0U; index < 3U; index++) {
        check(karqri_pico_task_context_create(
                  &port,
                  &contexts[index],
                  &tasks[index],
                  stacks[index].words,
                  STACK_WORDS),
              "preempt context create");
    }
    check(karqri_pico_tick_start(
              &tick, &port, &timer_queue, 1000U),
          "hardware tick start");
    check(karqri_pico_start(&port), "preempt start");
    karqri_pico_tick_stop(&tick);
    check(scenario.high_result == UINT32_C(0x03),
          "preempt delayed R0");
    check(scenario.high_done == 1U, "higher-priority wakeup");
    check(scenario.counters[0] != 0U && scenario.counters[1] != 0U,
          "equal-priority workers");
    check(tick.kernel_ticks >= 30U, "kernel tick count");
    check(tick.irq_count >= tick.kernel_ticks, "raw callback count");
    check(tick.switch_requests != 0U, "preempt switch requests");
    check(karqri_pico_switch_count >= 20U, "repeated preempt switches");
    check(scenario.overlap == 0U, "critical overlap");
    check(scheduler_empty(&scheduler), "preempt scheduler empty");
    for (index = 0U; index < 3U; index++) {
        check(stack_guards_intact(&stacks[index]), "preempt stack guard");
    }
    printf(
        "PICO_METRIC irq=%lu ticks=%lu switches=%lu worker0=%lu worker1=%lu overlap=%lu\n",
        (unsigned long)tick.irq_count,
        (unsigned long)tick.kernel_ticks,
        (unsigned long)karqri_pico_switch_count,
        (unsigned long)scenario.counters[0],
        (unsigned long)scenario.counters[1],
        (unsigned long)scenario.overlap);
}

int main(void)
{
    stdio_init_all();
    sleep_ms(1500U);
    printf("KARQRI_PICO_VALIDATION_START\n");

    validate_single_task();
    validate_cooperative_tasks();
    validate_event_resume();
    validate_suspend_resume();
    validate_preemption();

    if (failures == 0U) {
        printf("KARQRI_PICO_VALIDATION_PASS\n");
    } else {
        printf("KARQRI_PICO_VALIDATION_FAIL count=%lu\n",
               (unsigned long)failures);
    }
    fflush(stdout);
    for (;;) {
        tight_loop_contents();
    }
}
