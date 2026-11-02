#include "karqri/event.h"
#include "karqri/timer.h"
#include "karqri/timer_event.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

#include "event_probe.h"

static void test_timer_expiry_signals_events(void)
{
    KarqriTimerQueue timers;
    KarqriTimer timer_pool[4];
    KarqriEvent first_event;
    KarqriEvent second_event;
    EventProbe probe = {0};
    KarqriEventWaiter waiter;
    KarqriEventWaitResult wait_result;

    karqri_timer_queue_init(&timers, timer_pool, 4U);
    karqri_event_init(&first_event);
    karqri_event_init(&second_event);

    wait_result = karqri_event_wait(
        &first_event, &waiter, UINT32_C(0x03), KARQRI_EVENT_WAIT_ALL,
        &probe, &probe_operations);
    expect(wait_result.disposition == KARQRI_EVENT_WAIT_BLOCKED,
           "event waiter did not block before timer start");

    expect(karqri_timer_start(
               &timers,
               &first_event,
               UINT32_C(0x01),
               1U,
               karqri_timer_signal_event) &&
               karqri_timer_start(
                   &timers,
                   &second_event,
                   UINT32_C(0x08),
                   1U,
                   karqri_timer_signal_event) &&
               karqri_timer_start(
                   &timers,
                   &first_event,
                   UINT32_C(0x02),
                   1U,
                   karqri_timer_signal_event),
           "event timer setup failed");

    karqri_timer_tick(&timers);

    expect(probe.started == 1U && probe.completed == 1U,
           "timer expiry did not release the event waiter");
    expect(probe.returned == 1U &&
               probe.events == UINT32_C(0x03),
           "timer expiry established the wrong event return value");
    expect(karqri_event_pending(&first_event) == 0U,
           "fulfilled event retained consumed timer signals");
    expect(karqri_event_pending(&second_event) == UINT32_C(0x08),
           "timer did not signal the independent event");
    expect(karqri_queue_is_empty(&timers.active_timers),
           "expired event timers remain active");
}

static void test_cancel_preserves_pending_and_waiter(void)
{
    KarqriTimerQueue timers;
    KarqriTimer timer;
    KarqriEvent event;
    KarqriEventWaiter waiter;
    EventProbe probe = {0};

    karqri_timer_queue_init(&timers, &timer, 1U);
    karqri_event_init(&event);
    karqri_event_signal(&event, UINT32_C(0x01));
    expect(karqri_event_wait(
               &event, &waiter, UINT32_C(0x03), KARQRI_EVENT_WAIT_ALL,
               &probe, &probe_operations).disposition == KARQRI_EVENT_WAIT_BLOCKED,
           "cancel setup did not block on partial pending");
    expect(karqri_timer_start(
               &timers, &event, UINT32_C(0x02), 1U, karqri_timer_signal_event) &&
               karqri_timer_cancel(&timers, &event, UINT32_C(0x02)),
           "event timer start/cancel failed");
    karqri_timer_tick(&timers);
    expect(karqri_event_pending(&event) == UINT32_C(0x01) &&
               event.waiters.next == &waiter.wait_node &&
               event.waiters.previous == &waiter.wait_node &&
               waiter.wait_node.next == &event.waiters &&
               waiter.wait_node.previous == &event.waiters &&
               probe.started == 1U && probe.returned == 0U &&
               probe.completed == 0U && probe.events == 0U,
           "cancel cleared pending, detached waiter or notified completion");

    karqri_event_signal(&event, UINT32_C(0x02));
    expect(probe.completed == 1U && probe.events == UINT32_C(0x03) &&
               karqri_event_pending(&event) == 0U &&
               karqri_queue_is_empty(&event.waiters),
           "separate signal did not complete the retained waiter");
}

static void test_pool_exhaustion_preserves_event(void)
{
    KarqriTimerQueue timers;
    KarqriTimer timer;
    KarqriEvent event;
    KarqriEventWaiter waiter;
    EventProbe probe = {0};

    karqri_timer_queue_init(&timers, &timer, 1U);
    karqri_event_init(&event);
    karqri_event_signal(&event, UINT32_C(0x01));
    expect(karqri_event_wait(
               &event, &waiter, UINT32_C(0x03), KARQRI_EVENT_WAIT_ALL,
               &probe, &probe_operations).disposition == KARQRI_EVENT_WAIT_BLOCKED,
           "exhaustion setup did not block");
    expect(karqri_timer_start(
               &timers, &event, UINT32_C(0x02), 2U, karqri_timer_signal_event),
           "exhaustion setup timer failed");
    expect(!karqri_timer_start(
               &timers, &event, UINT32_C(0x04), 1U, karqri_timer_signal_event),
           "exhausted pool accepted another event timer");
    expect(karqri_event_pending(&event) == UINT32_C(0x01) &&
               event.waiters.next == &waiter.wait_node &&
               event.waiters.previous == &waiter.wait_node &&
               waiter.wait_node.next == &event.waiters &&
               waiter.wait_node.previous == &event.waiters &&
               probe.started == 1U && probe.returned == 0U &&
               probe.completed == 0U && probe.events == 0U,
           "failed start changed event pending, waiter or callback result");
    karqri_timer_tick(&timers);
    expect(karqri_event_pending(&event) == UINT32_C(0x01) &&
               probe.returned == 0U && probe.completed == 0U,
           "failed timer produced a signal or changed the existing expiry");
    karqri_timer_tick(&timers);
    expect(probe.completed == 1U && probe.events == UINT32_C(0x03) &&
               karqri_event_pending(&event) == 0U &&
               karqri_queue_is_empty(&event.waiters) &&
               karqri_queue_is_empty(&timers.active_timers),
           "existing timer failed after pool exhaustion");
}

