#include "karqri/event.h"

#include <stdbool.h>
#include <stddef.h>
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

static void expect_waiter_links(
    const KarqriEvent *event,
    size_t expected_count)
{
    const KarqriQueueNode *node = event->waiters.next;
    const KarqriQueueNode *previous = &event->waiters;
    size_t count = 0;

    while (node != &event->waiters) {
        expect(node->previous == previous, "waiter previous link broken");
        previous = node;
        node = node->next;
        count++;
        expect(count <= expected_count, "waiter queue contains a cycle");
    }

    expect(previous == event->waiters.previous,
           "waiter queue tail mismatch");
    expect(count == expected_count, "unexpected waiter count");
}

static void test_initialization_and_signal_without_waiters(void)
{
    KarqriEvent event;

    karqri_event_init(&event);
    expect(karqri_event_pending(&event) == 0U,
           "new event has pending bits");
    expect_waiter_links(&event, 0U);

    karqri_event_signal(&event, UINT32_C(0x5));
    expect(karqri_event_pending(&event) == UINT32_C(0x5),
           "signal without waiters was not retained");
    expect_waiter_links(&event, 0U);
}

static void test_immediate_any_and_all_wait(void)
{
    KarqriEvent event;
    EventProbe probe = {0};
    KarqriEventWaiter waiter;
    KarqriEventWaitResult result;

    karqri_event_init(&event);
    karqri_event_signal(&event, UINT32_C(0x5));

    result = karqri_event_wait(
        &event, &waiter, UINT32_C(0x1), KARQRI_EVENT_WAIT_ANY,
        &probe, &probe_operations);
    expect(result.disposition == KARQRI_EVENT_WAIT_COMPLETED,
           "satisfied ANY wait did not complete immediately");
    expect(result.events == UINT32_C(0x5),
           "ANY wait did not return the full event snapshot");
    expect(karqri_event_pending(&event) == UINT32_C(0x4),
           "ANY wait consumed the wrong event bits");

    karqri_event_signal(&event, UINT32_C(0x2));
    result = karqri_event_wait(
        &event, &waiter, UINT32_C(0x6), KARQRI_EVENT_WAIT_ALL,
        &probe, &probe_operations);
    expect(result.disposition == KARQRI_EVENT_WAIT_COMPLETED,
           "satisfied ALL wait did not complete immediately");
    expect(result.events == UINT32_C(0x6),
           "ALL wait returned the wrong event snapshot");
    expect(karqri_event_pending(&event) == 0U,
           "ALL wait did not consume its requested bits");
    expect(probe.started == 0U && probe.completed == 0U &&
               probe.returned == 0U,
           "immediate wait unexpectedly changed callback counts");
}

static void test_blocking_wait_and_notifications(void)
{
    KarqriEvent event;
    EventProbe probe = {0};
    KarqriEventWaiter waiter;
    KarqriEventWaitResult result;

    karqri_event_init(&event);
    result = karqri_event_wait(
        &event, &waiter, UINT32_C(0x1), KARQRI_EVENT_WAIT_ANY,
        &probe, &probe_operations);

    expect(result.disposition == KARQRI_EVENT_WAIT_BLOCKED,
           "unsatisfied wait did not block");
    expect(result.events == 0U, "blocked wait returned a completed value");
    expect(probe.started == 1U && probe.completed == 0U,
           "wait start did not notify the owner");
    expect(probe.returned == 0U,
           "wait start prematurely established a return value");
    expect(event.waiters.next == &waiter.wait_node,
           "waiter was not registered");
    expect_waiter_links(&event, 1U);

    karqri_event_signal(&event, UINT32_C(0x5));
    expect(probe.completed == 1U,
           "signal did not notify wait completion");
    expect(probe.returned == 1U &&
               probe.events == UINT32_C(0x5),
           "signal did not notify the full snapshot result");
    expect(karqri_event_pending(&event) == UINT32_C(0x4),
           "blocking wait consumed the wrong bits");
    expect_waiter_links(&event, 0U);
    expect(waiter.wait_node.next == &waiter.wait_node &&
               waiter.wait_node.previous == &waiter.wait_node,
           "completed waiter was not detached");
}

