# Karqri API Reference

[English (authoritative)](api-reference.md) | [日本語 (translation)](api-reference-ja.md)

This reference describes the APIs in the current public headers, implementations, and test definitions. The English version is authoritative; the Japanese version is a translation. Source and test links in each section identify where to inspect the contracts. Declarations and test definitions are distinguished from execution evidence; section 19 summarizes the scope and limitations of existing results. Preparing this reference does not add new verification results.

## 1. Scope and conventions

All seven headers under `include/karqri/` are covered. The API boundary classifies Queue/Task/Event/Timer as application primitives and Scheduler/scheduler_event/timer_event as internal/integration interfaces. A header's location does not by itself make it an application API. POSIX/Pico port headers are covered as a separate layer. Internal assembly helpers, diagnostics, and tracing are identified in the appendix rather than promoted to application APIs. The removed Memory/task_event APIs, message send/receive services, sleep, timeouts, forced Task deletion, and periodic Timers are not current services.

The C declarations in each section are the current header prototypes. Tables describe arguments, return values, effects, and function-specific restrictions. Apply the common conditions together with the relevant sections. A declaration, a reference in a test, recorded execution, and verification of every input/interrupt interleaving are different kinds of evidence.

Coverage: 48 Kernel/integration functions and 32 POSIX/Pico port functions, totaling 80. The appendix covers eight additional internal inline, diagnostic, and conditional trace functions, for a total of 88. Function pointer types, data types, major constants, and exposed extern symbols are also described. Demo-private headers and Python tools are outside the Kernel API scope.

## 2. Common execution, lifetime, and error contracts

- Kernel primitives provide no internal locks, interrupt masking, allocation, or CPU dispatch. Task/ISR usability depends on the caller serializing all competing operations, including reads, queue traversal, and callbacks. There is no blanket guarantee of arbitrary ISR safety. POSIX additionally imposes the owner-thread restrictions below.
- Objects are caller-owned and must remain valid at stable addresses while referenced, linked, or used by callbacks. Initialize before first use. Moving or individually reinitializing registered objects, or linking one node into multiple queues, is forbidden. NULL is permitted only where explicitly allowed. Misuse is generally unchecked; an error return is not guaranteed.
- Callbacks run synchronously in the caller's context. Direct or indirect reentry that invalidates traversal of the same Event/Timer is forbidden. Logical Scheduler selection may change, but CPU dispatch must not occur before the enclosing Event signal/Timer tick completes.
- Blocking or self-suspending an executing Task requires another runnable context. The application supplies a driver/idle Task if necessary. All Tasks may return normally; a permanent idle Task is not mandatory. Mid-wait cancellation and removal/destruction of a waiting Task are outside the contract.
- Suppression is a bool, not a nesting count or an IRQ lock. Save and restore its previous value. Blocking, self-suspension, and Task return under suppression are forbidden. Clearing the raw setter alone does not reschedule.
- POSIX supports one owner OS thread and destruction after a fixed group of contexts completes normally. Foreign-thread/signal-handler access, concurrent instances, and host resumption of an unfinished group are unsupported. Violations are not guaranteed a diagnostic.
- Pico assumes the same core, one runtime, and privileged execution. Task code directly using raw Timer/Event/Queue/Task/Scheduler operations must protect them against the tick IRQ. Restore the saved PRIMASK value; do not wait for SVC/PendSV while masked. NMI, HardFault, and another core are outside this protection.

## 3. Types, constants, and state model

| Layer | Type/field | Meaning |
| --- | --- | --- |
| Queue | `KarqriQueueNode { next, previous }` | Intrusive circular doubly linked list. Sentinel and elements have the same type. `KarqriQueuePriority` is `int32_t (*)(const KarqriQueueNode *, void *)`. |
| Task | `KarqriTaskEntry = void (*)(void *)`, `KarqriTaskState = uint32_t` | Entry/argument, uint32 priority, node/state, uintptr delayed value plus bool validity. No stack/native context. |
| Event | ANY/ALL, COMPLETED/BLOCKED, `KarqriEventWaitResult { disposition, events }` | 32 pending bits, not counts. ANY requires a nonzero intersection; ALL requires every requested bit. |
| Event callback | `KarqriEventWaitOperations` | `void wait_started(void*)`, `void return_value_set(void*,uint32_t)`, `void wait_completed(void*)`; each receives owner. |
| Event storage | `KarqriEvent { pending_events, waiters }`, `KarqriEventWaiter` | Waiter holds node/owner/operations/requested_events/mode. Caller-provided storage. |
| Scheduler | `KarqriScheduler` | Ready/Suspended sentinels, logical current, suppression. Ready includes the selected Task; descending uint32 priority and FIFO among equals. |
| Adapter | `KarqriScheduledEventWaiter` | Raw EventWaiter plus Scheduler/Task references. Connects completion through the delayed value to Ready. |
| Timer | `KarqriTimerExpiry = void (*)(void *, uint32_t)`, `KarqriTimer` | node/destination/expiry/events/remaining_ticks. events is opaque to Timer; remaining_ticks is a delta. |
| Timer pool | `KarqriTimerQueue { free_timers, active_timers }` | Manages the caller's pool in two queues. Free entries remain linked. |

Task bits: EVENT_WAIT=1, RECEIVE_WAIT=2, SEND_WAIT=4, SUSPENDED=8, READY=16, RUNNING=32, JAM_WAIT=64. WAIT_STATES contains only the first three. RECEIVE/SEND/JAM constants do not declare implemented services. READY|SUSPENDED and RUNNING left on a previously selected Task are intentional historical semantics. State alone does not establish CPU execution or queue membership. Generic Queue priority is **signed int32**; Task/Scheduler priority is **unsigned uint32**.


## 4. Queue

Declarations: [include/karqri/queue.h](../include/karqri/queue.h). Implementation: [kernel/queue.c](../kernel/queue.c). Test definitions: [tests/test_queue.c](../tests/test_queue.c).

Nonblocking primitives for Task code or serialized ISR use. Protect the whole operation, including priority callback evaluation. Misuse is unchecked.

```c
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

KarqriQueueNode *karqri_queue_pop_front(KarqriQueueNode *queue);

void karqri_queue_remove(KarqriQueueNode *node);

void karqri_queue_insert_priority(
    KarqriQueueNode *queue,
    KarqriQueueNode *node,
    KarqriQueuePriority priority_of,
    void *context);
```

