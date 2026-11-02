#include "karqri/queue.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct TestItem {
    KarqriQueueNode node;
    const char *name;
    int32_t priority;
} TestItem;

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

static TestItem *item_from_node(KarqriQueueNode *node)
{
    return (TestItem *)((unsigned char *)node - offsetof(TestItem, node));
}

static int32_t item_priority(
    const KarqriQueueNode *node,
    void *context)
{
    const TestItem *item;

    (void)context;
    item = (const TestItem *)((const unsigned char *)node -
                              offsetof(TestItem, node));
    return item->priority;
}

static void init_item(TestItem *item, const char *name, int32_t priority)
{
    karqri_queue_init(&item->node);
    item->name = name;
    item->priority = priority;
}

static void expect_links(
    const KarqriQueueNode *queue,
    size_t expected_count)
{
    const KarqriQueueNode *node;
    const KarqriQueueNode *previous;
    const KarqriQueueNode *next;
    size_t forward_count;
    size_t reverse_count;

    previous = queue;
    node = queue->next;
    forward_count = 0;
    while (node != queue) {
        expect(node->previous == previous, "broken previous link");
        previous = node;
        node = node->next;
        forward_count++;
        expect(forward_count <= expected_count, "forward link cycle");
    }
    expect(previous == queue->previous, "incorrect queue tail");
    expect(forward_count == expected_count, "incorrect forward count");

    next = queue;
    node = queue->previous;
    reverse_count = 0;
    while (node != queue) {
        expect(node->next == next, "broken next link");
        next = node;
        node = node->previous;
        reverse_count++;
        expect(reverse_count <= expected_count, "reverse link cycle");
    }
    expect(next == queue->next, "incorrect queue head");
    expect(reverse_count == expected_count, "incorrect reverse count");
}

static void expect_pop(
    KarqriQueueNode *queue,
    TestItem *expected,
    const char *message)
{
    KarqriQueueNode *node = karqri_queue_pop_front(queue);

    expect(node == &expected->node, message);
    expect(node->next == node && node->previous == node,
           "removed node is not detached");
}

static void test_empty_queue(void)
{
    KarqriQueueNode queue;

    karqri_queue_init(&queue);
    expect(karqri_queue_is_empty(&queue), "new queue is not empty");
    expect(karqri_queue_pop_front(&queue) == NULL,
           "empty pop did not return NULL");
    expect_links(&queue, 0);
}

static void test_fifo(void)
{
    KarqriQueueNode queue;
    TestItem a;
    TestItem b;
    TestItem c;

    karqri_queue_init(&queue);
    init_item(&a, "A", 0);
    init_item(&b, "B", 0);
    init_item(&c, "C", 0);
    karqri_queue_push_back(&queue, &a.node);
    karqri_queue_push_back(&queue, &b.node);
    karqri_queue_push_back(&queue, &c.node);
    expect_links(&queue, 3);
    expect_pop(&queue, &a, "FIFO first item mismatch");
    expect_pop(&queue, &b, "FIFO second item mismatch");
    expect_pop(&queue, &c, "FIFO third item mismatch");
    expect(karqri_queue_is_empty(&queue), "FIFO queue did not empty");
}

static void test_lifo(void)
{
    KarqriQueueNode queue;
    TestItem a;
    TestItem b;
    TestItem c;

    karqri_queue_init(&queue);
    init_item(&a, "A", 0);
    init_item(&b, "B", 0);
    init_item(&c, "C", 0);
    karqri_queue_push_front(&queue, &a.node);
    karqri_queue_push_front(&queue, &b.node);
    karqri_queue_push_front(&queue, &c.node);
    expect_links(&queue, 3);
    expect_pop(&queue, &c, "LIFO first item mismatch");
    expect_pop(&queue, &b, "LIFO second item mismatch");
    expect_pop(&queue, &a, "LIFO third item mismatch");
}

