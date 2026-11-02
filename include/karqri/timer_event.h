#ifndef KARQRI_TIMER_EVENT_H
#define KARQRI_TIMER_EVENT_H

#include <stdint.h>

/*
 * Internal/integration adapter suitable for a Timer expiry callback.
 * destination must be an initialized, non-NULL KarqriEvent *. The adapter does
 * not own it: keep it alive at a stable address while registered as a Timer
 * destination and through callback completion. Stop all notification sources
 * before reinitializing the Event, as required by its contract.
 *
 * Synchronously forwards events unchanged as an Event signal mask; zero is
 * allowed under the Event contract. Same-tick expiries signal individually in
 * registration order, without merging: each signal has its own snapshot and
 * consumption, whose pending result is used by the next signal.
 *
 * Timer cancel removes only one matching future notification; it does not clear
 * Event pending, detach waiters or resume tasks. Pool-exhausted Timer start
 * changes no Event pending, waiter or task state and sends no failure notice
 * or rollback. Callers must handle registration failure themselves.
 *
 * Timer/Event/Scheduler ownership, lifetime, callback and synchronization
 * contracts apply. Indirect start/cancel/tick reentry to the originating Timer
 * queue through Event callbacks is also outside the contract. This adapter has
 * no state, queue, task management or dispatch of its own. Scheduler selection
 * may change via waiter callbacks; actual CPU context switching and interrupt
 * protection belong to the port. R6-01 implemented Pico wait-registration
 * serialization, protected switch requests/PendSV handoff, and RUNNING-gated
 * tick lifecycle. Special-path hardware verification remains tracked in R7;
 * normal-path PASS does not cover every interleaving. This adapter adds no
 * synchronization for direct shared primitive access.
 */
void karqri_timer_signal_event(void *destination, uint32_t events);

#endif