| API | Behavior, arguments, return value, and specific restrictions |
| --- | --- |
| `karqri_queue_init` | Initialize node as a detached, self-linked node; also used for sentinels. Do not initialize an individual linked node. No return value. |
| `karqri_queue_is_empty` | Return true if the queue sentinel's next pointer points to itself. Reads also require serialization of shared access. |
| `karqri_queue_insert_before` | Insert a detached node immediately before position, which must belong to the queue and may be its sentinel. No return value. |
| `karqri_queue_push_front` | Insert a detached node at the head. Together with pop_front, this provides LIFO order. |
| `karqri_queue_push_back` | Insert a detached node at the tail. Together with pop_front, this provides FIFO order. |
| `karqri_queue_pop_front` | Remove and return the head node, or NULL if empty. The returned node is detached. |
| `karqri_queue_remove` | Remove an element and leave it detached. An initialized, already detached node is also accepted. Removing a sentinel is forbidden. |
| `karqri_queue_insert_priority` | Insert in descending int32_t order as returned by priority_of(node, context), with FIFO order among equals. Pass context unchanged; NULL is permitted if supported by the callback. The queue must already be sorted, and priorities must remain stable. The callback must not modify the queue. |

**Declarations versus verification:** All functions above have implementations. The following lists indicate direct references in the named test/validation source, not successful execution or complete branch coverage.

Direct references: `karqri_queue_init`, `karqri_queue_is_empty`, `karqri_queue_push_front`, `karqri_queue_push_back`, `karqri_queue_pop_front`, `karqri_queue_remove`, `karqri_queue_insert_priority`.

No direct references in this test: `karqri_queue_insert_before`. Distinguish internal calls and use by other tests/demos; this is not a claim of separate testing.

## 5. Task

Declarations: [include/karqri/task.h](../include/karqri/task.h). Implementation: [kernel/task.c](../kernel/task.c). Test definitions: [tests/test_task.c](../tests/test_task.c).

Nonblocking reads/writes of logical attributes. Do not use raw setters to invalidate a registered Task's Scheduler bookkeeping. State-bit tests are shared reads too.

```c
void karqri_task_init(
    KarqriTask *task,
    KarqriTaskEntry entry,
    void *entry_argument,
    uint32_t priority);

KarqriTaskState karqri_task_state(const KarqriTask *task);

void karqri_task_state_replace(KarqriTask *task, KarqriTaskState state);

void karqri_task_state_set(KarqriTask *task, KarqriTaskState state_bits);

void karqri_task_state_clear(KarqriTask *task, KarqriTaskState state_bits);

bool karqri_task_state_has_any(
    const KarqriTask *task,
    KarqriTaskState state_bits);

bool karqri_task_state_has_all(
    const KarqriTask *task,
    KarqriTaskState state_bits);

bool karqri_task_is_waiting(const KarqriTask *task);

bool karqri_task_suspend(KarqriTask *task);

bool karqri_task_resume(KarqriTask *task);

uint32_t karqri_task_priority(const KarqriTask *task);

uint32_t karqri_task_set_priority(KarqriTask *task, uint32_t priority);

void karqri_task_set_delayed_return(
    KarqriTask *task,
    uintptr_t value);

bool karqri_task_has_delayed_return(const KarqriTask *task);

uintptr_t karqri_task_delayed_return(const KarqriTask *task);

void karqri_task_clear_delayed_return(KarqriTask *task);
```

| API | Behavior, arguments, return value, and specific restrictions |
| --- | --- |
| `karqri_task_init` | Store entry, entry_argument, and uint32_t priority; initialize a detached node, state=READY, and an invalid delayed result with stored value zero. Does not register, allocate a stack, or start execution. Keep the entry and argument valid for execution. |
| `karqri_task_state` | Return the state bitmask. It does not establish which Task is executing. |
| `karqri_task_state_replace` | Replace the entire state value. Does not change queue membership. |
| `karqri_task_state_set` | OR state_bits into state. Does not change queue membership. |
| `karqri_task_state_clear` | Clear state_bits using AND NOT. Does not change queue membership. |
| `karqri_task_state_has_any` | Return true if any specified bit is set. A zero mask returns false. |
| `karqri_task_state_has_all` | Return true if all specified bits are set. A zero mask returns true, as defined by the implementation's expression. |
| `karqri_task_is_waiting` | Return true if any WAIT_STATES bit (EVENT/RECEIVE/SEND) is set. SUSPENDED and JAM are not included. |
| `karqri_task_suspend` | Return false if already SUSPENDED. Otherwise set that bit, clear READY if not waiting, and return true. Changes state only. |
| `karqri_task_resume` | Return false if not SUSPENDED. Otherwise clear that bit, set READY if not waiting, and return true. Changes state only. |
| `karqri_task_priority` | Return the uint32_t priority. |
| `karqri_task_set_priority` | Store priority and return the previous value. Does not reorder the Ready queue. The caller must preserve Scheduler/port consistency for a registered Task. |
| `karqri_task_set_delayed_return` | Store value, including zero, and mark it valid, replacing any previous result. |
| `karqri_task_has_delayed_return` | Return the delayed-result valid flag. |
| `karqri_task_delayed_return` | Return the stored uintptr_t value without consuming it. Check validity before treating it as a completed result. |
| `karqri_task_clear_delayed_return` | Clear only the valid flag; retain the stored value. |

**Declarations versus verification:** All functions above have implementations. The following lists indicate direct references in the named test/validation source, not successful execution or complete branch coverage.

Direct references: `karqri_task_init`, `karqri_task_state`, `karqri_task_state_replace`, `karqri_task_state_set`, `karqri_task_state_clear`, `karqri_task_state_has_any`, `karqri_task_state_has_all`, `karqri_task_is_waiting`, `karqri_task_suspend`, `karqri_task_resume`, `karqri_task_priority`, `karqri_task_set_priority`, `karqri_task_set_delayed_return`, `karqri_task_has_delayed_return`, `karqri_task_delayed_return`, `karqri_task_clear_delayed_return`.

## 6. Event

Declarations: [include/karqri/event.h](../include/karqri/event.h). Implementation: [kernel/event.c](../kernel/event.c). Test definitions: [tests/test_event.c](../tests/test_event.c).

Raw operations are nonblocking. BLOCKED reports waiter registration; it does not suspend the caller's stack. No timeout argument. Protect the entire wait/signal, including callbacks.

```c
void karqri_event_init(KarqriEvent *event);

KarqriEventWaitResult karqri_event_wait(
    KarqriEvent *event,
    KarqriEventWaiter *waiter,
    uint32_t requested_events,
    KarqriEventWaitMode mode,
    void *owner,
    const KarqriEventWaitOperations *operations);

void karqri_event_signal(KarqriEvent *event, uint32_t signaled_events);

uint32_t karqri_event_pending(const KarqriEvent *event);
```