static void test_multiple_waiters_and_fifo_order(void)
{
    KarqriEvent event;
    EventProbe first = {0};
    EventProbe second = {0};
    EventProbe third = {0};
    KarqriEventWaiter first_waiter;
    KarqriEventWaiter second_waiter;
    KarqriEventWaiter third_waiter;

    karqri_event_init(&event);
    (void)karqri_event_wait(
        &event, &first_waiter, UINT32_C(0x1), KARQRI_EVENT_WAIT_ANY,
        &first, &probe_operations);
    (void)karqri_event_wait(
        &event, &second_waiter, UINT32_C(0x6), KARQRI_EVENT_WAIT_ALL,
        &second, &probe_operations);
    (void)karqri_event_wait(
        &event, &third_waiter, UINT32_C(0x4), KARQRI_EVENT_WAIT_ANY,
        &third, &probe_operations);
    expect(event.waiters.next == &first_waiter.wait_node &&
               first_waiter.wait_node.next == &second_waiter.wait_node &&
               second_waiter.wait_node.next == &third_waiter.wait_node,
           "event waiter FIFO registration order changed");
    expect_waiter_links(&event, 3U);

    karqri_event_signal(&event, UINT32_C(0x1));
    expect(first.completed == 1U &&
               (second.started == 1U && second.completed == 0U) &&
               (third.started == 1U && third.completed == 0U),
           "first signal released the wrong owners");
    expect_waiter_links(&event, 2U);

    karqri_event_signal(&event, UINT32_C(0x2));
    expect((second.started == 1U && second.completed == 0U) &&
               (third.started == 1U && third.completed == 0U),
           "partial ALL signal released a probe");
    expect(karqri_event_pending(&event) == UINT32_C(0x2),
           "partial ALL signal was not retained");

    karqri_event_signal(&event, UINT32_C(0x4));
    expect(second.completed == 1U &&
               third.completed == 1U,
           "completed signal did not release matching owners");
    expect(second.events == UINT32_C(0x6) &&
               third.events == UINT32_C(0x6),
           "matching owners did not receive the common signal snapshot");
    expect(karqri_event_pending(&event) == 0U,
           "matched owners left consumed bits pending");
    expect_waiter_links(&event, 0U);
}

static void test_owner_results_do_not_mix(void)
{
    KarqriEvent first_event;
    KarqriEvent second_event;
    EventProbe first = {0};
    EventProbe second = {0};
    KarqriEventWaiter first_waiter;
    KarqriEventWaiter second_waiter;

    karqri_event_init(&first_event);
    karqri_event_init(&second_event);
    (void)karqri_event_wait(
        &first_event, &first_waiter, UINT32_C(0x10), KARQRI_EVENT_WAIT_ANY,
        &first, &probe_operations);
    (void)karqri_event_wait(
        &second_event, &second_waiter, UINT32_C(0x20), KARQRI_EVENT_WAIT_ANY,
        &second, &probe_operations);

    karqri_event_signal(&second_event, UINT32_C(0x20));
    expect((first.started == 1U && first.completed == 0U) &&
               first.returned == 0U,
           "second probe signal changed first owner notifications");
    expect(second.completed == 1U &&
               second.events == UINT32_C(0x20),
           "second probe did not receive its own result");
}

static void test_completed_waiter_reuse_and_high_bit(void)
{
    KarqriEvent event;
    KarqriEventWaiter waiter;
    EventProbe first = {0};
    EventProbe second = {0};
    KarqriEventWaitResult result;

    karqri_event_init(&event);
    result = karqri_event_wait(
        &event, &waiter, UINT32_C(0x80000000), KARQRI_EVENT_WAIT_ANY,
        &first, &probe_operations);
    expect(result.disposition == KARQRI_EVENT_WAIT_BLOCKED,
           "high-bit ANY wait did not block");
    karqri_event_signal(&event, UINT32_C(0x80000002));
    expect(first.completed == 1U && first.events == UINT32_C(0x80000002),
           "high-bit ANY wait lost the full snapshot");
    expect(karqri_event_pending(&event) == UINT32_C(0x02) &&
               waiter.wait_node.next == &waiter.wait_node &&
               waiter.wait_node.previous == &waiter.wait_node,
           "high-bit completion did not consume and detach correctly");

    result = karqri_event_wait(
        &event, &waiter, UINT32_C(0x80000002), KARQRI_EVENT_WAIT_ALL,
        &second, &probe_operations);
    expect(result.disposition == KARQRI_EVENT_WAIT_BLOCKED,
           "reused high-bit ALL waiter did not block on partial pending");
    expect_waiter_links(&event, 1U);
    karqri_event_signal(&event, UINT32_C(0x80000000));
    expect(second.completed == 1U && second.events == UINT32_C(0x80000002) &&
               first.completed == 1U && first.returned == 1U,
           "reused waiter did not notify only its new owner");
    expect(karqri_event_pending(&event) == 0U &&
               waiter.wait_node.next == &waiter.wait_node &&
               waiter.wait_node.previous == &waiter.wait_node,
           "reused waiter completion left pending bits or links");
    expect_waiter_links(&event, 0U);
}

