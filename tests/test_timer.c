#include "karqri/timer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define TIMER_COUNT 6U

typedef struct ExpiryLog {
    uint32_t events[16];
    size_t count;
} ExpiryLog;

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

static KarqriTimer *timer_from_node(const KarqriQueueNode *node)
{
    return (KarqriTimer *)((unsigned char *)node -
                           offsetof(KarqriTimer, timer_node));
}

static void record_expiry(void *destination, uint32_t events)
{
    ExpiryLog *log = destination;

    expect(log->count < sizeof(log->events) / sizeof(log->events[0]),
           "expiry log overflow");
    log->events[log->count] = events;
    log->count++;
}

static size_t expect_queue_links(
    const KarqriQueueNode *queue,
    size_t maximum_count)
{
    const KarqriQueueNode *node = queue->next;
    const KarqriQueueNode *previous = queue;
    const KarqriQueueNode *next;
    size_t forward_count = 0;
    size_t reverse_count = 0;

    while (node != queue) {
        expect(node->previous == previous, "timer previous link broken");
        previous = node;
        node = node->next;
        forward_count++;
        expect(forward_count <= maximum_count,
               "timer forward links contain a cycle");
    }
    expect(previous == queue->previous, "timer queue tail mismatch");

    next = queue;
    node = queue->previous;
    while (node != queue) {
        expect(node->next == next, "timer next link broken");
        next = node;
        node = node->previous;
        reverse_count++;
        expect(reverse_count <= maximum_count,
               "timer reverse links contain a cycle");
    }
    expect(next == queue->next, "timer queue head mismatch");
    expect(reverse_count == forward_count,
           "timer forward/reverse counts differ");
    return forward_count;
}

static void expect_queue_counts(
    const KarqriTimerQueue *queue,
    size_t active_count,
    size_t free_count)
{
    expect(expect_queue_links(&queue->active_timers, TIMER_COUNT) ==
               active_count,
           "unexpected active timer count");
    expect(expect_queue_links(&queue->free_timers, TIMER_COUNT) == free_count,
           "unexpected free timer count");
}

static void expect_active_deltas(
    const KarqriTimerQueue *queue,
    const uint32_t *events,
    const uint32_t *deltas,
    size_t count)
{
    const KarqriQueueNode *node = queue->active_timers.next;
    size_t index;

    for (index = 0; index < count; index++) {
        const KarqriTimer *timer;

        expect(node != &queue->active_timers,
               "active timer queue ended early");
        timer = timer_from_node(node);
        expect(timer->events == events[index],
               "active timer event order mismatch");
        expect(timer->remaining_ticks == deltas[index],
               "active timer delta mismatch");
        node = node->next;
    }
    expect(node == &queue->active_timers,
           "active timer queue contains extra entries");
    expect_queue_links(&queue->active_timers, TIMER_COUNT);
}

static void test_initialization_and_empty_tick(void)
{
    KarqriTimerQueue queue;
    KarqriTimer timers[TIMER_COUNT];

    karqri_timer_queue_init(&queue, timers, TIMER_COUNT);
    expect_queue_counts(&queue, 0U, TIMER_COUNT);
    karqri_timer_tick(&queue);
    expect_queue_counts(&queue, 0U, TIMER_COUNT);
}

static void test_delta_insertion_and_equal_order(void)
{
    KarqriTimerQueue queue;
    KarqriTimer timers[TIMER_COUNT];
    ExpiryLog log = {{0U}, 0U};
    const uint32_t expected_events[] = {
        UINT32_C(0x02),
        UINT32_C(0x04),
        UINT32_C(0x08),
        UINT32_C(0x10),
        UINT32_C(0x01),
    };
    const uint32_t expected_deltas[] = {20U, 30U, 0U, 20U, 30U};

    karqri_timer_queue_init(&queue, timers, TIMER_COUNT);
    expect(karqri_timer_start(
               &queue, &log, UINT32_C(0x01), 100U, record_expiry),
           "single tail timer insertion failed");
    expect(karqri_timer_start(
               &queue, &log, UINT32_C(0x02), 20U, record_expiry),
           "timer insertion before head failed");
    expect(karqri_timer_start(
               &queue, &log, UINT32_C(0x04), 50U, record_expiry),
           "timer insertion in middle failed");
    expect(karqri_timer_start(
               &queue, &log, UINT32_C(0x08), 50U, record_expiry),
           "equal-expiry timer insertion failed");
    expect(karqri_timer_start(
               &queue, &log, UINT32_C(0x10), 70U, record_expiry),
           "second middle insertion failed");

    expect_active_deltas(
        &queue,
        expected_events,
        expected_deltas,
        sizeof(expected_events) / sizeof(expected_events[0]));
    expect_queue_counts(&queue, 5U, 1U);
}

