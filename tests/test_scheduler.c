#include "karqri/event.h"
#include "karqri/scheduler.h"
#include "karqri/scheduler_event.h"
#include "karqri/task.h"
#include "karqri/timer.h"
#include "karqri/timer_event.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_TASKS 8U

static const char *current_test;

static void fail(const char *message)
{
    fprintf(stderr, "FAIL [%s]: %s\n", current_test, message);
    exit(EXIT_FAILURE);
}

static void expect(bool condition, const char *message)
{
    if (!condition) {
        fail(message);
    }
}

static void test_entry(void *argument)
{
    (void)argument;
}

static void initialize_task(KarqriTask *task, uint32_t priority)
{
    karqri_task_init(task, test_entry, NULL, priority);
}

static KarqriTask *task_from_node(const KarqriQueueNode *node)
{
    return (KarqriTask *)((unsigned char *)node -
                          offsetof(KarqriTask, queue_node));
}

static size_t expect_links(
    const KarqriQueueNode *queue,
    size_t maximum_count)
{
    const KarqriQueueNode *node = queue->next;
    const KarqriQueueNode *previous = queue;
    const KarqriQueueNode *next;
    size_t forward_count = 0U;
    size_t reverse_count = 0U;

    while (node != queue) {
        expect(node->previous == previous, "scheduler previous link broken");
        previous = node;
        node = node->next;
        forward_count++;
        expect(forward_count <= maximum_count,
               "scheduler forward queue cycle");
    }
    expect(previous == queue->previous, "scheduler queue tail mismatch");

    next = queue;
    node = queue->previous;
    while (node != queue) {
        expect(node->next == next, "scheduler next link broken");
        next = node;
        node = node->previous;
        reverse_count++;
        expect(reverse_count <= maximum_count,
               "scheduler reverse queue cycle");
    }
    expect(next == queue->next, "scheduler queue head mismatch");
    expect(forward_count == reverse_count,
           "scheduler forward/reverse counts differ");
    return forward_count;
}

static void expect_ready_order(
    const KarqriScheduler *scheduler,
    KarqriTask *const *expected,
    size_t count)
{
    const KarqriQueueNode *node = scheduler->ready_tasks.next;
    uint32_t previous_priority = UINT32_MAX;
    size_t index;

    for (index = 0U; index < count; index++) {
        KarqriTask *task;

        expect(node != &scheduler->ready_tasks,
               "ready queue ended early");
        task = task_from_node(node);
        expect(task == expected[index], "ready queue task order mismatch");
        expect(task->priority <= previous_priority,
               "ready queue priority order is not descending");
        previous_priority = task->priority;
        node = node->next;
    }
    expect(node == &scheduler->ready_tasks,
           "ready queue has unexpected extra tasks");
    expect(expect_links(&scheduler->ready_tasks, MAX_TASKS) == count,
           "ready queue link count mismatch");
    expect_links(&scheduler->suspended_tasks, MAX_TASKS);
    expect(karqri_scheduler_candidate(scheduler) ==
               (count == 0U ? NULL : expected[0]),
           "scheduler candidate is not ready queue head");
}

static void test_initialization(void)
{
    KarqriScheduler scheduler;

    karqri_scheduler_init(&scheduler);
    expect(karqri_scheduler_current(&scheduler) == NULL,
           "new scheduler has a current task");
    expect(karqri_scheduler_candidate(&scheduler) == NULL,
           "empty scheduler has a candidate");
    expect(!scheduler.reschedule_suppressed,
           "new scheduler starts with reschedule suppressed");
    expect_links(&scheduler.ready_tasks, 0U);
    expect_links(&scheduler.suspended_tasks, 0U);
    expect(!karqri_scheduler_reschedule(&scheduler),
           "empty scheduler reported a switch");
}

static void test_ready_priority_and_equal_fifo(void)
{
    KarqriScheduler scheduler;
    KarqriTask p20;
    KarqriTask p40_first;
    KarqriTask p30;
    KarqriTask p40_second;
    KarqriTask *expected[] = {
        &p40_first, &p40_second, &p30, &p20,
    };

    karqri_scheduler_init(&scheduler);
    (void)karqri_scheduler_set_reschedule_suppressed(&scheduler, true);
    initialize_task(&p20, 20U);
    initialize_task(&p40_first, 40U);
    initialize_task(&p30, 30U);
    initialize_task(&p40_second, 40U);
    karqri_scheduler_add_ready(&scheduler, &p20);
    karqri_scheduler_add_ready(&scheduler, &p40_first);
    karqri_scheduler_add_ready(&scheduler, &p30);
    karqri_scheduler_add_ready(&scheduler, &p40_second);
    expect_ready_order(&scheduler, expected, 4U);
    expect(karqri_scheduler_current(&scheduler) == NULL,
           "suppressed scheduler selected a task");

    expect(karqri_scheduler_set_reschedule_suppressed(
               &scheduler, false),
           "reschedule suppression did not return previous mode");
    expect(karqri_scheduler_reschedule(&scheduler),
           "first task selection did not report a switch");
    expect(karqri_scheduler_current(&scheduler) == &p40_first &&
               karqri_task_state(&p40_first) ==
                   KARQRI_TASK_STATE_RUNNING,
           "first candidate did not transition to RUNNING");
}