static void test_same_tick_signals_have_separate_snapshots(void)
{
    KarqriTimerQueue timers;
    KarqriTimer pool[2];
    KarqriEvent event;
    KarqriEventWaiter first_waiter;
    KarqriEventWaiter second_waiter;
    EventProbe first = {0};
    EventProbe second = {0};

    karqri_timer_queue_init(&timers, pool, 2U);
    karqri_event_init(&event);
    (void)karqri_event_wait(
        &event, &first_waiter, UINT32_C(0x01), KARQRI_EVENT_WAIT_ANY,
        &first, &probe_operations);
    (void)karqri_event_wait(
        &event, &second_waiter, UINT32_C(0x02), KARQRI_EVENT_WAIT_ANY,
        &second, &probe_operations);
    expect(karqri_timer_start(
               &timers, &event, UINT32_C(0x05), 1U, karqri_timer_signal_event) &&
               karqri_timer_start(
                   &timers, &event, UINT32_C(0x02), 1U, karqri_timer_signal_event),
           "same-tick independent signal setup failed");
    karqri_timer_tick(&timers);
    expect(first.completed == 1U && first.events == UINT32_C(0x05) &&
               second.completed == 1U && second.events == UINT32_C(0x06),
           "same-tick signals merged or retained previously consumed bits");
    expect(karqri_event_pending(&event) == UINT32_C(0x04) &&
               karqri_queue_is_empty(&event.waiters) &&
               karqri_queue_is_empty(&timers.active_timers),
           "same-tick signal processing consumed wrong bits or left registrations");
}

static void test_expiry_before_wait_and_pool_reuse(void)
{
    KarqriTimerQueue timers;
    KarqriTimer timer;
    KarqriEvent event;
    KarqriEventWaiter waiter;
    EventProbe probe = {0};
    KarqriEventWaitResult result;

    karqri_timer_queue_init(&timers, &timer, 1U);
    karqri_event_init(&event);
    expect(karqri_timer_start(
               &timers, &event, UINT32_C(0x80000002), 1U, karqri_timer_signal_event),
           "expiry-before-wait timer start failed");
    karqri_timer_tick(&timers);
    expect(karqri_event_pending(&event) == UINT32_C(0x80000002),
           "timer signal without waiters was not retained");
    result = karqri_event_wait(
        &event, &waiter, UINT32_C(0x80000000), KARQRI_EVENT_WAIT_ANY,
        &probe, &probe_operations);
    expect(result.disposition == KARQRI_EVENT_WAIT_COMPLETED &&
               result.events == UINT32_C(0x80000002) &&
               karqri_event_pending(&event) == UINT32_C(0x02),
           "wait after expiry did not return and consume the high-bit snapshot");

    expect(karqri_timer_start(
               &timers, &event, 0U, 1U, karqri_timer_signal_event),
           "expired pool entry was not reusable for zero signal");
    karqri_timer_tick(&timers);
    expect(karqri_event_pending(&event) == UINT32_C(0x02) &&
               karqri_queue_is_empty(&timers.active_timers),
           "zero signal changed pending or failed to expire");
    result = karqri_event_wait(
        &event, &waiter, UINT32_C(0x02), KARQRI_EVENT_WAIT_ALL,
        &probe, &probe_operations);
    expect(result.disposition == KARQRI_EVENT_WAIT_COMPLETED &&
               result.events == UINT32_C(0x02) &&
               karqri_event_pending(&event) == 0U &&
               karqri_queue_is_empty(&event.waiters) &&
               probe.started == 0U && probe.returned == 0U && probe.completed == 0U,
           "immediate waits registered or notified a waiter");
}

int main(void)
{
    test_timer_expiry_signals_events();
    test_cancel_preserves_pending_and_waiter();
    test_pool_exhaustion_preserves_event();
    test_same_tick_signals_have_separate_snapshots();
    test_expiry_before_wait_and_pool_reuse();
    puts("timer/event validation: PASS");
    return EXIT_SUCCESS;
}