static void test_pool_exhaustion(void)
{
    KarqriTimerQueue queue;
    KarqriTimer timer;
    ExpiryLog log = {{0U}, 0U};

    karqri_timer_queue_init(&queue, &timer, 1U);
    expect(karqri_timer_start(
               &queue, &log, UINT32_C(0x01), 1U, record_expiry),
           "only pool timer could not be started");
    expect(!karqri_timer_start(
               &queue, &log, UINT32_C(0x02), 2U, record_expiry),
           "start succeeded with an exhausted timer pool");
    expect(expect_queue_links(&queue.active_timers, 1U) == 1U &&
               expect_queue_links(&queue.free_timers, 1U) == 0U,
           "pool exhaustion changed timer queues");
}

static void test_tick_progression_and_expiry_order(void)
{
    KarqriTimerQueue queue;
    KarqriTimer timers[TIMER_COUNT];
    ExpiryLog log = {{0U}, 0U};
    const uint32_t before_events[] = {
        UINT32_C(0x01), UINT32_C(0x02), UINT32_C(0x04),
    };
    const uint32_t initial_deltas[] = {2U, 2U, 0U};
    const uint32_t after_tick_deltas[] = {1U, 2U, 0U};
    const uint32_t after_first_expiry_events[] = {
        UINT32_C(0x02), UINT32_C(0x04),
    };
    const uint32_t after_first_expiry_deltas[] = {2U, 0U};

    karqri_timer_queue_init(&queue, timers, TIMER_COUNT);
    expect(karqri_timer_start(
               &queue, &log, UINT32_C(0x01), 2U, record_expiry) &&
               karqri_timer_start(
                   &queue, &log, UINT32_C(0x02), 4U, record_expiry) &&
               karqri_timer_start(
                   &queue, &log, UINT32_C(0x04), 4U, record_expiry),
           "expiry-order setup failed");
    expect_active_deltas(
        &queue, before_events, initial_deltas, 3U);

    karqri_timer_tick(&queue);
    expect(log.count == 0U, "timer expired one tick too early");
    expect_active_deltas(
        &queue, before_events, after_tick_deltas, 3U);

    karqri_timer_tick(&queue);
    expect(log.count == 1U && log.events[0] == UINT32_C(0x01),
           "single timer expiry mismatch");
    expect_active_deltas(
        &queue,
        after_first_expiry_events,
        after_first_expiry_deltas,
        2U);

    karqri_timer_tick(&queue);
    expect(log.count == 1U, "remaining timers expired too early");
    karqri_timer_tick(&queue);
    expect(log.count == 3U &&
               log.events[1] == UINT32_C(0x02) &&
               log.events[2] == UINT32_C(0x04),
           "same-tick timers did not expire in insertion order");
    expect_queue_counts(&queue, 0U, TIMER_COUNT);
}

static void test_cancel_and_delta_repair(void)
{
    KarqriTimerQueue queue;
    KarqriTimer timers[TIMER_COUNT];
    ExpiryLog log = {{0U}, 0U};
    const uint32_t after_middle_events[] = {
        UINT32_C(0x02), UINT32_C(0x08), UINT32_C(0x01),
    };
    const uint32_t after_middle_deltas[] = {20U, 30U, 50U};
    const uint32_t after_head_events[] = {
        UINT32_C(0x08), UINT32_C(0x01),
    };
    const uint32_t after_head_deltas[] = {50U, 50U};
    const uint32_t after_tail_events[] = {UINT32_C(0x08)};
    const uint32_t after_tail_deltas[] = {50U};

    karqri_timer_queue_init(&queue, timers, TIMER_COUNT);
    expect(karqri_timer_start(
               &queue, &log, UINT32_C(0x01), 100U, record_expiry) &&
               karqri_timer_start(
                   &queue, &log, UINT32_C(0x02), 20U, record_expiry) &&
               karqri_timer_start(
                   &queue, &log, UINT32_C(0x04), 50U, record_expiry) &&
               karqri_timer_start(
                   &queue, &log, UINT32_C(0x08), 50U, record_expiry),
           "cancel setup failed");

    expect(karqri_timer_cancel(&queue, &log, UINT32_C(0x04)),
           "middle timer cancel failed");
    expect_active_deltas(
        &queue, after_middle_events, after_middle_deltas, 3U);

    expect(karqri_timer_cancel(&queue, &log, UINT32_C(0x02)),
           "head timer cancel failed");
    expect_active_deltas(
        &queue, after_head_events, after_head_deltas, 2U);

    expect(karqri_timer_cancel(&queue, &log, UINT32_C(0x01)),
           "tail timer cancel failed");
    expect_active_deltas(
        &queue, after_tail_events, after_tail_deltas, 1U);

    expect(!karqri_timer_cancel(&queue, &log, UINT32_C(0x40)),
           "missing timer cancel succeeded");
    expect_active_deltas(
        &queue, after_tail_events, after_tail_deltas, 1U);

    expect(karqri_timer_cancel(&queue, &log, UINT32_C(0x08)),
           "last timer cancel failed");
    expect_queue_counts(&queue, 0U, TIMER_COUNT);
}

