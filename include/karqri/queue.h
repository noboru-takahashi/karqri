#ifndef KARQRI_QUEUE_H
#define KARQRI_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Intrusive circular doubly linked queue; no allocation, message transfer,
 * task waiting or notification. The caller owns the sentinel and all nodes.
 * Each node may belong to only one queue at a time. Keep the sentinel and
 * linked objects alive at stable addresses until all their links are removed.
 *
 * Initialize the sentinel before use and nodes before first insertion. Do not
 * individually reinitialize linked nodes or a sentinel in use. A subsystem
 * owning an entire structure (e.g. Timer queue and pool) may rebuild it only
 * after all external access stops and all old registrations/links are abandoned;
 * this does not permit resetting individual nodes in a live queue.
 * Insert only detached nodes; insert_before requires a position in the queue (possibly
 * its sentinel). Do not insert a sentinel as an element or pass it to remove.
 * remove accepts a linked element or an initialized detached node; remove and
 * successful pop leave the node detached and reusable without reinitializing.
 * All node/queue pointers must be valid and non-NULL. Misuse is not checked.
 *
 * No internal synchronization is provided. Callers must serialize shared
 * access, including traversal and callback evaluation. Interrupt-context use
 * requires caller/port protection appropriate to all competing contexts.
 */
typedef struct KarqriQueueNode {
    struct KarqriQueueNode *next;
    struct KarqriQueueNode *previous;
} KarqriQueueNode;

typedef int32_t (*KarqriQueuePriority)(
    const KarqriQueueNode *node,
    void *context);

void karqri_queue_init(KarqriQueueNode *node);
bool karqri_queue_is_empty(const KarqriQueueNode *queue);

void karqri_queue_insert_before(
    KarqriQueueNode *position,
    KarqriQueueNode *node);
void karqri_queue_push_front(
    KarqriQueueNode *queue,
    KarqriQueueNode *node);
void karqri_queue_push_back(
    KarqriQueueNode *queue,
    KarqriQueueNode *node);

/* Returns NULL for an empty queue. push_back + pop_front provides FIFO. */
KarqriQueueNode *karqri_queue_pop_front(KarqriQueueNode *queue);
void karqri_queue_remove(KarqriQueueNode *node);

/*
 * Insert in descending signed priority order; equal priorities retain FIFO
 * insertion order. priority_of must be non-NULL and receives context unchanged
 * (context may be NULL if the callback permits it). It must not mutate the
 * queue and must return a stable priority during the operation.
 * The queue must already be sorted by this same priority rule. Keep priorities
 * stable while linked; remove before changing priority, then reinsert. Mixing
 * positional insertion with priority insertion must preserve sorted order.
 */
void karqri_queue_insert_priority(
    KarqriQueueNode *queue,
    KarqriQueueNode *node,
    KarqriQueuePriority priority_of,
    void *context);

#endif

