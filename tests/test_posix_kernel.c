#include "karqri/event.h"
#include "karqri/scheduler.h"
#include "karqri/scheduler_event.h"
#include "karqri/task.h"
#include "karqri/timer.h"
#include "karqri/timer_event.h"
#include "posix_kernel.h"
#include "posix_port.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_STACK_SIZE (64U * 1024U)

typedef struct Trace {
    char entries[128];
    size_t count;
} Trace;

static void fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    exit(EXIT_FAILURE);
}

static void expect(bool condition, const char *message)
{
    if (!condition) {
        fail(message);
    }
}

static void trace_append(Trace *trace, char entry)
{
    expect(trace->count + 1U < sizeof(trace->entries),
           "trace overflow");
    trace->entries[trace->count] = entry;
    trace->count++;
    trace->entries[trace->count] = '\0';
}

static void expect_scheduler_finished(
    const KarqriScheduler *scheduler,
    const char *message)
{
    expect(karqri_scheduler_current(scheduler) == NULL &&
               karqri_scheduler_candidate(scheduler) == NULL &&
               karqri_queue_is_empty(&scheduler->ready_tasks) &&
               karqri_queue_is_empty(&scheduler->suspended_tasks),
           message);
}

typedef struct CooperativeArgument {
    KarqriPosixPort *port;
    Trace *trace;
    char label;
    uintptr_t local_address;
    uint32_t completed_rounds;
} CooperativeArgument;

static void cooperative_task(void *argument)
{
    CooperativeArgument *task_argument = argument;
    uint32_t local_round = 0U;

    task_argument->local_address = (uintptr_t)&local_round;
    for (local_round = 0U; local_round < 3U; local_round++) {
        trace_append(
            task_argument->trace,
            (char)(task_argument->label + (char)local_round));
        task_argument->completed_rounds = local_round + 1U;
        if (local_round < 2U) {
            expect(karqri_posix_yield(task_argument->port),
                   "cooperative yield did not switch context");
            expect(task_argument->completed_rounds == local_round + 1U,
                   "task-local state changed across context switch");
        }
    }
}

static void test_cooperative_contexts(void)
{
    KarqriScheduler scheduler;
    KarqriPosixPort port;
    KarqriTask tasks[3];
    KarqriPosixTaskContext contexts[3];
    CooperativeArgument arguments[3];
    Trace trace = {{0}, 0U};
    const char labels[] = {'A', 'D', 'G'};
    size_t index;

    karqri_scheduler_init(&scheduler);
    karqri_posix_port_init(&port, &scheduler);
    for (index = 0U; index < 3U; index++) {
        arguments[index].port = &port;
        arguments[index].trace = &trace;
        arguments[index].label = labels[index];
        arguments[index].local_address = 0U;
        arguments[index].completed_rounds = 0U;
        karqri_task_init(
            &tasks[index], cooperative_task, &arguments[index], 20U);
    }

    for (index = 0U; index < 3U; index++) {
        expect(karqri_posix_task_context_create(
                   &port,
                   &contexts[index],
                   &tasks[index],
                   TEST_STACK_SIZE),
               "POSIX task context creation failed");
    }
    expect(karqri_posix_run(&port), "POSIX cooperative run did not start");

    if (strcmp(trace.entries, "ADGBEHCFI") != 0) {
        fprintf(stderr, "cooperative trace: %s\n", trace.entries);
        fail("three-task cooperative execution order mismatch");
    }
    for (index = 0U; index < 3U; index++) {
        expect(arguments[index].completed_rounds == 3U,
               "cooperative task did not complete all rounds");
        expect(contexts[index].finished,
               "returned task context was not marked finished");
    }
    expect(arguments[0].local_address != arguments[1].local_address &&
               arguments[0].local_address != arguments[2].local_address &&
               arguments[1].local_address != arguments[2].local_address,
           "tasks did not use independent stacks");
    expect_scheduler_finished(
        &scheduler, "cooperative run left scheduler state behind");

    for (index = 0U; index < 3U; index++) {
        karqri_posix_task_context_destroy(&contexts[index]);
    }
}

typedef struct EventScenario {
    KarqriPosixPort *port;
    KarqriEvent *event;
    KarqriScheduledEventWaiter waiter;
    Trace *trace;
    uintptr_t result;
} EventScenario;

