#include "karqri/timer_event.h"

#include "karqri/event.h"

void karqri_timer_signal_event(void *destination, uint32_t events)
{
    karqri_event_signal((KarqriEvent *)destination, events);
}