| API | Behavior, arguments, return value, and specific restrictions |
| --- | --- |
| `karqri_event_init` | Set pending to zero and initialize an empty waiter queue. No notification callbacks. Do not reinitialize while waiters or notification sources remain. |
| `karqri_event_wait` | requested_events must be nonzero and mode must be ANY or ALL. Immediate completion returns the full pending snapshot before consumption, consumes only requested bits, and touches neither the waiter nor callbacks. Otherwise initialize the waiter, call wait_started, enqueue FIFO, and return BLOCKED/events=0. This reports registration, not CPU blocking. owner may be NULL if callbacks support it; operations and all three callbacks must be valid and non-NULL even for immediate completion. No timeout or cancellation. |
| `karqri_event_signal` | OR signaled_events into pending; zero is allowed. Scan FIFO, skipping unsatisfied waiters. Evaluate every waiter against the same snapshot, so one signal may complete several. For each completion: return_value_set while linked, consume requested bits, detach, then wait_completed. No return value. The snapshot is not limited to its intersection with the requested mask. |
| `karqri_event_pending` | Read pending without consuming it. Shared reads also require protection. |

**Declarations versus verification:** All functions above have implementations. The following lists indicate direct references in the named test/validation source, not successful execution or complete branch coverage.

Direct references: `karqri_event_init`, `karqri_event_wait`, `karqri_event_signal`, `karqri_event_pending`.

## 7. Timer

Declarations: [include/karqri/timer.h](../include/karqri/timer.h). Implementation: [kernel/timer.c](../kernel/timer.c). Test definitions: [tests/test_timer.c](../tests/test_timer.c).

Raw operations are nonblocking, but tick invokes expiry synchronously. Delay is measured in tick calls, with no wall-clock guarantee. Callback reentry into the same queue is forbidden.

```c
void karqri_timer_queue_init(
    KarqriTimerQueue *queue,
    KarqriTimer *timers,
    size_t timer_count);

bool karqri_timer_start(
    KarqriTimerQueue *queue,
    void *destination,
    uint32_t events,
    uint32_t delay_ticks,
    KarqriTimerExpiry expiry);

bool karqri_timer_cancel(
    KarqriTimerQueue *queue,
    void *destination,
    uint32_t events);

void karqri_timer_tick(KarqriTimerQueue *queue);
```

| API | Behavior, arguments, return value, and specific restrictions |
| --- | --- |
| `karqri_timer_queue_init` | Initialize an empty active queue and register timers[0..timer_count) in free. timers may be NULL only when count is zero. Reinitialize the whole structure only after ticks, callbacks, and all external access have stopped. |
| `karqri_timer_start` | Register destination/events/expiry for one expiry after delay_ticks tick calls. Delay must be 1..UINT32_MAX; zero is outside the contract. expiry must be non-NULL; destination may be NULL if supported by the callback. Return true on registration, false on pool exhaustion with active timers unchanged. Equal deadlines retain registration order. No callbacks or dispatch here. |
| `karqri_timer_cancel` | Cancel only the first active-order match of destination pointer and the full events value. Repair following deltas and return the entry to free without a callback. Return true if removed, false if absent. expiry is not part of the key. Does not cancel Event pending bits or waits. |
| `karqri_timer_tick` | Advance one logical tick. Expire equal deadlines FIFO: remove from active, invoke expiry synchronously, then return to free. The entry is unavailable for reuse during its callback. An empty queue is a no-op. No periodic restart, elapsed-time compensation, or CPU switch. |

**Declarations versus verification:** All functions above have implementations. The following lists indicate direct references in the named test/validation source, not successful execution or complete branch coverage.

Direct references: `karqri_timer_queue_init`, `karqri_timer_start`, `karqri_timer_cancel`, `karqri_timer_tick`.

## 8. Scheduler

Declarations: [include/karqri/scheduler.h](../include/karqri/scheduler.h). Implementation: [kernel/scheduler.c](../kernel/scheduler.c). Test definitions: [tests/test_scheduler.c](../tests/test_scheduler.c).

Internal/integration interface. All operations change logical state only; no CPU switching. Target Tasks belong to this Scheduler. The caller serializes operations with callbacks and port handoff.

```c
void karqri_scheduler_init(KarqriScheduler *scheduler);

KarqriTask *karqri_scheduler_current(const KarqriScheduler *scheduler);

KarqriTask *karqri_scheduler_candidate(const KarqriScheduler *scheduler);

bool karqri_scheduler_reschedule(KarqriScheduler *scheduler);

bool karqri_scheduler_set_reschedule_suppressed(
    KarqriScheduler *scheduler,
    bool suppressed);

void karqri_scheduler_add_ready(
    KarqriScheduler *scheduler,
    KarqriTask *task);

void karqri_scheduler_begin_wait(
    KarqriScheduler *scheduler,
    KarqriTask *task,
    KarqriTaskState wait_state);

void karqri_scheduler_ready(
    KarqriScheduler *scheduler,
    KarqriTask *task,
    KarqriTaskState completed_wait_state);

bool karqri_scheduler_suspend(
    KarqriScheduler *scheduler,
    KarqriTask *task);

bool karqri_scheduler_resume(
    KarqriScheduler *scheduler,
    KarqriTask *task);

uint32_t karqri_scheduler_set_priority(
    KarqriScheduler *scheduler,
    KarqriTask *task,
    uint32_t priority);

bool karqri_scheduler_rotate_timeslice(KarqriScheduler *scheduler);

bool karqri_scheduler_rotate_task_timeslice(
    KarqriScheduler *scheduler,
    KarqriTask *task);

void karqri_scheduler_remove_task(
    KarqriScheduler *scheduler,
    KarqriTask *task);
```