static void event_waiting_task(void *argument)
{
    EventScenario *scenario = argument;

    trace_append(scenario->trace, 'A');
    scenario->result = karqri_posix_wait_event(
        scenario->port,
        &scenario->waiter,
        scenario->event,
        UINT32_C(0x01),
        KARQRI_EVENT_WAIT_ANY);
    trace_append(scenario->trace, 'C');
}

static void event_signaling_task(void *argument)
{
    EventScenario *scenario = argument;

    trace_append(scenario->trace, 'B');
    karqri_posix_signal_event(
        scenario->port, scenario->event, UINT32_C(0x05));
    trace_append(scenario->trace, 'D');
}

static void test_event_block_and_resume(void)
{
    KarqriScheduler scheduler;
    KarqriPosixPort port;
    KarqriEvent event;
    KarqriTask waiter_task;
    KarqriTask signaler_task;
    KarqriPosixTaskContext waiter_context;
    KarqriPosixTaskContext signaler_context;
    Trace trace = {{0}, 0U};
    EventScenario scenario = {0};

    karqri_scheduler_init(&scheduler);
    scenario.port = &port;
    scenario.event = &event;
    scenario.trace = &trace;
    karqri_posix_port_init(&port, &scheduler);
    karqri_event_init(&event);
    karqri_task_init(
        &signaler_task, event_signaling_task, &scenario, 20U);
    karqri_task_init(
        &waiter_task, event_waiting_task, &scenario, 40U);
    expect(karqri_posix_task_context_create(
               &port,
               &signaler_context,
               &signaler_task,
               TEST_STACK_SIZE) &&
               karqri_posix_task_context_create(
                   &port,
                   &waiter_context,
                   &waiter_task,
                   TEST_STACK_SIZE),
           "event scenario context creation failed");

    expect(karqri_posix_run(&port), "event scenario did not run");
    expect(strcmp(trace.entries, "ABCD") == 0,
           "event block/resume execution order mismatch");
    expect(scenario.result == UINT32_C(0x05),
           "event waiter resumed with wrong delayed return");
    expect(karqri_event_pending(&event) == UINT32_C(0x04),
           "event waiter consumed wrong signal bits");
    expect(waiter_context.finished && signaler_context.finished,
           "event scenario task return was not handled");
    expect_scheduler_finished(
        &scheduler, "event scenario left scheduler state behind");
    karqri_posix_task_context_destroy(&waiter_context);
    karqri_posix_task_context_destroy(&signaler_context);
}

typedef struct TimerScenario {
    KarqriPosixPort *port;
    KarqriEvent *event;
    KarqriTimerQueue *timer_queue;
    KarqriScheduledEventWaiter waiter;
    Trace *trace;
    uintptr_t result;
} TimerScenario;

static void timer_waiting_task(void *argument)
{
    TimerScenario *scenario = argument;

    trace_append(scenario->trace, 'W');
    scenario->result = karqri_posix_wait_event(
        scenario->port,
        &scenario->waiter,
        scenario->event,
        UINT32_C(0x03),
        KARQRI_EVENT_WAIT_ALL);
    trace_append(scenario->trace, 'R');
}

static void timer_driver_task(void *argument)
{
    TimerScenario *scenario = argument;

    trace_append(scenario->trace, 'T');
    karqri_posix_timer_tick(scenario->port, scenario->timer_queue);
    trace_append(scenario->trace, 'D');
}