static void test_higher_priority_switch_and_suppression(void)
{
    KarqriScheduler scheduler;
    KarqriTask low;
    KarqriTask high;
    KarqriTask *expected[] = {&high, &low};

    karqri_scheduler_init(&scheduler);
    initialize_task(&low, 10U);
    initialize_task(&high, 40U);
    karqri_scheduler_add_ready(&scheduler, &low);
    expect(karqri_scheduler_current(&scheduler) == &low,
           "first ready task was not selected");

    expect(!karqri_scheduler_set_reschedule_suppressed(
               &scheduler, true),
           "initial suppression mode was not false");
    karqri_scheduler_add_ready(&scheduler, &high);
    expect_ready_order(&scheduler, expected, 2U);
    expect(karqri_scheduler_current(&scheduler) == &low,
           "suppressed reschedule changed current task");
    expect(!karqri_scheduler_reschedule(&scheduler),
           "suppressed explicit reschedule reported a switch");

    (void)karqri_scheduler_set_reschedule_suppressed(&scheduler, false);
    expect(karqri_scheduler_reschedule(&scheduler) &&
               karqri_scheduler_current(&scheduler) == &high,
           "unmasked scheduler did not select higher priority task");
    expect(karqri_task_state(&low) == KARQRI_TASK_STATE_RUNNING,
           "baseline old RUNNING marker was unexpectedly rewritten");
}

static void test_suspend_and_resume(void)
{
    KarqriScheduler scheduler;
    KarqriTask high;
    KarqriTask low;
    KarqriTask *only_low[] = {&low};
    KarqriTask *resumed[] = {&high, &low};

    karqri_scheduler_init(&scheduler);
    (void)karqri_scheduler_set_reschedule_suppressed(&scheduler, true);
    initialize_task(&high, 40U);
    initialize_task(&low, 20U);
    karqri_scheduler_add_ready(&scheduler, &low);
    karqri_scheduler_add_ready(&scheduler, &high);
    (void)karqri_scheduler_set_reschedule_suppressed(&scheduler, false);
    expect(karqri_scheduler_reschedule(&scheduler),
           "suspend setup did not dispatch high task");

    expect(karqri_scheduler_suspend(&scheduler, NULL),
           "NULL/current running-task suspend failed");
    expect(karqri_scheduler_current(&scheduler) == &low,
           "running-task suspend did not select next task");
    expect(karqri_task_state(&high) ==
               (KARQRI_TASK_STATE_RUNNING |
                KARQRI_TASK_STATE_SUSPENDED),
           "running-task suspend changed baseline state bits");
    expect_ready_order(&scheduler, only_low, 1U);
    expect(scheduler.suspended_tasks.next == &high.queue_node,
           "suspended running task was not moved to suspend queue");

    expect(karqri_scheduler_resume(&scheduler, &high),
           "suspended task resume failed");
    expect_ready_order(&scheduler, resumed, 2U);
    expect(karqri_scheduler_current(&scheduler) == &high &&
               karqri_task_state(&high) == KARQRI_TASK_STATE_RUNNING,
           "resumed higher-priority task was not selected");
    expect(karqri_queue_is_empty(&scheduler.suspended_tasks),
           "resume left task in suspend queue");

    expect(karqri_scheduler_suspend(&scheduler, &low),
           "READY task suspend failed");
    expect(!karqri_task_state_has_any(
               &low, KARQRI_TASK_STATE_READY) &&
               karqri_task_state_has_any(
                   &low, KARQRI_TASK_STATE_SUSPENDED),
           "READY task suspend did not update state");
}

static void test_priority_change(void)
{
    KarqriScheduler scheduler;
    KarqriTask high;
    KarqriTask middle;
    KarqriTask low;
    KarqriTask *initial[] = {&high, &middle, &low};
    KarqriTask *raised[] = {&low, &high, &middle};
    KarqriTask *running_lowered[] = {&high, &middle, &low};

    karqri_scheduler_init(&scheduler);
    (void)karqri_scheduler_set_reschedule_suppressed(&scheduler, true);
    initialize_task(&high, 40U);
    initialize_task(&middle, 30U);
    initialize_task(&low, 20U);
    karqri_scheduler_add_ready(&scheduler, &low);
    karqri_scheduler_add_ready(&scheduler, &middle);
    karqri_scheduler_add_ready(&scheduler, &high);
    expect_ready_order(&scheduler, initial, 3U);

    expect(karqri_scheduler_set_priority(
               &scheduler, &low, 50U) == 20U,
           "READY task priority change returned wrong old value");
    expect_ready_order(&scheduler, raised, 3U);
    (void)karqri_scheduler_set_reschedule_suppressed(&scheduler, false);
    expect(karqri_scheduler_reschedule(&scheduler) &&
               karqri_scheduler_current(&scheduler) == &low,
           "raised READY task was not selected");

    expect(karqri_scheduler_set_priority(
               &scheduler, NULL, 10U) == 50U,
           "NULL/current priority change returned wrong old value");
    expect_ready_order(&scheduler, running_lowered, 3U);
    expect(karqri_scheduler_current(&scheduler) == &high,
           "lowering RUNNING task did not select higher candidate");
}

static void test_same_priority_resume_fifo(void)
{
    KarqriScheduler scheduler;
    KarqriTask current;
    KarqriTask peer;
    KarqriTask resumed;
    KarqriTask *before_resume[] = {&current, &peer};
    KarqriTask *after_resume[] = {&current, &peer, &resumed};

    karqri_scheduler_init(&scheduler);
    (void)karqri_scheduler_set_reschedule_suppressed(&scheduler, true);
    initialize_task(&current, 30U);
    initialize_task(&peer, 30U);
    initialize_task(&resumed, 30U);
    karqri_scheduler_add_ready(&scheduler, &current);
    karqri_scheduler_add_ready(&scheduler, &peer);
    karqri_scheduler_add_ready(&scheduler, &resumed);
    expect(karqri_scheduler_suspend(&scheduler, &resumed),
           "same-priority resume setup suspend failed");
    expect_ready_order(&scheduler, before_resume, 2U);
    (void)karqri_scheduler_set_reschedule_suppressed(&scheduler, false);
    expect(karqri_scheduler_reschedule(&scheduler),
           "same-priority resume setup did not select current");

    expect(karqri_scheduler_resume(&scheduler, &resumed),
           "same-priority resume failed");
    expect_ready_order(&scheduler, after_resume, 3U);
    expect(karqri_scheduler_current(&scheduler) == &current,
           "same-priority resume preempted current task");
}