static void test_priority_and_equal_order(void)
{
    KarqriQueueNode queue;
    TestItem p20;
    TestItem p40_first;
    TestItem p30;
    TestItem p40_second;
    TestItem p10;
    TestItem *expected[] = {
        &p40_first, &p40_second, &p30, &p20, &p10,
    };
    KarqriQueueNode *node;
    size_t i;

    karqri_queue_init(&queue);
    init_item(&p20, "20", 20);
    init_item(&p40_first, "40-first", 40);
    init_item(&p30, "30", 30);
    init_item(&p40_second, "40-second", 40);
    init_item(&p10, "10", 10);

    karqri_queue_insert_priority(&queue, &p20.node, item_priority, NULL);
    karqri_queue_insert_priority(
        &queue, &p40_first.node, item_priority, NULL);
    karqri_queue_insert_priority(&queue, &p30.node, item_priority, NULL);
    karqri_queue_insert_priority(
        &queue, &p40_second.node, item_priority, NULL);
    karqri_queue_insert_priority(&queue, &p10.node, item_priority, NULL);

    expect_links(&queue, 5);
    node = queue.next;
    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        expect(item_from_node(node) == expected[i],
               "priority insertion order mismatch");
        node = node->next;
    }
    expect(node == &queue, "priority queue did not end at sentinel");
}

static void test_remove(void)
{
    KarqriQueueNode queue;
    TestItem a;
    TestItem b;
    TestItem c;

    karqri_queue_init(&queue);
    init_item(&a, "A", 0);
    init_item(&b, "B", 0);
    init_item(&c, "C", 0);
    karqri_queue_push_back(&queue, &a.node);
    karqri_queue_push_back(&queue, &b.node);
    karqri_queue_push_back(&queue, &c.node);

    karqri_queue_remove(&b.node);
    expect(b.node.next == &b.node && b.node.previous == &b.node,
           "removed middle node is not detached");
    expect(queue.next == &a.node && a.node.next == &c.node &&
               c.node.previous == &a.node && queue.previous == &c.node,
           "remove did not reconnect neighbors");
    expect_links(&queue, 2);

    karqri_queue_remove(&a.node);
    karqri_queue_remove(&c.node);
    expect(karqri_queue_is_empty(&queue), "remove did not empty queue");
    expect_links(&queue, 0);
}

static void test_reinsert_after_removal(void)
{
    KarqriQueueNode queue;
    TestItem a;
    TestItem b;

    karqri_queue_init(&queue);
    init_item(&a, "A", 0);
    init_item(&b, "B", 0);
    karqri_queue_push_back(&queue, &a.node);
    karqri_queue_push_back(&queue, &b.node);
    karqri_queue_remove(&a.node);
    expect_links(&queue, 1);
    karqri_queue_push_back(&queue, &a.node);
    expect_links(&queue, 2);
    expect_pop(&queue, &b, "reinsert did not move A behind B");
    /* A popped node is also reusable without initialization. */
    karqri_queue_push_back(&queue, &b.node);
    expect_links(&queue, 2);
    expect_pop(&queue, &a, "reinserted A missing");
    expect_pop(&queue, &b, "reinserted popped B missing");
    expect_links(&queue, 0);
}

static int32_t priority_with_context(
    const KarqriQueueNode *node,
    void *context)
{
    const TestItem *promoted = context;

    expect(promoted != NULL, "priority callback lost its context");
    return node == &promoted->node ? INT32_C(5) : item_priority(node, NULL);
}

static void test_negative_priority_and_context(void)
{
    KarqriQueueNode queue;
    TestItem items[5];
    const int32_t priorities[] = {-20, -5, -30, 0, -5};
    const size_t expected[] = {2, 3, 1, 4, 0};
    size_t i;

    karqri_queue_init(&queue);
    for (i = 0; i < 5; i++) {
        init_item(&items[i], "item", priorities[i]);
    }
    for (i = 0; i < 5; i++) {
        /* Context promotes item 2 from -30 to 5 for every comparison. */
        karqri_queue_insert_priority(
            &queue, &items[i].node, priority_with_context, &items[2]);
        expect_links(&queue, i + 1);
    }
    for (i = 0; i < 5; i++) {
        expect_pop(&queue, &items[expected[i]],
                   "negative priority, equal FIFO or context order mismatch");
        expect_links(&queue, 4 - i);
    }
}

int main(void)
{
    test_empty_queue();
    test_fifo();
    test_lifo();
    test_priority_and_equal_order();
    test_remove();
    test_reinsert_after_removal();
    test_negative_priority_and_context();
    puts("PASS: queue behavior validation");
    return EXIT_SUCCESS;
}