static void test_cancel_reuse_and_first_match(void)
{
    KarqriTimerQueue queue;
    KarqriTimer timers[2];
    ExpiryLog log = {{0U}, 0U};
    const uint32_t remaining_event[] = {UINT32_C(0x01)};
    const uint32_t remaining_delta[] = {5U};

    karqri_timer_queue_init(&queue, timers, 2U);
    expect(karqri_timer_start(
               &queue, &log, UINT32_C(0x01), 3U, record_expiry) &&
               karqri_timer_start(
                   &queue, &log, UINT32_C(0x01), 5U, record_expiry),
           "duplicate timer setup failed");
    expect(karqri_timer_cancel(&queue, &log, UINT32_C(0x01)),
           "duplicate timer cancel failed");
    expect_active_deltas(
        &queue, remaining_event, remaining_delta, 1U);
    expect(karqri_timer_start(
               &queue, &log, UINT32_C(0x02), 2U, record_expiry),
           "cancelled timer entry was not reusable");
}

static void test_expired_pool_reuse_and_one_shot(void)
{
    KarqriTimerQueue queue;
    KarqriTimer timer;
    ExpiryLog log = {{0U}, 0U};
    karqri_timer_queue_init(&queue, &timer, 1U);
    expect(karqri_timer_start(
               &queue, &log, UINT32_C(0x01), 1U, record_expiry),
           "one-shot timer start failed");
    karqri_timer_tick(&queue);
    expect(log.count == 1U && log.events[0] == UINT32_C(0x01),
           "one-tick timer did not expire");
    expect_queue_counts(&queue, 0U, 1U);
    karqri_timer_tick(&queue);
    expect(log.count == 1U, "expired timer notified again");

    expect(karqri_timer_start(
               &queue, &log, UINT32_C(0x02), 1U, record_expiry),
           "expired pool entry was not reusable");
    karqri_timer_tick(&queue);
    expect(log.count == 2U && log.events[1] == UINT32_C(0x02),
           "reused timer did not expire with new arguments");
    expect_queue_counts(&queue, 0U, 1U);
    karqri_timer_tick(&queue);
    expect(log.count == 2U, "reused one-shot timer notified again");
    expect_queue_counts(&queue, 0U, 1U);
}

static void test_start_cancel_after_elapsed_tick(void)
{
    KarqriTimerQueue queue;
    KarqriTimer timers[3];
    ExpiryLog log = {{0U}, 0U};
    const uint32_t events[] = {UINT32_C(0x02), UINT32_C(0x01), UINT32_C(0x03)};
    const uint32_t deltas[] = {1U, 2U, 2U};
    const uint32_t remaining_events[] = {UINT32_C(0x02), UINT32_C(0x03)};
    const uint32_t remaining_deltas[] = {1U, 4U};
    size_t index;

    karqri_timer_queue_init(&queue, timers, 3U);
    expect(karqri_timer_start(&queue, &log, UINT32_C(0x01), 4U, record_expiry) &&
               karqri_timer_start(&queue, &log, UINT32_C(0x03), 6U, record_expiry),
           "elapsed-tick timer setup failed");
    karqri_timer_tick(&queue);
    expect(log.count == 0U, "setup timers expired too early");
    expect(karqri_timer_start(&queue, &log, UINT32_C(0x02), 1U, record_expiry),
           "start after elapsed tick failed");
    expect_active_deltas(&queue, events, deltas, 3U);
    expect(karqri_timer_cancel(&queue, &log, UINT32_C(0x01)),
           "cancel after elapsed tick failed");
    expect_active_deltas(&queue, remaining_events, remaining_deltas, 2U);
    expect(log.count == 0U, "cancel invoked expiry callback");
    karqri_timer_tick(&queue);
    expect(log.count == 1U && log.events[0] == UINT32_C(0x02),
           "new timer was not relative to its start time");
    for (index = 0U; index < 3U; index++) {
        karqri_timer_tick(&queue);
    }
    expect(log.count == 1U, "cancel advanced the remaining expiry");
    karqri_timer_tick(&queue);
    expect(log.count == 2U && log.events[1] == UINT32_C(0x03),
           "cancel changed the remaining expiry time");
    expect_queue_counts(&queue, 0U, 3U);
}