static void test_same_priority_change_fifo_and_no_op(void)
{
    KarqriScheduler scheduler;
    KarqriTask current;
    KarqriTask peer;
    KarqriTask changed;
    KarqriTask *before[] = {&current, &peer, &changed};
    KarqriTask *after_change[] = {&current, &peer, &changed};

    karqri_scheduler_init(&scheduler);
    (void)karqri_scheduler_set_reschedule_suppressed(&scheduler, true);
    initialize_task(&current, 30U);
    initialize_task(&peer, 30U);
    initialize_task(&changed, 20U);
    karqri_scheduler_add_ready(&scheduler, &current);
    karqri_scheduler_add_ready(&scheduler, &peer);
    karqri_scheduler_add_ready(&scheduler, &changed);
    expect_ready_order(&scheduler, before, 3U);
    (void)karqri_scheduler_set_reschedule_suppressed(&scheduler, false);
    expect(karqri_scheduler_reschedule(&scheduler),
           "same-priority change setup did not select current");

    expect(karqri_scheduler_set_priority(
               &scheduler, &changed, 30U) == 20U,
           "priority change into equal group returned wrong old value");
    expect_ready_order(&scheduler, after_change, 3U);
    expect(karqri_scheduler_current(&scheduler) == &current,
           "priority change into equal group preempted current");

    expect(karqri_scheduler_set_priority(
               &scheduler, &current, 30U) == 30U,
           "same-value priority change returned wrong value");
    expect_ready_order(&scheduler, after_change, 3U);
    expect(karqri_scheduler_current(&scheduler) == &current,
           "same-value priority change moved current task");
}

static void test_timeslice_rotation(void)
{
    KarqriScheduler scheduler;
    KarqriTask first;
    KarqriTask second;
    KarqriTask third;
    KarqriTask *before[] = {&first, &second, &third};
    KarqriTask *after[] = {&second, &third, &first};

    karqri_scheduler_init(&scheduler);
    (void)karqri_scheduler_set_reschedule_suppressed(&scheduler, true);
    initialize_task(&first, 30U);
    initialize_task(&second, 30U);
    initialize_task(&third, 30U);
    karqri_scheduler_add_ready(&scheduler, &first);
    karqri_scheduler_add_ready(&scheduler, &second);
    karqri_scheduler_add_ready(&scheduler, &third);
    expect_ready_order(&scheduler, before, 3U);
    (void)karqri_scheduler_set_reschedule_suppressed(&scheduler, false);
    expect(karqri_scheduler_reschedule(&scheduler),
           "timeslice setup did not select first task");

    expect(karqri_scheduler_rotate_timeslice(&scheduler),
           "equal-priority timeslice rotation did not switch task");
    expect_ready_order(&scheduler, after, 3U);
    expect(karqri_scheduler_current(&scheduler) == &second,
           "timeslice rotation selected wrong equal-priority task");
}

static void test_interrupted_task_timeslice_rotation(void)
{
    KarqriScheduler scheduler;
    KarqriTask high;
    KarqriTask interrupted;
    KarqriTask peer;
    KarqriTask *before[] = {&high, &interrupted, &peer};
    KarqriTask *after[] = {&high, &peer, &interrupted};

    karqri_scheduler_init(&scheduler);
    (void)karqri_scheduler_set_reschedule_suppressed(&scheduler, true);
    initialize_task(&high, 40U);
    initialize_task(&interrupted, 20U);
    initialize_task(&peer, 20U);
    karqri_scheduler_add_ready(&scheduler, &interrupted);
    karqri_scheduler_add_ready(&scheduler, &peer);
    karqri_scheduler_add_ready(&scheduler, &high);
    expect_ready_order(&scheduler, before, 3U);
    scheduler.current_task = &high;

    (void)karqri_scheduler_set_reschedule_suppressed(&scheduler, false);
    expect(!karqri_scheduler_rotate_task_timeslice(
               &scheduler, &interrupted),
           "interrupted-task rotation unexpectedly changed candidate");
    expect_ready_order(&scheduler, after, 3U);
    expect(karqri_scheduler_current(&scheduler) == &high,
           "interrupted-task rotation changed higher-priority current");
}

static void test_event_wait_and_wakeup(void)
{
    KarqriScheduler scheduler;
    KarqriTask waiter_task;
    KarqriTask peer;
    KarqriEvent event;
    KarqriScheduledEventWaiter waiter;
    KarqriTask *peer_only[] = {&peer};
    KarqriTask *woken[] = {&waiter_task, &peer};

    karqri_scheduler_init(&scheduler);
    karqri_event_init(&event);
    initialize_task(&waiter_task, 40U);
    initialize_task(&peer, 20U);
    karqri_scheduler_add_ready(&scheduler, &peer);
    karqri_scheduler_add_ready(&scheduler, &waiter_task);
    expect(karqri_scheduler_current(&scheduler) == &waiter_task,
           "event waiter was not initial current task");

    expect(karqri_scheduler_wait_event(
               &scheduler,
               &waiter_task,
               &waiter,
               &event,
               UINT32_C(0x01),
               KARQRI_EVENT_WAIT_ANY).disposition ==
               KARQRI_EVENT_WAIT_BLOCKED,
           "scheduler event wait did not block");
    expect_ready_order(&scheduler, peer_only, 1U);
    expect(karqri_scheduler_current(&scheduler) == &peer &&
               karqri_task_state(&waiter_task) ==
                   KARQRI_TASK_STATE_EVENT_WAIT,
           "event wait did not remove running task and select peer");

    karqri_event_signal(&event, UINT32_C(0x05));
    expect_ready_order(&scheduler, woken, 2U);
    expect(karqri_scheduler_current(&scheduler) == &waiter_task &&
               karqri_task_state(&waiter_task) ==
                   KARQRI_TASK_STATE_RUNNING,
           "event wakeup did not select higher-priority task");
    expect(karqri_task_has_delayed_return(&waiter_task) &&
               karqri_task_delayed_return(&waiter_task) ==
                   UINT32_C(0x05),
           "event wakeup did not set logical return value");
}