| API | Behavior, arguments, return value, and specific restrictions |
| --- | --- |
| `karqri_scheduler_init` | Initialize an unused Scheduler with empty Ready/Suspended queues, current=NULL, and suppression=false. |
| `karqri_scheduler_current` | Return the last logically selected Task, or NULL. This is not the executing CPU context. |
| `karqri_scheduler_candidate` | Return the Ready head, or NULL if empty. May differ from current under suppression. |
| `karqri_scheduler_reschedule` | Return false under suppression. Otherwise select candidate as current and replace the new current's entire state with RUNNING. Do not repair the old Task's state. Return true for a selection change, including a change to NULL. No CPU switch. |
| `karqri_scheduler_set_reschedule_suppressed` | Store the suppressed bool and return the previous bool. This is not a nesting count. Clearing it alone does not reschedule. Queue updates continue. It is neither an interrupt mask nor a mutex. |
| `karqri_scheduler_add_ready` | Initially register an initialized, detached Task. For an existing Ready Task, remove and reinsert at the tail of its priority group, set READY, and reschedule. Re-registration is not idempotent. Tasks owned by another Scheduler are not allowed. |
| `karqri_scheduler_begin_wait` | Remove a Ready Task, replace its entire state with wait_state, and reschedule. Waiter management is separate. Protect registration, state changes, and handoff as a whole. |
| `karqri_scheduler_ready` | Clear completed_wait_state. If no wait bits remain, reinsert at the tail of the priority group and set READY. SUSPENDED is not a wait bit and may coexist with READY. Reschedule. |
| `karqri_scheduler_suspend` | NULL task means current. Return false if there is no current or it is already SUSPENDED. Preserve registration while waiting; otherwise move from Ready to Suspended. Return true on success and reschedule. Self-suspension requires another runnable context. |
| `karqri_scheduler_resume` | Return false for NULL task or a Task that is not SUSPENDED. Clear SUSPENDED; continue any outstanding wait, otherwise requeue through ready and return true. |
| `karqri_scheduler_set_priority` | Ready Tasks only. NULL task requires an existing current. Return the previous priority. The same value preserves queue position and current; a changed value reinserts at the tail of its new group and reschedules. Changes while waiting or suspended are outside the contract. |
| `karqri_scheduler_rotate_timeslice` | Rotate current to the tail of its priority group. Return false for NULL or detached current. The result reports a selection change, not whether the queue moved. |
| `karqri_scheduler_rotate_task_timeslice` | Rotate the explicit Task, such as the Task interrupted by a tick, to the tail of its priority group and reschedule. NULL or detached targets return false. The Task must belong to Ready. The result reports a logical selection change. |
| `karqri_scheduler_remove_task` | Remove the Task and set state=0. If current, clear current first, then reschedule. An Event wait must have completed normally beforehand. Does not cancel a waiter, clear the delayed result, destroy a context, or free storage. |

**Declarations versus verification:** All functions above have implementations. The following lists indicate direct references in the named test/validation source, not successful execution or complete branch coverage.

Direct references: `karqri_scheduler_init`, `karqri_scheduler_current`, `karqri_scheduler_candidate`, `karqri_scheduler_reschedule`, `karqri_scheduler_set_reschedule_suppressed`, `karqri_scheduler_add_ready`, `karqri_scheduler_suspend`, `karqri_scheduler_resume`, `karqri_scheduler_set_priority`, `karqri_scheduler_rotate_timeslice`, `karqri_scheduler_rotate_task_timeslice`, `karqri_scheduler_remove_task`.

No direct references in this test: `karqri_scheduler_begin_wait`, `karqri_scheduler_ready`. Distinguish internal calls and use by other tests/demos; this is not a claim of separate testing.

## 9. Scheduler/Event adapter

Declarations: [include/karqri/scheduler_event.h](../include/karqri/scheduler_event.h). Implementation: [kernel/scheduler_event.c](../kernel/scheduler_event.c). Test definitions: [tests/test_scheduler.c](../tests/test_scheduler.c).

Integration interface returning a raw EventWaitResult. The port implements CPU blocking. scheduler/task/waiter/event must all be non-NULL with valid lifetimes.

```c
KarqriEventWaitResult karqri_scheduler_wait_event(
    KarqriScheduler *scheduler,
    KarqriTask *task,
    KarqriScheduledEventWaiter *waiter,
    KarqriEvent *event,
    uint32_t requested_events,
    KarqriEventWaitMode mode);
```

| API | Behavior, arguments, return value, and specific restrictions |
| --- | --- |
| `karqri_scheduler_wait_event` | Connect a Ready Task and caller-owned waiter to an Event. Immediate completion preserves state and any existing valid delayed result. Blocking clears validity, replaces state with EVENT_WAIT, and removes the Task from Ready. Signal completion stores the snapshot as a valid delayed result, then clears EVENT_WAIT and readies the Task. No CPU switch. At most one active registration per Task; no mid-wait cancellation. |

**Declarations versus verification:** All functions above have implementations. The following lists indicate direct references in the named test/validation source, not successful execution or complete branch coverage.

Direct references: `karqri_scheduler_wait_event`.

## 10. Timer/Event adapter

Declarations: [include/karqri/timer_event.h](../include/karqri/timer_event.h). Implementation: [kernel/timer_event.c](../kernel/timer_event.c). Test definitions: [tests/test_timer_event.c](../tests/test_timer_event.c).

Integration interface subject to the complete Timer and Event contracts. timer_event alone manages no Tasks or contexts.

```c
void karqri_timer_signal_event(void *destination, uint32_t events);
```

| API | Behavior, arguments, return value, and specific restrictions |
| --- | --- |
| `karqri_timer_signal_event` | destination must be a valid, non-NULL KarqriEvent pointer. Signal events unchanged. Same-tick expiries remain separate signals; they are not merged. No CPU switch. Indirect reentry into the originating Timer is also forbidden. |

**Declarations versus verification:** All functions above have implementations. The following lists indicate direct references in the named test/validation source, not successful execution or complete branch coverage.

Direct references: none (indirect internal calls are separate).

No direct references in this test: `karqri_timer_signal_event`. Distinguish internal calls and use by other tests/demos; this is not a claim of separate testing.

## 11. POSIX context/dispatch

Declarations: [ports/posix/posix_port.h](../ports/posix/posix_port.h). Implementation: [ports/posix/posix_port.c](../ports/posix/posix_port.c). Test definitions: [tests/test_posix_kernel.c](../tests/test_posix_kernel.c).

Owner thread only. init/create/run set up and run a fixed group from the host; destroy is host teardown after all finish. yield is for the executing Task. Dispatch only after enclosing callback processing completes. The 32 trampoline slots are process-local and do not guarantee concurrent ports.

```c
void karqri_posix_port_init(
    KarqriPosixPort *port,
    KarqriScheduler *scheduler);

bool karqri_posix_task_context_create(
    KarqriPosixPort *port,
    KarqriPosixTaskContext *context,
    KarqriTask *task,
    size_t stack_size);

void karqri_posix_task_context_destroy(
    KarqriPosixTaskContext *context);

KarqriTask *karqri_posix_executing_task(const KarqriPosixPort *port);

bool karqri_posix_dispatch(KarqriPosixPort *port);

bool karqri_posix_yield(KarqriPosixPort *port);

bool karqri_posix_run(KarqriPosixPort *port);
```

