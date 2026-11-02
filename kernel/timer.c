#include "karqri/timer.h"

#include <stddef.h>

#define KARQRI_CONTAINER_OF(pointer, type, member) \
    ((type *)((unsigned char *)(pointer) - offsetof(type, member)))

static KarqriTimer *timer_from_node(KarqriQueueNode *node)
{
    return KARQRI_CONTAINER_OF(node, KarqriTimer, timer_node);
}

void karqri_timer_queue_init(
    KarqriTimerQueue *queue,
    KarqriTimer *timers,
    size_t timer_count)
{
    size_t index;

    karqri_queue_init(&queue->free_timers);
    karqri_queue_init(&queue->active_timers);

    for (index = 0; index < timer_count; index++) {
        karqri_queue_init(&timers[index].timer_node);
        karqri_queue_push_back(
            &queue->free_timers, &timers[index].timer_node);
    }
}

bool karqri_timer_start(
    KarqriTimerQueue *queue,
    void *destination,
    uint32_t events,
    uint32_t delay_ticks,
    KarqriTimerExpiry expiry)
{
    KarqriQueueNode *free_node;
    KarqriQueueNode *position;
    KarqriTimer *timer;
    uint32_t remaining_ticks = delay_ticks;

    free_node = karqri_queue_pop_front(&queue->free_timers);
    if (free_node == NULL) {
        return false;
    }

    position = queue->active_timers.next;
    while (position != &queue->active_timers) {
        KarqriTimer *position_timer = timer_from_node(position);

        if (position_timer->remaining_ticks > remaining_ticks) {
            break;
        }
        remaining_ticks -= position_timer->remaining_ticks;
        position = position->next;
    }

    timer = timer_from_node(free_node);
    timer->destination = destination;
    timer->expiry = expiry;
    timer->events = events;
    timer->remaining_ticks = remaining_ticks;

    if (position != &queue->active_timers) {
        timer_from_node(position)->remaining_ticks -= remaining_ticks;
    }
    karqri_queue_insert_before(position, &timer->timer_node);
    return true;
}

bool karqri_timer_cancel(
    KarqriTimerQueue *queue,
    void *destination,
    uint32_t events)
{
    KarqriQueueNode *position = queue->active_timers.next;

    while (position != &queue->active_timers) {
        KarqriTimer *timer = timer_from_node(position);

        if (timer->destination == destination && timer->events == events) {
            if (position->next != &queue->active_timers) {
                timer_from_node(position->next)->remaining_ticks +=
                    timer->remaining_ticks;
            }
            karqri_queue_remove(&timer->timer_node);
            karqri_queue_push_back(
                &queue->free_timers, &timer->timer_node);
            return true;
        }
        position = position->next;
    }

    return false;
}

void karqri_timer_tick(KarqriTimerQueue *queue)
{
    KarqriTimer *timer;

    if (karqri_queue_is_empty(&queue->active_timers)) {
        return;
    }

    timer = timer_from_node(queue->active_timers.next);
    timer->remaining_ticks--;

    while (timer->remaining_ticks == 0U) {
        karqri_queue_remove(&timer->timer_node);
        timer->expiry(timer->destination, timer->events);
        karqri_queue_push_back(
            &queue->free_timers, &timer->timer_node);

        if (karqri_queue_is_empty(&queue->active_timers)) {
            break;
        }
        timer = timer_from_node(queue->active_timers.next);
    }
}