static void test_suspended_event_waiter(void)
{
    KarqriScheduler scheduler;
    KarqriTask waiter_task;
    KarqriTask peer;
    KarqriEvent event;
    KarqriScheduledEventWaiter waiter;
    KarqriTask *after_signal[] = {&waiter_task, &peer};

    karqri_scheduler_init(&scheduler);
    karqri_event_init(&event);
    initialize_task(&waiter_task, 40U);
    initialize_task(&peer, 20U);
    karqri_scheduler_add_ready(&scheduler, &peer);
    karqri_scheduler_add_ready(&scheduler, &waiter_task);
    (void)karqri_scheduler_wait_event(
        &scheduler,
        &waiter_task,
        &waiter,
        &event,
        UINT32_C(0x08),
        KARQRI_EVENT_WAIT_ANY);
    expect(karqri_scheduler_suspend(&scheduler, &waiter_task),
           "waiting task suspend failed");
    expect(karqri_task_state(&waiter_task) ==
               (KARQRI_TASK_STATE_EVENT_WAIT |
                KARQRI_TASK_STATE_SUSPENDED),
           "suspended event wait bits did not coexist");

    (void)karqri_scheduler_set_reschedule_suppressed(&scheduler, true);
    karqri_event_signal(&event, UINT32_C(0x08));
    expect_ready_order(&scheduler, after_signal, 2U);
    expect(karqri_task_state(&waiter_task) ==
               (KARQRI_TASK_STATE_READY |
                KARQRI_TASK_STATE_SUSPENDED),
           "event signal did not preserve READY|SUSPENDED baseline state");
    expect(karqri_scheduler_current(&scheduler) == &peer,
           "suppressed event wakeup changed current task");
    expect(karqri_task_has_delayed_return(&waiter_task) &&
               karqri_task_delayed_return(&waiter_task) == UINT32_C(0x08),
           "suspended waiter did not receive its deferred result");
    expect(expect_links(&event.waiters, MAX_TASKS) == 0U,
           "suspended waiter remained registered after signal");

    (void)karqri_scheduler_set_reschedule_suppressed(&scheduler, false);
    expect(karqri_scheduler_reschedule(&scheduler) &&
               karqri_scheduler_current(&scheduler) == &waiter_task &&
               karqri_task_state(&waiter_task) ==
                   KARQRI_TASK_STATE_RUNNING,
           "baseline state-agnostic candidate selection changed");
}

static void test_same_priority_event_wakeup_fifo(void)
{
    KarqriScheduler scheduler;
    KarqriTask current;
    KarqriTask first_task;
    KarqriTask second_task;
    KarqriEvent event;
    KarqriScheduledEventWaiter first_waiter;
    KarqriScheduledEventWaiter second_waiter;
    KarqriTask *current_only[] = {&current};
    KarqriTask *woken[] = {&current, &first_task, &second_task};

    karqri_scheduler_init(&scheduler);
    karqri_event_init(&event);
    initialize_task(&current, 30U);
    initialize_task(&first_task, 30U);
    initialize_task(&second_task, 30U);
    karqri_scheduler_add_ready(&scheduler, &current);
    karqri_scheduler_add_ready(&scheduler, &first_task);
    karqri_scheduler_add_ready(&scheduler, &second_task);
    (void)karqri_scheduler_wait_event(
        &scheduler, &first_task, &first_waiter, &event,
        UINT32_C(0x01), KARQRI_EVENT_WAIT_ANY);
    (void)karqri_scheduler_wait_event(
        &scheduler, &second_task, &second_waiter, &event,
        UINT32_C(0x01), KARQRI_EVENT_WAIT_ANY);
    expect_ready_order(&scheduler, current_only, 1U);

    karqri_event_signal(&event, UINT32_C(0x01));
    expect_ready_order(&scheduler, woken, 3U);
    expect(karqri_scheduler_current(&scheduler) == &current,
           "same-priority event wakeup preempted current task");
}

static void test_timer_event_scheduler_path(void)
{
    KarqriScheduler scheduler;
    KarqriTask waiter_task;
    KarqriTask peer;
    KarqriEvent event;
    KarqriScheduledEventWaiter waiter;
    KarqriTimerQueue timer_queue;
    KarqriTimer timers[2];

    karqri_scheduler_init(&scheduler);
    karqri_event_init(&event);
    karqri_timer_queue_init(&timer_queue, timers, 2U);
    initialize_task(&waiter_task, 30U);
    initialize_task(&peer, 10U);
    karqri_scheduler_add_ready(&scheduler, &peer);
    karqri_scheduler_add_ready(&scheduler, &waiter_task);
    (void)karqri_scheduler_wait_event(
        &scheduler,
        &waiter_task,
        &waiter,
        &event,
        UINT32_C(0x03),
        KARQRI_EVENT_WAIT_ALL);
    expect(karqri_scheduler_current(&scheduler) == &peer,
           "timer/event waiter did not yield to peer");

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
           "timer/event scheduler setup failed");
    karqri_timer_tick(&timer_queue);
    expect(karqri_scheduler_current(&scheduler) == &waiter_task &&
               karqri_task_state(&waiter_task) == KARQRI_TASK_STATE_RUNNING &&
               karqri_task_has_delayed_return(&waiter_task) &&
               karqri_task_delayed_return(&waiter_task) ==
                   UINT32_C(0x03),
           "timer -> event -> task -> scheduler path failed");
    expect(karqri_queue_is_empty(&timer_queue.active_timers),
           "timer/event scheduler path left active timers");
}