| API | Behavior, arguments, return value, and specific restrictions |
| --- | --- |
| `karqri_posix_port_init` | On the owner thread, associate an unused port with an initialized Scheduler and clear the context table and execution state. Do not reinitialize a live port. |
| `karqri_posix_task_context_create` | Allocate a Task stack with malloc, use getcontext/makecontext, register a slot, and add the Task to Ready. stack_size is in bytes. Return false on the 32-context/slot limit, zero size, or malloc/getcontext failure; true on success. Duplicate Task/context registration and creation during execution are outside the contract. Do not destroy an uninitialized context after failure. |
| `karqri_posix_task_context_destroy` | After the entire group completes normally, destroy only successfully created contexts from the owner host thread. Free the stack, release the trampoline slot, and set active=false. Does not remove the Task, remove pointers from port.contexts, decrement context_count, or cancel waits. Keep context/Task storage until old port references are gone. |
| `karqri_posix_executing_task` | Return the Task of executing_context, or NULL. Owner thread only, including this read. |
| `karqri_posix_dispatch` | Dispatch Scheduler current to its native context. Return false if no target, no context, or the same context. Return true after the successful swapcontext path resumes; abort on failure. Does not reschedule. From a Task, the call may not return until that Task is selected again. No dispatch in the middle of an enclosing Event/Timer operation. |
| `karqri_posix_yield` | Rotate Scheduler current and dispatch only on a selection change. true denotes the dispatch path; no equal-priority peer, for example, can yield false. Use from a Task. This is not hardware preemption. |
| `karqri_posix_run` | Run a fixed group from the owner host thread. Return false if already running, otherwise return dispatch's result. true is not a successful join. Check every context.finished flag and external references separately. Returning with unfinished wait/suspend contexts, even with no Ready Tasks, is not supported completion. |

**Declarations versus verification:** All functions above have implementations. The following lists indicate direct references in the named test/validation source, not successful execution or complete branch coverage.

Direct references: `karqri_posix_port_init`, `karqri_posix_task_context_create`, `karqri_posix_task_context_destroy`, `karqri_posix_yield`, `karqri_posix_run`.

No direct references in this test: `karqri_posix_executing_task`, `karqri_posix_dispatch`. Distinguish internal calls and use by other tests/demos; this is not a claim of separate testing.

## 12. POSIX Kernel wrappers

Declarations: [ports/posix/posix_kernel.h](../ports/posix/posix_kernel.h). Implementation: [ports/posix/posix_kernel.c](../ports/posix/posix_kernel.c). Test definitions: [tests/test_posix_kernel.c](../tests/test_posix_kernel.c).

All calls are synchronous on the owner thread. wait is for an executing Task only. Foreign-thread and signal-handler calls to signal/tick/resume and other operations are forbidden. Wrappers do not make all shared raw primitives thread-safe.

```c
uintptr_t karqri_posix_wait_event(
    KarqriPosixPort *port,
    KarqriScheduledEventWaiter *waiter,
    KarqriEvent *event,
    uint32_t requested_events,
    KarqriEventWaitMode mode);

void karqri_posix_signal_event(
    KarqriPosixPort *port,
    KarqriEvent *event,
    uint32_t events);

void karqri_posix_timer_tick(
    KarqriPosixPort *port,
    KarqriTimerQueue *timer_queue);

bool karqri_posix_suspend_task(
    KarqriPosixPort *port,
    KarqriTask *task);

bool karqri_posix_resume_task(
    KarqriPosixPort *port,
    KarqriTask *task);
```

| API | Behavior, arguments, return value, and specific restrictions |
| --- | --- |
| `karqri_posix_wait_event` | Requires an executing Task, suppression=false, and another runnable context after blocking. Return an immediate snapshot as uintptr_t; otherwise dispatch and return the valid delayed value on resumption. No timeout. The implementation returns zero when there is no executing Task or the result is invalid on return; this is not a supported wait-failure contract. Unsupported uses are not guaranteed a diagnostic. |
| `karqri_posix_signal_event` | Complete Event signal before dispatching. events is an Event mask. Owner thread only; no reentry that invalidates an enclosing traversal. No return value. |
| `karqri_posix_timer_tick` | Complete the Timer tick and all expiries before dispatching. Explicit owner-thread call, not a signal-handler tick or a real-time clock API. |
| `karqri_posix_suspend_task` | Dispatch if scheduler_suspend succeeds. The bool reports the suspend state change. NULL task means logical current. Self-suspension needs another context and suppression=false. |
| `karqri_posix_resume_task` | Dispatch if scheduler_resume succeeds. The bool reports the resume state change. NULL returns false. Owner thread only. |

**Declarations versus verification:** All functions above have implementations. The following lists indicate direct references in the named test/validation source, not successful execution or complete branch coverage.

Direct references: `karqri_posix_wait_event`, `karqri_posix_signal_event`, `karqri_posix_timer_tick`, `karqri_posix_suspend_task`, `karqri_posix_resume_task`.

## 13. Pico context

Declarations: [ports/pico/pico_context.h](../ports/pico/pico_context.h). Implementation: [ports/pico/pico_context.c](../ports/pico/pico_context.c). Test definitions: [ports/pico/validation_main.c](../ports/pico/validation_main.c).

For port construction/checking. Hardware and software frames each contain eight uint32 words, totaling 64 bytes. saved_psp points to the software frame. The checker is not safe for an invalid context.

```c
bool karqri_pico_context_build(
    KarqriPicoTaskContext *context,
    KarqriTask *task,
    uint32_t *stack_base,
    size_t stack_words,
    void (*bootstrap)(KarqriPicoTaskContext *context),
    void (*return_trap)(void));

bool karqri_pico_context_contains_psp(
    const KarqriPicoTaskContext *context);
```

| API | Behavior, arguments, return value, and specific restrictions |
| --- | --- |
| `karqri_pico_context_build` | Build a 64-byte initial frame in the caller-provided region of stack_words uint32_t words. Round the top down to an 8-byte boundary; set R0=context, PC=bootstrap, LR=return_trap, and Thumb xPSR. Return false for NULL arguments, insufficient aligned space, and related checks; true on success. No allocation or Scheduler registration. Sufficient stack capacity is the caller's responsibility; 64 bytes does not guarantee execution headroom. |
| `karqri_pico_context_contains_psp` | Return true if saved_psp lies at or above stack_low and below stack_high and is 8-byte aligned. Does not read the live CPU PSP, validate the whole frame, check guards, or measure high-water usage. Requires a valid context and stack. |