static void test_unsatisfied_first_waiter_does_not_block_later(void)
{
    KarqriEvent event;
    KarqriEventWaiter first_waiter;
    KarqriEventWaiter later_waiter;
    EventProbe first = {0};
    EventProbe later = {0};

    karqri_event_init(&event);
    (void)karqri_event_wait(
        &event, &first_waiter, UINT32_C(0x03), KARQRI_EVENT_WAIT_ALL,
        &first, &probe_operations);
    (void)karqri_event_wait(
        &event, &later_waiter, UINT32_C(0x04), KARQRI_EVENT_WAIT_ANY,
        &later, &probe_operations);
    karqri_event_signal(&event, UINT32_C(0x05));
    expect(first.started == 1U && first.returned == 0U && first.completed == 0U &&
               later.completed == 1U && later.events == UINT32_C(0x05),
           "unsatisfied first waiter prevented later completion");
    expect(karqri_event_pending(&event) == UINT32_C(0x01) &&
               event.waiters.next == &first_waiter.wait_node &&
               later_waiter.wait_node.next == &later_waiter.wait_node &&
               later_waiter.wait_node.previous == &later_waiter.wait_node,
           "later completion changed the retained wait or wrong pending bits");
    expect_waiter_links(&event, 1U);
    karqri_event_signal(&event, UINT32_C(0x02));
    expect(first.completed == 1U && first.events == UINT32_C(0x03) &&
               later.completed == 1U && karqri_event_pending(&event) == 0U,
           "retained first waiter did not complete on remaining bits");
    expect_waiter_links(&event, 0U);
}

static void test_duplicate_signals_do_not_accumulate_counts(void)
{
    KarqriEvent event;
    KarqriEventWaiter waiter;
    EventProbe probe = {0};
    KarqriEventWaitResult result;

    karqri_event_init(&event);
    karqri_event_signal(&event, UINT32_C(0x01));
    karqri_event_signal(&event, UINT32_C(0x01));
    expect(karqri_event_pending(&event) == UINT32_C(0x01),
           "duplicate signal changed the pending bit set");
    result = karqri_event_wait(
        &event, &waiter, UINT32_C(0x01), KARQRI_EVENT_WAIT_ANY,
        &probe, &probe_operations);
    expect(result.disposition == KARQRI_EVENT_WAIT_COMPLETED &&
               result.events == UINT32_C(0x01) &&
               karqri_event_pending(&event) == 0U && probe.started == 0U,
           "first wait did not consume the single pending bit");
    result = karqri_event_wait(
        &event, &waiter, UINT32_C(0x01), KARQRI_EVENT_WAIT_ANY,
        &probe, &probe_operations);
    expect(result.disposition == KARQRI_EVENT_WAIT_BLOCKED && result.events == 0U &&
               probe.started == 1U && probe.completed == 0U,
           "duplicate signal left an extra notification count");
    expect_waiter_links(&event, 1U);
    karqri_event_signal(&event, UINT32_C(0x01));
    expect(probe.completed == 1U && probe.events == UINT32_C(0x01),
           "a fresh signal did not complete the second wait");
    expect_waiter_links(&event, 0U);
}

int main(void)
{
    test_initialization_and_signal_without_waiters();
    test_immediate_any_and_all_wait();
    test_blocking_wait_and_notifications();
    test_multiple_waiters_and_fifo_order();
    test_owner_results_do_not_mix();
    test_completed_waiter_reuse_and_high_bit();
    test_unsatisfied_first_waiter_does_not_block_later();
    test_duplicate_signals_do_not_accumulate_counts();
    puts("event validation: PASS");
    return EXIT_SUCCESS;
}