static void test_same_priority_timer_wakeup_fifo(void)
{
    KarqriScheduler scheduler;
    KarqriTask current;
    KarqriTask waiter_task;
    KarqriEvent event;
    KarqriScheduledEventWaiter waiter;
    KarqriTimerQueue timer_queue;
    KarqriTimer timer;
    KarqriTask *before_tick[] = {&current};
    KarqriTask *after_tick[] = {&current, &waiter_task};

    karqri_scheduler_init(&scheduler);
    karqri_event_init(&event);
    karqri_timer_queue_init(&timer_queue, &timer, 1U);
    initialize_task(&current, 30U);
    initialize_task(&waiter_task, 30U);
    karqri_scheduler_add_ready(&scheduler, &current);
    karqri_scheduler_add_ready(&scheduler, &waiter_task);
    (void)karqri_scheduler_wait_event(
        &scheduler, &waiter_task, &waiter, &event,
        UINT32_C(0x01), KARQRI_EVENT_WAIT_ANY);
    expect_ready_order(&scheduler, before_tick, 1U);
    expect(karqri_timer_start(
               &timer_queue, &event, UINT32_C(0x01), 1U,
               karqri_timer_signal_event),
           "same-priority timer setup failed");

    karqri_timer_tick(&timer_queue);
    expect_ready_order(&scheduler, after_tick, 2U);
    expect(karqri_scheduler_current(&scheduler) == &current,
           "same-priority timer wakeup preempted current task");
}

static void test_long_scheduler_sequence(void)
{
    KarqriScheduler scheduler;
    KarqriTask tasks[4];
    KarqriTask *order_one[] = {&tasks[3], &tasks[2], &tasks[1], &tasks[0]};
    KarqriTask *order_two[] = {&tasks[1], &tasks[0]};
    KarqriTask *order_three[] = {
        &tasks[3], &tasks[2], &tasks[1], &tasks[0],
    };
    size_t index;

    karqri_scheduler_init(&scheduler);
    (void)karqri_scheduler_set_reschedule_suppressed(&scheduler, true);
    for (index = 0U; index < 4U; index++) {
        initialize_task(&tasks[index], (uint32_t)(10U + index * 10U));
        karqri_scheduler_add_ready(&scheduler, &tasks[index]);
        expect_links(&scheduler.ready_tasks, MAX_TASKS);
    }
    expect_ready_order(&scheduler, order_one, 4U);
    (void)karqri_scheduler_set_reschedule_suppressed(&scheduler, false);
    expect(karqri_scheduler_reschedule(&scheduler),
           "long sequence initial dispatch failed");

    expect(karqri_scheduler_suspend(&scheduler, &tasks[2]),
           "long sequence READY suspend failed");
    expect(karqri_scheduler_suspend(&scheduler, NULL),
           "long sequence RUNNING suspend failed");
    expect_ready_order(&scheduler, order_two, 2U);
    expect(karqri_scheduler_current(&scheduler) == &tasks[1],
           "long sequence selected wrong task after suspends");

    expect(karqri_scheduler_resume(&scheduler, &tasks[2]) &&
               karqri_scheduler_resume(&scheduler, &tasks[3]),
           "long sequence resume failed");
    expect_ready_order(&scheduler, order_three, 4U);
    expect(karqri_scheduler_current(&scheduler) == &tasks[3],
           "long sequence selected wrong resumed task");
    expect_links(&scheduler.suspended_tasks, MAX_TASKS);
}

/* These migrated cases suppress dispatch to observe logical READY states.
 * The earlier integration cases exercise dispatch and RUNNING transitions. */
static void test_scheduled_immediate_any_and_all_wait(void)
{
    KarqriScheduler scheduler;
    KarqriEvent event;
    KarqriTask task;
    KarqriScheduledEventWaiter waiter;
    KarqriEventWaitResult result;

    karqri_scheduler_init(&scheduler);
    (void)karqri_scheduler_set_reschedule_suppressed(&scheduler, true);
    karqri_event_init(&event);
    initialize_task(&task, 1U);
    karqri_scheduler_add_ready(&scheduler, &task);
    karqri_event_signal(&event, UINT32_C(0x5));

    result = karqri_scheduler_wait_event(
        &scheduler,
        &task,
        &waiter,
        &event,
        UINT32_C(0x1),
        KARQRI_EVENT_WAIT_ANY);
    expect(result.disposition == KARQRI_EVENT_WAIT_COMPLETED,
           "satisfied ANY wait did not complete immediately");
    expect(result.events == UINT32_C(0x5),
           "ANY wait did not return the full event snapshot");
    expect(karqri_event_pending(&event) == UINT32_C(0x4),
           "ANY wait consumed the wrong event bits");

    karqri_event_signal(&event, UINT32_C(0x2));
    result = karqri_scheduler_wait_event(
        &scheduler,
        &task,
        &waiter,
        &event,
        UINT32_C(0x6),
        KARQRI_EVENT_WAIT_ALL);
    expect(result.disposition == KARQRI_EVENT_WAIT_COMPLETED,
           "satisfied ALL wait did not complete immediately");
    expect(result.events == UINT32_C(0x6),
           "ALL wait returned the wrong event snapshot");
    expect(karqri_event_pending(&event) == 0U,
           "ALL wait did not consume its requested bits");
    expect(karqri_task_state(&task) == KARQRI_TASK_STATE_READY &&
               !karqri_task_has_delayed_return(&task),
           "immediate wait unexpectedly changed task state or return slot");
}