**Declarations versus verification:** All functions above have implementations. The following lists indicate direct references in the named test/validation source, not successful execution or complete branch coverage.

Direct references: `karqri_pico_context_contains_psp`.

No direct references in this test: `karqri_pico_context_build`. Distinguish internal calls and use by other tests/demos; this is not a claim of separate testing.

## 14. Pico port

Declarations: [ports/pico/pico_port.h](../ports/pico/pico_port.h). Implementation: [ports/pico/pico_port.c](../ports/pico/pico_port.c). Test definitions: [ports/pico/validation_main.c](../ports/pico/validation_main.c).

The same header declares application-facing init/create/start/yield/executing/suppression functions and internal assembly/integration entry points. No examples call the latter directly. active/pending are context pointers; Scheduler current is a Task pointer.

```c
void karqri_pico_port_init(
    KarqriPicoPort *port,
    KarqriScheduler *scheduler,
    uint32_t slice_ticks);

bool karqri_pico_task_context_create(
    KarqriPicoPort *port,
    KarqriPicoTaskContext *context,
    KarqriTask *task,
    uint32_t *stack_base,
    size_t stack_words);

bool karqri_pico_start(KarqriPicoPort *port);

void karqri_pico_require_running(KarqriPicoPort *port);

bool karqri_pico_request_switch(KarqriPicoPort *port);

KarqriPicoTaskContext *karqri_pico_deliver_event_return(
    KarqriPicoTaskContext *next);

bool karqri_pico_yield(KarqriPicoPort *port);

KarqriTask *karqri_pico_executing_task(void);

bool karqri_pico_set_reschedule_suppressed(
    KarqriPicoPort *port,
    bool suppressed);

void karqri_pico_task_bootstrap(KarqriPicoTaskContext *context);

void karqri_pico_task_return_trap(void);
```

| API | Behavior, arguments, return value, and specific restrictions |
| --- | --- |
| `karqri_pico_port_init` | Store scheduler, slice_ticks, and the calling core; clear the context table and set started=false. slice_ticks=0 disables tick-driven slice rotation. Single runtime; initialize an unused port before starting. |
| `karqri_pico_task_context_create` | Build the caller-owned stack/context, initialize the remaining slice, register the context, and add the Task to Ready. Maximum 16 Tasks. Return false on the limit or build failure; true on success. Retain Task/context/stack until execution and all references have ended. No destroy API. |
| `karqri_pico_start` | Same core, privileged Thread/MSP, entry PRIMASK=0. Initial SVC enters Task execution; return true to main after every Task returns normally. Return false for an occupied runtime, already started port, missing current, or missing context. Invalid execution context causes a Release fault. The first bootstrap publishes RUNNING; final return clears it. Hardware alarm start/stop are separate APIs. Resolve all waits/suspends before completion. |
| `karqri_pico_require_running` | Internal guard; caller holds PRIMASK. Check active_port identity, started, and core affinity; fault on violation. Not an application entry point. |
| `karqri_pico_request_switch` | Port-internal/integration function. Save/restore PRIMASK and protect selection of next from Scheduler current through pending/ICSR SET/CLR. true means a request was issued or retained, not that a switch completed. The caller's preceding Scheduler operation updates current. Does not protect the entire surrounding Kernel update. A completed but undelivered value on active requires self-PendSV. |
| `karqri_pico_deliver_event_return` | Assembly only. Under PRIMASK, after updating the old saved_psp, deliver the valid delayed value to next's frame R0, clear only context D, and return next. Preserve Task validity. An invalid pending result faults. Do not call directly. |
| `karqri_pico_yield` | RUNNING, same core. Save/restore PRIMASK around current's timeslice rotation and request update. true means a switch request following a selection change; false means no change or a similar case. Intended for Task use; general ISR use has not been verified. |
| `karqri_pico_executing_task` | Return the Task of active_context, or NULL. Distinct from logical current. The read itself is not protected by PRIMASK. No guarantee for cross-core or external concurrent reads. |
| `karqri_pico_set_reschedule_suppressed` | RUNNING, same core; save/restore PRIMASK. Return the previous bool. true cancels an existing request; false reschedules and reevaluates the latest request. Suppression that would cancel an unfinished active wait handoff faults before modification. This does not disable interrupts. |
| `karqri_pico_task_bootstrap` | Internal initial PC: run the entry and handle its return. Check Thread/PSP and active consistency, then publish started=true on first entry. Return requires mask=0 and suppression=false. Protect finished/Task removal/next request; after all complete, set started=false, cancel requests, then invoke final SVC. Fault if unfinished contexts remain. Do not call directly. |
| `karqri_pico_task_return_trap` | Fallback trap used as the initial frame's LR. Infinite tight_loop_contents loop. Bootstrap handles normal completion; execution is not intended to return here. |

**Declarations versus verification:** All functions above have implementations. The following lists indicate direct references in the named test/validation source, not successful execution or complete branch coverage.

Direct references: `karqri_pico_port_init`, `karqri_pico_task_context_create`, `karqri_pico_start`, `karqri_pico_yield`, `karqri_pico_set_reschedule_suppressed`.

No direct references in this test: `karqri_pico_require_running`, `karqri_pico_request_switch`, `karqri_pico_deliver_event_return`, `karqri_pico_executing_task`, `karqri_pico_task_bootstrap`, `karqri_pico_task_return_trap`. Distinguish internal calls and use by other tests/demos; this is not a claim of separate testing.

## 15. Pico Kernel wrappers

Declarations: [ports/pico/pico_kernel.h](../ports/pico/pico_kernel.h). Implementation: [ports/pico/pico_kernel.c](../ports/pico/pico_kernel.c). Test definitions: [ports/pico/validation_main.c](../ports/pico/validation_main.c).

Only wait is defined in assembly (pico_switch.S). EventWaitCall holds port/waiter/event/requested_events/mode/blocked. signal/suspend/resume are C wrappers. Entry requirements and supported contexts differ by function.

```c
uint32_t karqri_pico_event_wait(KarqriPicoEventWaitCall *call);

uint32_t karqri_pico_event_wait_prepare(KarqriPicoEventWaitCall *call);

void karqri_pico_event_signal(
    KarqriPicoPort *port,
    KarqriEvent *event,
    uint32_t events);

bool karqri_pico_suspend_task(
    KarqriPicoPort *port,
    KarqriTask *task);

bool karqri_pico_resume_task(
    KarqriPicoPort *port,
    KarqriTask *task);
```