static void test_timer_wakeup(void)
{
    KarqriScheduler scheduler;
    KarqriPosixPort port;
    KarqriEvent event;
    KarqriTimerQueue timer_queue;
    KarqriTimer timers[2];
    KarqriTask waiter_task;
    KarqriTask driver_task;
    KarqriPosixTaskContext waiter_context;
    KarqriPosixTaskContext driver_context;
    Trace trace = {{0}, 0U};
    TimerScenario scenario = {0};

    karqri_scheduler_init(&scheduler);
    scenario.port = &port;
    scenario.event = &event;
    scenario.timer_queue = &timer_queue;
    scenario.trace = &trace;
    karqri_posix_port_init(&port, &scheduler);
    karqri_event_init(&event);
    karqri_timer_queue_init(&timer_queue, timers, 2U);
    expect(karqri_timer_start(
               &timer_queue,
               &event,
               UINT32_C(0x01),
               1U,
               karqri_timer_signal_event) &&
               karqri_timer_start(
                   &timer_queue,
                   &event,
                   UINT32_C(0x02),
                   1U,
                   karqri_timer_signal_event),
           "timer scenario setup failed");
    karqri_task_init(&driver_task, timer_driver_task, &scenario, 10U);
    karqri_task_init(&waiter_task, timer_waiting_task, &scenario, 30U);
    expect(karqri_posix_task_context_create(
               &port, &driver_context, &driver_task, TEST_STACK_SIZE) &&
               karqri_posix_task_context_create(
                   &port, &waiter_context, &waiter_task, TEST_STACK_SIZE),
           "timer scenario context creation failed");

    expect(karqri_posix_run(&port), "timer scenario did not run");
    expect(strcmp(trace.entries, "WTRD") == 0,
           "timer wakeup execution order mismatch");
    expect(scenario.result == UINT32_C(0x03),
           "timer wakeup returned wrong delayed event value");
    expect(karqri_queue_is_empty(&timer_queue.active_timers),
           "timer wakeup left active timers");
    expect_scheduler_finished(
        &scheduler, "timer scenario left scheduler state behind");
    karqri_posix_task_context_destroy(&waiter_context);
    karqri_posix_task_context_destroy(&driver_context);
}

typedef struct SuspendScenario {
    KarqriPosixPort *port;
    KarqriTask *high_task;
    KarqriTask *middle_task;
    Trace *trace;
} SuspendScenario;

static void high_suspend_task(void *argument)
{
    SuspendScenario *scenario = argument;

    trace_append(scenario->trace, 'A');
    expect(karqri_posix_suspend_task(
               scenario->port, scenario->middle_task),
           "high task could not suspend READY middle task");
    trace_append(scenario->trace, 'B');
    expect(karqri_posix_suspend_task(scenario->port, NULL),
           "high task could not suspend itself");
    trace_append(scenario->trace, 'H');
}

static void middle_resume_task(void *argument)
{
    SuspendScenario *scenario = argument;

    trace_append(scenario->trace, 'M');
    expect(karqri_posix_resume_task(
               scenario->port, scenario->high_task),
           "middle task could not resume high task");
    trace_append(scenario->trace, 'N');
}

static void low_resume_task(void *argument)
{
    SuspendScenario *scenario = argument;

    trace_append(scenario->trace, 'L');
    expect(karqri_posix_resume_task(
               scenario->port, scenario->middle_task),
           "low task could not resume middle task");
    trace_append(scenario->trace, 'Q');
}

static void test_suspend_resume_contexts(void)
{
    KarqriScheduler scheduler;
    KarqriPosixPort port;
    KarqriTask high;
    KarqriTask middle;
    KarqriTask low;
    KarqriPosixTaskContext high_context;
    KarqriPosixTaskContext middle_context;
    KarqriPosixTaskContext low_context;
    Trace trace = {{0}, 0U};
    SuspendScenario scenario = {&port, &high, &middle, &trace};

    karqri_scheduler_init(&scheduler);
    karqri_posix_port_init(&port, &scheduler);
    karqri_task_init(&low, low_resume_task, &scenario, 10U);
    karqri_task_init(&middle, middle_resume_task, &scenario, 20U);
    karqri_task_init(&high, high_suspend_task, &scenario, 30U);
    expect(karqri_posix_task_context_create(
               &port, &low_context, &low, TEST_STACK_SIZE) &&
               karqri_posix_task_context_create(
                   &port, &middle_context, &middle, TEST_STACK_SIZE) &&
               karqri_posix_task_context_create(
                   &port, &high_context, &high, TEST_STACK_SIZE),
           "suspend/resume context creation failed");

    expect(karqri_posix_run(&port), "suspend/resume scenario did not run");
    expect(strcmp(trace.entries, "ABLMHNQ") == 0,
           "suspend/resume context execution order mismatch");
    expect(high_context.finished && middle_context.finished &&
               low_context.finished,
           "suspend/resume task return was not handled");
    expect_scheduler_finished(
        &scheduler, "suspend/resume scenario left scheduler state behind");
    karqri_posix_task_context_destroy(&high_context);
    karqri_posix_task_context_destroy(&middle_context);
    karqri_posix_task_context_destroy(&low_context);
}

int main(void)
{
    test_cooperative_contexts();
    test_event_block_and_resume();
    test_timer_wakeup();
    test_suspend_resume_contexts();
    puts("POSIX kernel integration validation: PASS");
    return EXIT_SUCCESS;
}