static void test_scheduled_immediate_wait_preserves_valid_result(void)
{
    KarqriScheduler scheduler;
    KarqriEvent event;
    KarqriTask task;
    KarqriScheduledEventWaiter waiter;
    KarqriEventWaitResult result;
    KarqriTask *ready[] = {&task};

    karqri_scheduler_init(&scheduler);
    karqri_event_init(&event);
    initialize_task(&task, 30U);
    karqri_scheduler_add_ready(&scheduler, &task);
    karqri_task_set_delayed_return(&task, UINT32_C(0xdead));
    karqri_event_signal(&event, UINT32_C(0x03));

    result = karqri_scheduler_wait_event(
        &scheduler, &task, &waiter, &event,
        UINT32_C(0x01), KARQRI_EVENT_WAIT_ANY);
    expect(result.disposition == KARQRI_EVENT_WAIT_COMPLETED &&
               result.events == UINT32_C(0x03) &&
               karqri_task_has_delayed_return(&task) &&
               karqri_task_delayed_return(&task) == UINT32_C(0xdead),
           "immediate ANY wait changed an existing valid delayed result");
    result = karqri_scheduler_wait_event(
        &scheduler, &task, &waiter, &event,
        UINT32_C(0x02), KARQRI_EVENT_WAIT_ALL);
    expect(result.disposition == KARQRI_EVENT_WAIT_COMPLETED &&
               result.events == UINT32_C(0x02) &&
               karqri_task_has_delayed_return(&task) &&
               karqri_task_delayed_return(&task) == UINT32_C(0xdead),
           "immediate ALL wait changed an existing valid delayed result");
    expect_ready_order(&scheduler, ready, 1U);
    expect(karqri_scheduler_current(&scheduler) == &task &&
               karqri_task_state(&task) == KARQRI_TASK_STATE_RUNNING &&
               karqri_event_pending(&event) == 0U &&
               expect_links(&event.waiters, 0U) == 0U,
           "immediate waits changed selection, state or waiter registration");
}

static void test_scheduled_blocking_wait_and_deferred_return(void)
{
    KarqriScheduler scheduler;
    KarqriEvent event;
    KarqriTask task;
    KarqriScheduledEventWaiter waiter;
    KarqriEventWaitResult result;
    KarqriTask *ready[] = {&task};

    karqri_scheduler_init(&scheduler);
    (void)karqri_scheduler_set_reschedule_suppressed(&scheduler, true);
    karqri_event_init(&event);
    initialize_task(&task, 1U);
    karqri_scheduler_add_ready(&scheduler, &task);
    karqri_task_set_delayed_return(&task, UINT32_C(0xdead));
    result = karqri_scheduler_wait_event(
        &scheduler,
        &task,
        &waiter,
        &event,
        UINT32_C(0x1),
        KARQRI_EVENT_WAIT_ANY);

    expect(result.disposition == KARQRI_EVENT_WAIT_BLOCKED,
           "unsatisfied wait did not block");
    expect(result.events == 0U, "blocked wait returned a completed value");
    expect_ready_order(&scheduler, NULL, 0U);
    expect(karqri_task_state(&task) == KARQRI_TASK_STATE_EVENT_WAIT,
           "wait start did not establish event-wait state");
    expect(!karqri_task_has_delayed_return(&task),
           "wait start prematurely established a return value");
    expect(event.waiters.next == &waiter.event_waiter.wait_node,
           "waiter was not registered");
    expect(expect_links(&event.waiters, MAX_TASKS) == 1U,
           "unexpected event waiter count");

    karqri_event_signal(&event, UINT32_C(0x5));
    expect_ready_order(&scheduler, ready, 1U);
    expect(karqri_task_state(&task) == KARQRI_TASK_STATE_READY,
           "signal did not make the task logically ready");
    expect(karqri_task_has_delayed_return(&task) &&
               karqri_task_delayed_return(&task) == UINT32_C(0x5),
           "signal did not establish the deferred full snapshot return");
    expect(karqri_event_pending(&event) == UINT32_C(0x4),
           "blocking wait consumed the wrong bits");
    expect(expect_links(&event.waiters, MAX_TASKS) == 0U,
           "unexpected event waiter count");
    expect(waiter.event_waiter.wait_node.next == &waiter.event_waiter.wait_node &&
               waiter.event_waiter.wait_node.previous ==
                   &waiter.event_waiter.wait_node,
           "completed waiter was not detached");
}