| API | Behavior, arguments, return value, and specific restrictions |
| --- | --- |
| `karqri_pico_event_wait` | Keep call and all referenced objects valid. Requires privileged Thread/PSP, RUNNING, and entry PRIMASK=0. call.blocked is internal output. Protect condition checking, registration, Scheduler update, and request publication in one masked region. Return the immediate snapshot or, after blocking, the snapshot delivered through PendSV/R0 as uint32_t. No timeout. Blocking without a valid request, returning before delivery, or violating context/mask requirements causes a diagnostic stop in Release too. No busy loop. |
| `karqri_pico_event_wait_prepare` | Assembly wrapper only, under its enclosing PRIMASK region. Return a snapshot on immediate completion; after blocking, set D/blocked and request a switch, then return an internal placeholder zero. The application must not receive that placeholder. |
| `karqri_pico_event_signal` | RUNNING, same core. Save/restore PRIMASK around the entire signal, callbacks, and request update. Task and port tick IRQ integration paths exist; this is not a guarantee of safety from arbitrary ISRs, NMI, or another core. Event non-reentry restrictions still apply. |
| `karqri_pico_suspend_task` | RUNNING, same core. Protect Scheduler suspend and request updates with PRIMASK. NULL task means logical current; the bool reports state change. For self-suspension, the caller must provide another runnable context and no suppression. No guarantee for self-operations from arbitrary ISRs. |
| `karqri_pico_resume_task` | RUNNING, same core. Protect Scheduler resume and request updates with PRIMASK. NULL or not suspended returns false; the bool reports state change. Do not generalize support to arbitrary ISRs; separate tests outside the tick path have not been verified. |

**Declarations versus verification:** All functions above have implementations. The following lists indicate direct references in the named test/validation source, not successful execution or complete branch coverage.

Direct references: `karqri_pico_event_wait`, `karqri_pico_event_signal`, `karqri_pico_suspend_task`, `karqri_pico_resume_task`.

No direct references in this test: `karqri_pico_event_wait_prepare`. Distinguish internal calls and use by other tests/demos; this is not a claim of separate testing.

## 16. Pico hardware tick

Declarations: [ports/pico/pico_tick.h](../ports/pico/pico_tick.h). Implementation: [ports/pico/pico_tick.c](../ports/pico/pico_tick.c). Test definitions: [ports/pico/validation_main.c](../ports/pico/validation_main.c).

Hardware alarm ownership and RUNNING are separate intervals. irq_count counts raw callbacks; kernel_ticks counts only while RUNNING; switch_requests counts successful requests, not switches; missed_rearms is cumulative; consecutive_misses is consecutive. uint32 counters are not guaranteed never to wrap over indefinite operation. Each callback rearms relatively, and the first Kernel tick accepts the existing alarm phase. No elapsed-time compensation. A rearm miss forces an IRQ; success resets the consecutive count to zero. The default permits eight consecutive misses and faults on the ninth. This provisional budget is overridable by a build-time macro and does not guarantee Thread/PendSV progress.

```c
bool karqri_pico_tick_start(
    KarqriPicoTick *tick,
    KarqriPicoPort *port,
    KarqriTimerQueue *timer_queue,
    uint32_t period_us);

void karqri_pico_tick_stop(KarqriPicoTick *tick);
```

| API | Behavior, arguments, return value, and specific restrictions |
| --- | --- |
| `karqri_pico_tick_start` | Same-core privileged Thread only; restore the caller's original PRIMASK. period_us must be positive; tick/port/timer_queue must be non-NULL. Return false for an already registered tick, no unused alarm, or a missed initial arm. On initial miss, clean up registration, IRQ causes/pending state, and ownership without forcing an IRQ. true starts the hardware alarm. While started=false, count only raw callbacks without entering the Kernel. |
| `karqri_pico_tick_stop` | Same-core privileged Thread only. Under saved/restored PRIMASK: cancel alarm, unregister callback, clear peripheral causes, clear NVIC pending, remove registry entry, then unclaim. An unregistered tick, a failed start, or NULL is a registry no-op. No return value. Retain storage until stop returns. IRQ/callback reentry and cross-core calls are forbidden. |

**Declarations versus verification:** All functions above have implementations. The following lists indicate direct references in the named test/validation source, not successful execution or complete branch coverage.

Direct references: `karqri_pico_tick_start`, `karqri_pico_tick_stop`.

## 17. Internal headers and symbols (not application APIs)

- [pico_interrupt.h](../ports/pico/pico_interrupt.h): `static inline uint32_t karqri_pico_interrupt_save_and_disable(void)` returns the previous mask using MRS PRIMASK followed by CPSID I. `static inline void karqri_pico_interrupt_restore(uint32_t primask)` restores the saved value using MSR. Same-core only; nested uses restore their respective saved values. Does not arrange switching. The restore helper itself has no ISB; the wait wrapper executes one separately.
- [pico_fault.h](../ports/pico/pico_fault.h): `_Noreturn void karqri_pico_fault(uint32_t reason)` records volatile `karqri_pico_fault_reason` and executes DSB/UDF. Enabled in Release; does not return. This is not a normal error-return API. Reasons: 1=WAIT_PRIMASK, 2=WAIT_CONTEXT, 3=HANDOFF, 4=MSP_ALIGNMENT, 5=SUPPRESSION, 6=DELAYED_RETURN, 7=PORT_CONTEXT, 8=PORT_STATE, 9=UNFINISHED_TASKS, 10=TICK_CONTEXT, 11=TICK_MISSED. Automatic USB fault output is not guaranteed; retrieve the reason through SWD or a similar method.
- [pico_context_offsets.h](../ports/pico/pico_context_offsets.h), [pico_kernel_offsets.h](../ports/pico/pico_kernel_offsets.h): C/assembly ABI constants for frames, registers, context D, and blocked. Checked by static assertions. Applications must not modify storage using these offsets directly.
- `karqri_pico_active_context`, `karqri_pico_pending_context`, and `karqri_pico_switch_count` are externs in pico_port.h. active represents physical handoff state; pending is the next unconsumed context. The counter increments for nonempty PendSV, including self-switches. Do not assign externally. It does not count Task instructions executed.

### 17.1 Private cooperative trace

[pico_trace.h](../ports/pico/pico_trace.h) is explicitly private. `KARQRI_PICO_TRACE=0` removes the buffer and recording code; begin/freeze/dump macros become no-ops. With =1 it defines:

```c
void karqri_pico_trace_begin(KarqriPicoPort *port);
void karqri_pico_trace_freeze(void);
void karqri_pico_trace_dump(const char *labels);
void karqri_pico_trace_record(uint16_t event, uint8_t source,
                             uintptr_t subject, uint32_t value);
void karqri_pico_trace_commit(void);
```

begin resets capture state, sequence/counters, and start time for a valid port. freeze stops capture and records the end time. After freeze, dump reads labels for context_count entries and outputs the batch through USB/stdio from main; it outputs nothing while capturing or without a port. During capture, record saves/restores PRIMASK around insertion into the fixed ring. A full ring overwrites the oldest records and increments overwritten. The sequence limit stops capture and sets overflow. commit is PendSV-only, recording the old context and request sequence. `karqri_pico_trace_old_context` is an extern scratch pointer for PendSV.

`KarqriPicoTraceRecord` is 32 bytes: 256 entries use 8192 bytes plus bookkeeping. Fields are sequence, low 32 bits of the 1-µs timer, active/current/pending/subject/value, and event/source/flags. Events are BOOTSTRAP/MARK/YIELD/SET/REPLACE/CANCEL/COMMIT/RETURN/STOP; sources are UNKNOWN/FROM_YIELD/FROM_RETURN. `KARQRI_TRACE`/`KARQRI_TRACE_ARG` are build macros. Instrumentation is limited to cooperative_three_task; it is not a general trace service or support for other-core/NMI/HardFault recording. dump reports time_valid based on elapsed time being less than 2^32 µs.

HANDOFF_COMMIT is an observation immediately before exception return, not proof that a Task instruction executed. Recording affects stack use, latency, and IRQ timing. Existing hardware evidence of 38 records/8 handoffs/ADGBEHCFI/overwritten=0 contains no firmware hash in its log, and the identity of the executed binary has not been independently established.

## 18. Minimal usage examples

### 18.1 Registering an Event notification from a caller-managed Timer

```c
KarqriEvent event;
KarqriTimerQueue timers;
KarqriTimer pool[2];
karqri_event_init(&event);
karqri_timer_queue_init(&timers, pool, 2);
bool registered = karqri_timer_start(
    &timers, &event, UINT32_C(1), 3, karqri_timer_signal_event);
/* false means no registration. Keep objects alive through notification.
 * This is a single-environment registration example. Protect the entire
 * operation separately for a shared Pico queue. The port/application
 * determines whether to advance the logical clock by three ticks. */
(void)registered;
```

Include `karqri/event.h`, `karqri/timer.h`, and `karqri/timer_event.h`. This example does not implement an Event wait timeout; there is no API returning a timeout/cancellation result.

### 18.2 Execution and completion examples

For complete startup and lifetime management, see the [POSIX integration test](../tests/test_posix_kernel.c), [Pico validation](../ports/pico/validation_main.c), [POSIX demo](../demos/posix/track_demo.c), and [Pico demo](../demos/pico/track_demo.c). The sequence is Task init, context create (including Scheduler registration), run/start, confirm normal completion of all Tasks and external references, then POSIX destroy / Pico tick stop. Raw Task initialization alone does not start a Task.

Typical Pico setup initializes Scheduler/port/Event/Timer/Task objects, creates every context, and registers any required Timers before tick_start followed by start. The alarm runs before start, but does not enter the Kernel until RUNNING is published. Do not treat tick_start failure as successful execution startup. If start returns false, stop any alarm still owned. After normal start return, call tick_stop and confirm notification/reference termination before reusing caller-owned storage. See validation/demo branches for concrete failure cleanup.

## 19. Verification status, unverified behavior, and usage cautions

| Evidence | Supported scope | Claims it does not support |
| --- | --- | --- |
| Existing host 7/7 PASS | Queue/Task/Event/Timer/Timer-Event/Scheduler/POSIX integration test definitions; unit tests include Event reuse and same-tick notifications | Not every input to every API, ISR races, or all execution contexts. Existing execution evidence, not a new run. |
| Five Pico validation phases | Normal single_task, cooperative_three_task, event_delayed_r0, suspend_resume, hardware_preemption paths; PSP/guard/register checks as implemented per scenario | Not hardware coverage of early-wake self-PendSV or all handoff interleavings. |
| Existing supplementary verification records | Stub/emulation/disassembly evidence for initial-arm failure cleanup, miss recovery, masking/handoff, and related cases | Not all test artifacts are in the current suite; do not promote this to measured RP2040 guarantees. |
| POSIX demo normal/fast | 6000 explicit checkpoint ticks and normal completion of six Tasks, among other checks | Not hardware timer preemption or stack-headroom measurement. |
| CDC-updated Pico hardware run | User report: 6000 ticks, irq=6000, switches=6027, critical=26,26, waits=22, overlap=0, owner=-1, waiters_empty=YES, guards=intact, PASS | Not board operation/hash verification by Codex, or per-API/special-boundary hardware PASS. |

Implementations were confirmed for all functions in scope. No declaration-only, unimplemented public function was found. Separate successful tests of internal entry points/functions without direct test references, arbitrary ISR support, and unsupported argument domains remain unverified.

Usage cautions:

1. Pico implements wait-registration serialization, protected switch requests/PendSV handoff, and RUNNING-gated tick lifecycle. This does not mean all special paths have been verified on hardware. Callers remain responsible for synchronization when directly accessing shared primitives. See [scheduler_event.h](../include/karqri/scheduler_event.h), [timer.h](../include/karqri/timer.h), and [timer_event.h](../include/karqri/timer_event.h).
2. The POSIX run bool, context.active, and Task RUNNING bit do not establish all-Task completion or CPU execution from their names. These are header-contract restrictions, not a finding of a runtime defect.
3. Pico request reads Scheduler current; it does not reselect the Ready head returned by `scheduler_candidate()`. Use it together with the caller's Scheduler updates. The request function alone does not replace protection around raw operations.
4. Pico context_contains_psp checks only saved_psp. Do not describe it as full-frame/stack-guard validation. Tests across the entire arithmetic domain, such as arbitrarily large stack_words, have not been verified.
5. POSIX does not guarantee Pico-like diagnostics for Task return under suppression or remaining waiters. Do not treat a zero result or host return from unsupported execution as success.

Items with remaining unverified behavior include repeated waiters' delayed values in real contexts, same-tick wake FIFO execution, early wake/self-PendSV/P/H replacement and cancellation/empty requests, IRQs at SVC boundaries, alarm miss/stop/restart, MSP high-water/IRQ nesting, and trace perturbation. Keep normal API contracts distinct from completed verification. See [API Design Notes](api-design.md) for rationale and [README verification status](../README.md#verification-status) for normal-path results.
