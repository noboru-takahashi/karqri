#include "karqri/queue.h"

#include <stddef.h>

void karqri_queue_init(KarqriQueueNode *node)
{
    node->next = node;
    node->previous = node;
}

bool karqri_queue_is_empty(const KarqriQueueNode *queue)
{
    return queue->next == queue;
}

void karqri_queue_insert_before(
    KarqriQueueNode *position,
    KarqriQueueNode *node)
{
    node->next = position;
    node->previous = position->previous;
    position->previous->next = node;
    position->previous = node;
}

void karqri_queue_push_front(
    KarqriQueueNode *queue,
    KarqriQueueNode *node)
{
    karqri_queue_insert_before(queue->next, node);
}

void karqri_queue_push_back(
    KarqriQueueNode *queue,
    KarqriQueueNode *node)
{
    karqri_queue_insert_before(queue, node);
}

KarqriQueueNode *karqri_queue_pop_front(KarqriQueueNode *queue)
{
    KarqriQueueNode *node;

    if (karqri_queue_is_empty(queue)) {
        return NULL;
    }

    node = queue->next;
    karqri_queue_remove(node);
    return node;
}

void karqri_queue_remove(KarqriQueueNode *node)
{
    node->previous->next = node->next;
    node->next->previous = node->previous;
    karqri_queue_init(node);
}

void karqri_queue_insert_priority(
    KarqriQueueNode *queue,
    KarqriQueueNode *node,
    KarqriQueuePriority priority_of,
    void *context)
{
    KarqriQueueNode *position;
    const int32_t priority = priority_of(node, context);

    position = queue->next;
    while (position != queue &&
           priority_of(position, context) >= priority) {
        position = position->next;
    }

    karqri_queue_insert_before(position, node);
}