static void test_scheduled_multiple_waiters_and_fifo_order(void)
{
    KarqriScheduler scheduler;
    KarqriEvent event;
    KarqriTask first;
    KarqriTask second;
    KarqriTask third;
    KarqriScheduledEventWaiter first_waiter;
    KarqriScheduledEventWaiter second_waiter;
    KarqriScheduledEventWaiter third_waiter;

    karqri_scheduler_init(&scheduler);
    (void)karqri_scheduler_set_reschedule_suppressed(&scheduler, true);
    karqri_event_init(&event);
    initialize_task(&first, 1U);
    karqri_scheduler_add_ready(&scheduler, &first);
    initialize_task(&second, 2U);
    karqri_scheduler_add_ready(&scheduler, &second);
    initialize_task(&third, 3U);
    karqri_scheduler_add_ready(&scheduler, &third);
    (void)karqri_scheduler_wait_event(
        &scheduler,
        &first,
        &first_waiter,
        &event,
        UINT32_C(0x1),
        KARQRI_EVENT_WAIT_ANY);
    (void)karqri_scheduler_wait_event(
        &scheduler,
        &second,
        &second_waiter,
        &event,
        UINT32_C(0x6),
        KARQRI_EVENT_WAIT_ALL);
    (void)karqri_scheduler_wait_event(
        &scheduler,
        &third,
        &third_waiter,
        &event,
        UINT32_C(0x4),
        KARQRI_EVENT_WAIT_ANY);
    expect(event.waiters.next == &first_waiter.event_waiter.wait_node &&
               first_waiter.event_waiter.wait_node.next ==
                   &second_waiter.event_waiter.wait_node &&
               second_waiter.event_waiter.wait_node.next ==
                   &third_waiter.event_waiter.wait_node,
           "event waiter FIFO registration order changed");
    expect(expect_links(&event.waiters, MAX_TASKS) == 3U,
           "unexpected event waiter count");

    karqri_event_signal(&event, UINT32_C(0x1));
    expect(karqri_task_state(&first) == KARQRI_TASK_STATE_READY &&
               karqri_task_state(&second) ==
                   KARQRI_TASK_STATE_EVENT_WAIT &&
               karqri_task_state(&third) == KARQRI_TASK_STATE_EVENT_WAIT,
           "first signal released the wrong tasks");
    expect(expect_links(&event.waiters, MAX_TASKS) == 2U,
           "unexpected event waiter count");

    karqri_event_signal(&event, UINT32_C(0x2));
    expect(karqri_task_state(&second) == KARQRI_TASK_STATE_EVENT_WAIT &&
               karqri_task_state(&third) == KARQRI_TASK_STATE_EVENT_WAIT,
           "partial ALL signal released a task");
    expect(karqri_event_pending(&event) == UINT32_C(0x2),
           "partial ALL signal was not retained");

    karqri_event_signal(&event, UINT32_C(0x4));
    expect(karqri_task_state(&second) == KARQRI_TASK_STATE_READY &&
               karqri_task_state(&third) == KARQRI_TASK_STATE_READY,
           "completed signal did not release matching tasks");
    expect(karqri_task_delayed_return(&second) == UINT32_C(0x6) &&
               karqri_task_delayed_return(&third) == UINT32_C(0x6),
           "matching tasks did not receive the common signal snapshot");
    expect(karqri_event_pending(&event) == 0U,
           "matched tasks left consumed bits pending");
    expect(expect_links(&event.waiters, MAX_TASKS) == 0U,
           "unexpected event waiter count");
}

static void test_scheduled_task_results_do_not_mix(void)
{
    KarqriScheduler scheduler;
    KarqriEvent first_event;
    KarqriEvent second_event;
    KarqriTask first;
    KarqriTask second;
    KarqriScheduledEventWaiter first_waiter;
    KarqriScheduledEventWaiter second_waiter;

    karqri_scheduler_init(&scheduler);
    (void)karqri_scheduler_set_reschedule_suppressed(&scheduler, true);
    karqri_event_init(&first_event);
    karqri_event_init(&second_event);
    initialize_task(&first, 1U);
    karqri_scheduler_add_ready(&scheduler, &first);
    initialize_task(&second, 2U);
    karqri_scheduler_add_ready(&scheduler, &second);
    (void)karqri_scheduler_wait_event(
        &scheduler,
        &first,
        &first_waiter,
        &first_event,
        UINT32_C(0x10),
        KARQRI_EVENT_WAIT_ANY);
    (void)karqri_scheduler_wait_event(
        &scheduler,
        &second,
        &second_waiter,
        &second_event,
        UINT32_C(0x20),
        KARQRI_EVENT_WAIT_ANY);

    karqri_event_signal(&second_event, UINT32_C(0x20));
    expect(karqri_task_state(&first) == KARQRI_TASK_STATE_EVENT_WAIT &&
               !karqri_task_has_delayed_return(&first),
           "second task signal changed first task state or value");
    expect(karqri_task_state(&second) == KARQRI_TASK_STATE_READY &&
               karqri_task_delayed_return(&second) == UINT32_C(0x20),
           "second task did not receive its own result");
}

static void test_remove_ready_and_final_current(void)
{
    KarqriScheduler scheduler;
    KarqriTask current;
    KarqriTask peer;
    KarqriTask *remaining[] = {&current};

    karqri_scheduler_init(&scheduler);
    initialize_task(&current, 30U);
    initialize_task(&peer, 30U);
    karqri_scheduler_add_ready(&scheduler, &current);
    karqri_scheduler_add_ready(&scheduler, &peer);

    karqri_scheduler_remove_task(&scheduler, &peer);
    expect_ready_order(&scheduler, remaining, 1U);
    expect(karqri_scheduler_current(&scheduler) == &current,
           "removing non-current ready task changed current");
    expect(karqri_task_state(&peer) == 0U &&
               peer.queue_node.next == &peer.queue_node &&
               peer.queue_node.previous == &peer.queue_node,
           "removed ready task retained state or links");

    karqri_scheduler_remove_task(&scheduler, &current);
    expect_ready_order(&scheduler, NULL, 0U);
    expect(karqri_scheduler_current(&scheduler) == NULL &&
               karqri_task_state(&current) == 0U &&
               current.queue_node.next == &current.queue_node &&
               current.queue_node.previous == &current.queue_node,
           "final current removal did not clear selection, state and links");
    expect(expect_links(&scheduler.suspended_tasks, 0U) == 0U,
           "final removal left suspended tasks");
    expect(!karqri_scheduler_reschedule(&scheduler),
           "finished scheduler reported another selection change");
}

static void test_non_current_same_priority_no_op(void)
{
    KarqriScheduler scheduler;
    KarqriTask tasks[3];
    KarqriTask *expected[] = {&tasks[0], &tasks[1], &tasks[2]};
    size_t index;

    karqri_scheduler_init(&scheduler);
    for (index = 0U; index < 3U; index++) {
        initialize_task(&tasks[index], 30U);
        karqri_scheduler_add_ready(&scheduler, &tasks[index]);
    }
    expect(karqri_scheduler_set_priority(
               &scheduler, &tasks[1], 30U) == 30U,
           "non-current same-value priority returned wrong value");
    expect_ready_order(&scheduler, expected, 3U);
    expect(karqri_scheduler_current(&scheduler) == &tasks[0] &&
               karqri_task_state(&tasks[1]) == KARQRI_TASK_STATE_READY,
           "non-current same-value priority changed selection or state");
}