static void test_cancel_destination_and_events_match(void)
{
    KarqriTimerQueue queue;
    KarqriTimer timers[3];
    ExpiryLog first = {{0U}, 0U};
    ExpiryLog second = {{0U}, 0U};

    karqri_timer_queue_init(&queue, timers, 3U);
    expect(karqri_timer_start(&queue, &second, UINT32_C(0x01), 1U, record_expiry) &&
               karqri_timer_start(&queue, &first, UINT32_C(0x03), 1U, record_expiry) &&
               karqri_timer_start(&queue, &first, UINT32_C(0x01), 1U, record_expiry),
           "cancel key setup failed");
    expect(!karqri_timer_cancel(&queue, &second, UINT32_C(0x03)),
           "cancel matched destination and events from different timers");
    expect(karqri_timer_cancel(&queue, &first, UINT32_C(0x01)),
           "cancel did not find the exact key after partial matches");
    expect(first.count == 0U && second.count == 0U,
           "cancel invoked a callback");
    expect_queue_counts(&queue, 2U, 1U);
    karqri_timer_tick(&queue);
    expect(first.count == 1U && first.events[0] == UINT32_C(0x03) &&
               second.count == 1U && second.events[0] == UINT32_C(0x01),
           "cancel removed a destination-only or events-only match");
    expect_queue_counts(&queue, 0U, 3U);
}

static void test_maximum_delay_delta_and_cancel(void)
{
    KarqriTimerQueue queue;
    KarqriTimer timers[3];
    ExpiryLog log = {{0U}, 0U};
    const uint32_t events[] = {UINT32_C(0x02), UINT32_C(0x01), UINT32_C(0x03)};
    const uint32_t deltas[] = {1U, UINT32_MAX - 1U, 0U};
    const uint32_t after_head_events[] = {UINT32_C(0x01), UINT32_C(0x03)};
    const uint32_t after_head_deltas[] = {UINT32_MAX, 0U};
    const uint32_t last_event[] = {UINT32_C(0x03)};
    const uint32_t last_delta[] = {UINT32_MAX};
    const uint32_t after_tick_delta[] = {UINT32_MAX - 1U};

    karqri_timer_queue_init(&queue, timers, 3U);
    expect(karqri_timer_start(&queue, &log, UINT32_C(0x01), UINT32_MAX, record_expiry) &&
               karqri_timer_start(&queue, &log, UINT32_C(0x02), 1U, record_expiry) &&
               karqri_timer_start(&queue, &log, UINT32_C(0x03), UINT32_MAX, record_expiry),
           "maximum-delay insertion failed");
    expect_active_deltas(&queue, events, deltas, 3U);
    expect(karqri_timer_cancel(&queue, &log, UINT32_C(0x02)),
           "maximum-delay head repair setup failed");
    expect_active_deltas(&queue, after_head_events, after_head_deltas, 2U);
    expect(karqri_timer_cancel(&queue, &log, UINT32_C(0x01)),
           "maximum-delay equal-expiry repair failed");
    expect_active_deltas(&queue, last_event, last_delta, 1U);
    karqri_timer_tick(&queue);
    expect_active_deltas(&queue, last_event, after_tick_delta, 1U);
    expect(log.count == 0U, "maximum-delay timer expired early or on cancel");
    expect(karqri_timer_cancel(&queue, &log, UINT32_C(0x03)),
           "remaining maximum-delay timer cancel failed");
    expect_queue_counts(&queue, 0U, 3U);
}

static void test_empty_pool(void)
{
    KarqriTimerQueue queue;
    ExpiryLog log = {{0U}, 0U};

    karqri_timer_queue_init(&queue, NULL, 0U);
    expect_queue_counts(&queue, 0U, 0U);
    expect(!karqri_timer_start(&queue, &log, UINT32_C(0x01), 1U, record_expiry),
           "empty pool accepted a timer");
    expect(!karqri_timer_cancel(&queue, &log, UINT32_C(0x01)),
           "empty pool cancel succeeded");
    karqri_timer_tick(&queue);
    expect(log.count == 0U, "empty pool invoked callback");
    expect_queue_counts(&queue, 0U, 0U);
}

int main(void)
{
    test_initialization_and_empty_tick();
    test_delta_insertion_and_equal_order();
    test_pool_exhaustion();
    test_tick_progression_and_expiry_order();
    test_cancel_and_delta_repair();
    test_cancel_reuse_and_first_match();
    test_expired_pool_reuse_and_one_shot();
    test_start_cancel_after_elapsed_tick();
    test_cancel_destination_and_events_match();
    test_maximum_delay_delta_and_cancel();
    test_empty_pool();
    puts("timer validation: PASS");
    return EXIT_SUCCESS;
}
