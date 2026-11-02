#ifndef KARQRI_TEST_EVENT_PROBE_H
#define KARQRI_TEST_EVENT_PROBE_H

#include "karqri/event.h"

/* Include after the test harness defines expect().
 * Records notifications, without task or scheduler behavior. */
typedef struct EventProbe {
    unsigned int started;
    unsigned int returned;
    unsigned int completed;
    uint32_t events;
} EventProbe;

static void probe_wait_started(void *owner)
{
    EventProbe *probe = owner;

    expect(probe->started == 0U && probe->returned == 0U &&
               probe->completed == 0U, "unexpected wait-start notification");
    probe->started++;
}

static void probe_return_value_set(void *owner, uint32_t events)
{
    EventProbe *probe = owner;

    expect(probe->started == 1U && probe->returned == 0U &&
               probe->completed == 0U, "unexpected result notification order");
    probe->events = events;
    probe->returned++;
}

static void probe_wait_completed(void *owner)
{
    EventProbe *probe = owner;

    expect(probe->started == 1U && probe->returned == 1U &&
               probe->completed == 0U, "unexpected completion notification order");
    probe->completed++;
}

static const KarqriEventWaitOperations probe_operations = {
    probe_wait_started,
    probe_return_value_set,
    probe_wait_completed,
};

#endif