static void test_rotation_without_equal_peer(void)
{
    KarqriScheduler scheduler;
    KarqriTask high;
    KarqriTask low;
    KarqriTask *single[] = {&high};
    KarqriTask *mixed[] = {&high, &low};

    karqri_scheduler_init(&scheduler);
    initialize_task(&high, 30U);
    initialize_task(&low, 10U);
    karqri_scheduler_add_ready(&scheduler, &high);
    expect(!karqri_scheduler_rotate_timeslice(&scheduler),
           "single-task rotation reported selection change");
    expect_ready_order(&scheduler, single, 1U);
    expect(karqri_scheduler_current(&scheduler) == &high,
           "single-task rotation lost current");

    karqri_scheduler_add_ready(&scheduler, &low);
    expect(!karqri_scheduler_rotate_timeslice(&scheduler),
           "rotation without equal peer selected a lower-priority task");
    expect_ready_order(&scheduler, mixed, 2U);
    expect(karqri_scheduler_current(&scheduler) == &high,
           "rotation without equal peer changed current");
}

static void test_resume_while_event_wait_continues(void)
{
    KarqriScheduler scheduler;
    KarqriTask waiting;
    KarqriTask peer;
    KarqriEvent event;
    KarqriScheduledEventWaiter waiter;
    KarqriTask *blocked[] = {&peer};
    KarqriTask *woken[] = {&waiting, &peer};

    karqri_scheduler_init(&scheduler);
    karqri_event_init(&event);
    initialize_task(&waiting, 30U);
    initialize_task(&peer, 10U);
    karqri_scheduler_add_ready(&scheduler, &peer);
    karqri_scheduler_add_ready(&scheduler, &waiting);
    expect(karqri_scheduler_wait_event(
               &scheduler, &waiting, &waiter, &event,
               UINT32_C(0x01), KARQRI_EVENT_WAIT_ANY).disposition ==
               KARQRI_EVENT_WAIT_BLOCKED,
           "resume-while-waiting setup did not block");
    expect(karqri_scheduler_suspend(&scheduler, &waiting) &&
               karqri_scheduler_resume(&scheduler, &waiting),
           "waiting task suspend/resume failed");
    expect_ready_order(&scheduler, blocked, 1U);
    expect(karqri_scheduler_current(&scheduler) == &peer &&
               karqri_task_state(&waiting) == KARQRI_TASK_STATE_EVENT_WAIT &&
               !karqri_task_has_delayed_return(&waiting),
           "resume completed an outstanding event wait");
    expect(expect_links(&event.waiters, 1U) == 1U &&
               event.waiters.next == &waiter.event_waiter.wait_node,
           "resume detached the outstanding event waiter");
    expect(expect_links(&scheduler.suspended_tasks, 0U) == 0U,
           "waiting resume left a suspended queue entry");

    karqri_event_signal(&event, UINT32_C(0x01));
    expect_ready_order(&scheduler, woken, 2U);
    expect(karqri_scheduler_current(&scheduler) == &waiting &&
               karqri_task_state(&waiting) == KARQRI_TASK_STATE_RUNNING &&
               karqri_task_has_delayed_return(&waiting) &&
               karqri_task_delayed_return(&waiting) == UINT32_C(0x01),
           "event completion after resume did not wake the waiting task");
    expect(expect_links(&event.waiters, 0U) == 0U,
           "completed event waiter remained registered");
}

int main(void)
{
    current_test = "remove ready and final current";
    test_remove_ready_and_final_current();
    current_test = "non-current same-priority no-op";
    test_non_current_same_priority_no_op();
    current_test = "rotation without equal peer";
    test_rotation_without_equal_peer();
    current_test = "resume while event wait continues";
    test_resume_while_event_wait_continues();
    current_test = "initialization";
    test_initialization();
    current_test = "ready priority and equal FIFO";
    test_ready_priority_and_equal_fifo();
    current_test = "higher priority switch and suppression";
    test_higher_priority_switch_and_suppression();
    current_test = "suspend and resume";
    test_suspend_and_resume();
    current_test = "same-priority resume FIFO";
    test_same_priority_resume_fifo();
    current_test = "priority change";
    test_priority_change();
    current_test = "same-priority change FIFO and no-op";
    test_same_priority_change_fifo_and_no_op();
    current_test = "timeslice rotation";
    test_timeslice_rotation();
    current_test = "interrupted task timeslice rotation";
    test_interrupted_task_timeslice_rotation();
    current_test = "event wait and wakeup";
    test_event_wait_and_wakeup();
    current_test = "same-priority event wakeup FIFO";
    test_same_priority_event_wakeup_fifo();
    current_test = "suspended event waiter";
    test_suspended_event_waiter();
    current_test = "timer event scheduler path";
    test_timer_event_scheduler_path();
    current_test = "same-priority timer wakeup FIFO";
    test_same_priority_timer_wakeup_fifo();
    current_test = "long scheduler sequence";
    test_long_scheduler_sequence();
    current_test = "test_scheduled_immediate_any_and_all_wait";
    test_scheduled_immediate_any_and_all_wait();
    current_test = "immediate wait preserves valid result";
    test_scheduled_immediate_wait_preserves_valid_result();
    current_test = "test_scheduled_blocking_wait_and_deferred_return";
    test_scheduled_blocking_wait_and_deferred_return();
    current_test = "test_scheduled_multiple_waiters_and_fifo_order";
    test_scheduled_multiple_waiters_and_fifo_order();
    current_test = "test_scheduled_task_results_do_not_mix";
    test_scheduled_task_results_do_not_mix();
    puts("scheduler validation: PASS");
    return EXIT_SUCCESS;
}
