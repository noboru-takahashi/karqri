# Karqri API Design Notes

[English (authoritative)](api-design.md) | [日本語 (translation)](api-design-ja.md)

See the [API Reference](api-reference.md) for current contracts and [project history](history.md) for the relationship between CHARM-II, the 2026 research implementation, and Karqri. These notes explain the existing design; they do not add API guarantees or verification results. Implementation and test references below point to files in this source tree.

## 1. Starting point and current scope

Karqri is a separate RTOS developed from CHARM-II reconstruction and behavioral analysis. The initial scope is execution of the same kernel on POSIX and Pico with a six-task demo on both, rather than source compatibility or reproduction of every old OS service. Systematic comparison of superloop/cooperative/preemptive execution and redesign of the state model remain future development topics, not current API features.

Preserving selected behavior is a design approach, not a permanent contract to preserve every historical algorithm. Karqri adopts equal-priority Ready FIFO ordering and does not retain the former Memory service or task_event adapter as current APIs.

Karqri carries forward specific implementation work and knowledge from the 2026 research project, including parts of the Pico execution infrastructure and demo, while adapting them to its own architecture. This is distinct from directly porting the 1986 MC68000 implementation. Behavioral inheritance, source expression, author statements, independently verified provenance, and rights ownership must be considered separately. Karqri's MIT license does not relicense the historical work. This document neither reproduces original CHARM-II source nor proves independent source provenance or legal clearance; it does not resolve outstanding provenance questions.

Normal demo operation and unverified special paths are distinguished in section 13. A successful normal run is not verification of every interrupt interleaving, and publication preparation does not establish completion of all verification.

## 2. Layer responsibilities and public boundaries

| Layer | Responsibilities | Outside its responsibilities | Main evidence |
| --- | --- | --- | --- |
| Queue | Intrusive lists of caller-owned nodes; positional/priority insertion | Message contents, Task waiting, allocation, mutual exclusion | [queue.h](../include/karqri/queue.h), [queue.c](../kernel/queue.c) |
| Task | Entry/argument, priority, state bits, delayed result | Ready ordering, CPU execution, stacks | [task.h](../include/karqri/task.h), [task.c](../kernel/task.c) |
| Scheduler | Ready/Suspended membership, candidate and logical current, priority FIFO, rotation | CPU save/restore, waiter cancellation, context deallocation | [scheduler.h](../include/karqri/scheduler.h), [scheduler.c](../kernel/scheduler.c) |
| Event | Pending bitset, ANY/ALL, waiter list, synchronous callbacks | Task-specific management, CPU blocking, timeouts | [event.h](../include/karqri/event.h), [event.c](../kernel/event.c) |
| scheduler_event | Connect Event callbacks to Task delayed results and Scheduler wait completion | Stack saving, actual dispatch, mid-wait cancellation | [scheduler_event.h](../include/karqri/scheduler_event.h), [scheduler_event.c](../kernel/scheduler_event.c) |
| Timer | Caller pool, delta queue, logical ticks, one-shot expiry | Hardware time acquisition, elapsed-time compensation, periodic operation, Task-specific processing | [timer.h](../include/karqri/timer.h), [timer.c](../kernel/timer.c) |
| timer_event | Connect Timer's opaque destination/events to Event | Its own queue/state, dispatch, timeout rollback | [timer_event.h](../include/karqri/timer_event.h), [timer_event.c](../kernel/timer_event.c) |
| Port | Stacks/native contexts, execution, handoff, notification/dispatch ordering | General automatic synchronization of every Kernel primitive; arbitrary foreign-thread/core bridges | [POSIX port](../ports/posix/posix_port.c), [Pico port](../ports/pico/pico_port.c) |
| Demo adapter | Connect platform ticks/output/wait/yield/suppression | Kernel contract changes | [POSIX adapter](../demos/posix/track_demo.c), [Pico adapter](../demos/pico/track_demo.c) |
| Common workload | Six-task movement, Event gate, critical owner, rendering/results | Hardware preemption implementation | [Common workload](../demos/common/track_demo.c) |

The API boundary classifies Queue/Task/Event/Timer as application/public interfaces and Scheduler/scheduler_event/timer_event as internal/integration interfaces. Placement under `include/karqri/` alone does not guarantee unrestricted application manipulation of internal state. Port headers also mix application entry points with assembly helpers; visibility of a declaration does not mean direct use is recommended.

The design keeps synchronization and dispatch out of raw primitives, specifying ownership, lifetime, and serialization as contracts. The atomicity boundary is not one queue pointer write: it is condition checking, waiter registration, Scheduler update, and port handoff together, or an entire Timer tick and its callbacks before dispatch.

## 3. Queue: generic nodes only

[queue.h](../include/karqri/queue.h) and [queue.c](../kernel/queue.c) implement a sentinel-based circular doubly linked list. The caller embeds nodes in its objects. Nodes are detached after pop/remove. Without malloc or payload copying, lifetime and single-queue membership of linked objects remain caller responsibilities.

The priority callback returns signed int32, sorted descending with FIFO among equals. Karqri adopts FIFO rather than retaining all baseline ordering. Scheduler uses a separate helper comparing Task uint32 priorities directly, without forcing them through the signed generic callback.

To preserve equal-priority registration order, the queue must already use the same sorting rule, and priorities must not change while linked. Automatic detection of dynamic priority changes was not adopted for Queue. Remove first, change the attribute, then reinsert. Unchecked API misuse does not mean arbitrary arguments have defined behavior.

`tests/test_queue.c` checks FIFO/LIFO, equal priorities, negative priority, callback context, and remove/reinsert. Negative-priority tests do not establish Scheduler behavior across its unsigned domain.

## 4. Task and Scheduler: separate attributes, selection, and CPU execution

### 4.1 Task state is not a single enumeration

The initial release retains the historical bitmask. EVENT_WAIT, RECEIVE_WAIT, SEND_WAIT, SUSPENDED, READY, RUNNING, and JAM_WAIT are defined, but do not all represent implemented services. `karqri_task_is_waiting()` tests only EVENT/RECEIVE/SEND.

READY|SUSPENDED and RUNNING left on a Task no longer selected are explicitly part of current behavior. The initial release does not redesign state as a mutually exclusive enumeration or make RUNNING an authoritative indication of CPU execution. Future separation of conditions and scheduling state is a reconsideration candidate, not permission for current callers to reinterpret bits independently.

Task suspend/resume/priority setters affect state or attributes only. Control registered Tasks through Scheduler/port paths that preserve queue consistency. Reading a delayed value does not consume it; clearing it invalidates only the flag. These rules distinguish immediate Event results from delayed ones.

### 4.2 The current Ready policy is FIFO

Old CHARM-II inserted newly Ready Tasks before existing equal-priority entries; Karqri instead inserts them at the tail of that group. The historical connection to VRTX is an explanation consistent with the author's recollection and available documentation, not a direct record proving each 1986 decision. This document does not turn that interpretation into a definitive source-provenance claim.

Current `insert_ready_fifo` sorts by descending priority, placing equals at the tail. Creation, readying, wakeup, resume, and reinsertion after a priority change follow that rule. Setting the same priority is a no-op preserving position. Timeslice rotation moves current, or an explicit Task, to the tail of its group. A higher-priority Task becoming Ready may change logical current; actual switching depends on the port.

### 4.3 current/candidate/active/pending

| Value | Meaning | Referencing layer |
| --- | --- | --- |
| Scheduler candidate | Head of Ready | Scheduler |
| Scheduler current | Last logically selected Task; may differ from candidate under suppression | Scheduler/port integration |
| POSIX executing_context | Currently executing native context | POSIX port |
| Pico active_context | Executing context in the physical handoff state | Pico port/PendSV |
| Pico pending_context | Next handoff target; NULL can represent an empty request | Pico port/PendSV |

Scheduler reschedule replaces the new current's state with RUNNING without repairing the old Task's state. Ready includes the selected Task. State, Ready membership, logical current, and CPU execution therefore are not synonyms.

### 4.4 Suppression and Task completion

Suppression is a bool preventing reselection, not a queue-update freeze or IRQ protection. Clearing the raw setter does not select a new current. The Pico wrapper reschedules and reevaluates requests on release; POSIX has no corresponding wrapper, so integration code does this explicitly.

Leave another runnable context when blocking or self-suspending. The design permits all Tasks to return normally without mandating a permanent idle Task. However, other waits/suspends must be resolved before the last Ready Task returns. Scheduler remove is neither waiter cancellation nor context destruction; see [scheduler.h](../include/karqri/scheduler.h).

## 5. Event: bitsets, snapshots, and callbacks

### 5.1 Notifications are not counts

Event ORs 32 pending bits. Repeated signals set the same bit rather than accumulating a count. ANY/ALL test the requested mask, but the result is the **full pending snapshot before consumption**, not its intersection with the request. Only requested bits are consumed on completion.

signal uses the initial pending|signal snapshot for every waiter; consumption by an earlier waiter does not change the snapshot used to test later waiters. One signal can therefore complete multiple waiters. FIFO describes traversal order; an unsatisfied head waiter does not block later waiters.

This does not directly guarantee exclusive acquisition of a gate. The common track workload maintains a separate critical owner, rechecks ownership after an Event wakeup, and waits again when necessary. Do not treat Event as a semaphore or mutex. See [demos/common/track_demo.c](../demos/common/track_demo.c).

### 5.2 Raw wait and Task blocking are separate

An unsatisfied raw Event wait sets waiter information, calls wait_started, then enqueues FIFO. COMPLETED invokes no callbacks; BLOCKED reports registration, and the C call itself returns.

scheduler_event callbacks invalidate the Task delayed result and move it to EVENT_WAIT. Completion stores a valid delayed value before Scheduler ready. Immediate completion preserves existing Task state and delayed validity. The port blocks CPU execution. Mid-wait cancellation and deletion of waiting Tasks were not adopted.

This boundary explains why IRQ arrival during registration is particularly dangerous. Calling the raw API alone does not make condition checking through registration atomic. The Pico wrapper encloses the whole sequence without changing kernel algorithms; see [pico_kernel.c](../ports/pico/pico_kernel.c) and [pico_switch.S](../ports/pico/pico_switch.S).

### 5.3 Removal of task_event

The former task_event adapter was an intermediate connection between Task and Event before Scheduler existed. Task+Event alone is not maintained as a separate supported architecture. The current connection is [scheduler_event](../kernel/scheduler_event.c), with behavior covered by the raw Event and Scheduler integration tests. Current scheduler_event and old task_event are not two parallel public alternatives.

## 6. Timer and the Timer/Event adapter

Timer provides a caller-owned fixed pool, a delta queue, logical ticks, and one-shot callbacks. Each tick decrements the head delta; equal-deadline timers expire in registration order. cancel removes the first match of destination pointer and the full events value, repairing the following delta. The expiry function is not part of the key.

Expiry removes the entry from active, invokes the callback synchronously, then returns it to free. Inability to reuse that entry during its callback matters when sizing the pool. Reentrant start/cancel/tick on the same queue from expiry is outside the contract. The API provides no periodic callbacks, Timer handles, destroy operation, or Task timeouts.

delay=0 is outside the current public contract; use 1..UINT32_MAX. An earlier zero-delay wrap test is not part of the current contract or test coverage; it must not be treated as a guarantee for zero. The UINT32_MAX test checks delta/repair boundaries and advancement by one tick; it does not wait that many real-time ticks.

Timer itself treats events as opaque. Only timer_event interprets it as an Event bitmask and signals synchronously. Same-tick expiries are signaled individually rather than merged, so later signals use pending after previous consumption. cancel removes one future notification without rolling back Event pending or waiters. Pool exhaustion sends no failure notification to Event.

The POSIX Timer wrapper dispatches after the entire tick and callbacks. Pico requests PendSV after all Timer/slice processing. Replacing this with immediate switching from inside a callback is outside the current contract.

## 7. POSIX port: cooperative contexts and a limited usage contract

The [POSIX port](../ports/posix/posix_port.c) uses ucontext, port-owned malloc stacks, and integer trampoline slots, under the [limited execution contract](../ports/posix/README.md). A native context is separate from Task; the trampoline slot identifies its Task.

The design assumes one kernel per execution environment. In the current implementation, `trampoline_slots[]`, the context table, host_context, and executing/running state are needed by the current structure. It does not adopt removal of all globals or multi-instance support.

All initialization, reads/writes, execution, and destruction occur on one owner thread. No foreign-thread/signal-handler API, asynchronous tick/dispatch bridge, thread-affinity checks, or live context cancellation/destruction was added. These are outside the initial release, not unimplemented operations that callers may assume work.

Normal Task return removes the Task from Scheduler. destroy releases only the stack and slot, without clearing the port's pointer table or context_count. Verify every context in the fixed group has finished and all notification sources/external references have ended, destroy on the host, then retire the old port. Final Task return does not scan for remaining waits/suspends, so host return alone cannot establish successful completion. Suppression violations are not guaranteed Pico-like faults either.

The limited contract describes supported use; it does not imply additional runtime enforcement. Future cancellation or host-driven resumption should be treated as a separate design extension rather than conflated with an existing defect fix.

## 8. Pico port: request, handoff, and RUNNING boundaries

### 8.1 Protection boundaries and implementation

| Boundary | Measures | Implementation |
| --- | --- | --- |
| Interrupt state and frame ABI | Save/restore PRIMASK; assert context/frame layout | [pico_interrupt.h](../ports/pico/pico_interrupt.h), [pico_context.c](../ports/pico/pico_context.c) |
| PendSV handoff | Mask handler internals; return unchanged on empty pending; preserve EXC_RETURN | [pico_switch.S](../ports/pico/pico_switch.S) |
| Request and Event wait | Share C request rules; mask the entire wait registration; deliver delayed R0 in PendSV | [pico_port.c](../ports/pico/pico_port.c), [pico_kernel.c](../ports/pico/pico_kernel.c), [pico_switch.S](../ports/pico/pico_switch.S) |
| Startup, shutdown, and ticks | Publish/clear RUNNING at initial bootstrap/final return; separate alarm ownership from RUNNING; handle misses | [pico_port.c](../ports/pico/pico_port.c), [pico_tick.c](../ports/pico/pico_tick.c) |

The relevant races span wait checking through registration, shared kernel operations, request and active/pending transitions, and startup/shutdown tick boundaries. Protecting a single queue primitive, checking started alone, unconditionally enabling IRQs, or polling delivery in a busy loop is not a complete solution.

### 8.2 Requests and delayed R0

Here D denotes the context's `delayed_return_pending`, P denotes `pending_context`, and H denotes the PendSV pending bit.

Shared processing in [port C](../ports/pico/pico_port.c) obtains next from Scheduler current and updates pending and SCB ICSR SET/CLR in the same PRIMASK region. Replace an old request for another Task with the latest next; cancel unnecessary self-requests. However, retain self-PendSV if active has D set for a completed but undelivered value. Suppression must not cancel an unfinished handoff; unsupported suppression faults before state modification.

The old separation between a trailing wrapper SET and C preparation, and the old practice of writing R0 into stale saved_psp during preparation, are no longer used. PendSV saves old R4–R11/PSP and updates old.saved_psp before delivering R0 into next's current saved frame. Self-switching uses the same sequence. Delivery clears only D, retaining the Task delayed-result valid flag.

The wrapper checks entry PRIMASK=0 and privileged Thread/PSP, then masks from C preparation through request publication. After restoration, it executes ISB and checks D once for diagnostics. This detects an incomplete wait returning placeholder zero; it is not a waiting/retry mechanism. Do not generalize ISB alone into a guarantee that any pending exception is immediately accepted. Appropriate execution state, priority, and preservation of the request are prerequisites; special hardware timing remains a separate verification task.

PendSV restores the original PRIMASK only after handoff is complete, then returns from the exception without modifying context, PSP, Task registers, or counters. Calls to C helpers preserve EXC_RETURN and MSP alignment. This does not establish that all MSP headroom measurements are complete.

### 8.3 Hardware alarms and RUNNING

A hardware alarm may be claimed and running before start, but a started=false callback only counts raw irq_count and rearms; it does not access Timer/Scheduler/context requests. After initial SVC establishes PSP, bootstrap checks consistency and publishes started=true. The first Kernel tick accepts the existing alarm phase. Final normal return publishes started=false and cancels requests before final SVC returns to main. Alarm stop/unclaim is a separate host operation.

A missed initial arm cleans up and returns false without forced-IRQ recovery, avoiding the previous owner's target_hi problem. During operation, a rearm miss forces another IRQ based on SDK conditions satisfied on callback entry. Without elapsed-time compensation, Kernel tick counts do not precisely represent elapsed wall time. The default consecutive-miss limit of eight is a finite budget: fault on the ninth, reset on successful arm. It guarantees neither Thread/PendSV progress on every iteration nor a measured latency bound.

start/stop are restricted to privileged Thread mode on the same core; PRIMASK protects registration/removal. A single started flag representing both alarm ownership and permission for Task execution was not adopted.

## 9. Common workload and port-specific execution drivers

Movement, critical zone, Event gate, ANSI rendering, and results are shared in [demos/common](../demos/common/track_demo.c). POSIX runs six independent ucontexts cooperatively, advancing logical ticks at checkpoints in the executing Task. Pico retains hardware alarms and PendSV.

The same-Kernel/same-workload comparison does not require identical tick timing, rotation/movement counts, or critical acquisition counts. POSIX normal/fast modes differ in pacing; neither implements signal-driven hardware preemption. Do not confuse adapter differences with differences in the shared workload.

The maximum 10-second CDC connection wait and terminal-settling delay in the [Pico demo adapter](../demos/pico/track_demo.c) prepare demo output before Tasks/ticks start. They do not change Kernel startup APIs or Scheduler policy. Existing PASS conditions remain unchanged.

[Trace](../ports/pico/pico_trace.c) is private instrumentation for cooperative_three_task. The [collector](../tools/pico_validation/capture.py) and [Viewer](../tools/pico_validation/trace_viewer.py) capture/analyze on Windows. Normal API execution does not printf; records go to RAM and are printed from main after freeze. An untraced demo needs neither collector nor Viewer. HANDOFF_COMMIT does not prove Task execution. WASM/GUI/Viewer extensions are not additional initial-publication requirements.

## 10. Index of rejected or deferred designs

| Alternative | Current treatment | Source or contract |
| --- | --- | --- |
| Source/binary compatibility with the old OS | Not an initial-release requirement | [Project history](history.md) |
| Permanently retain old equal-priority Ready LIFO | Changed to FIFO | [scheduler.c](../kernel/scheduler.c) |
| Task+Event alone as a second supported architecture | Replace the intermediate task_event adapter with Scheduler integration | [scheduler_event.h](../include/karqri/scheduler_event.h) |
| Standard Memory allocator service | Arena/partition APIs removed; delegate to application/platform | [Current API scope](api-reference.md) |
| Remove all instance-related state / support multiple instances | Not an initial requirement; permit necessary port-local state | [POSIX port contract](../ports/posix/README.md), [pico_port.c](../ports/pico/pico_port.c) |
| Complete Task state redesign | Retain historical semantics; reconsider later | [task.h](../include/karqri/task.h) |
| Mid-wait Event cancellation / deletion of waiting Tasks | Outside the initial contract; no new API | [scheduler.h](../include/karqri/scheduler.h), [event.h](../include/karqri/event.h) |
| Timer handles/periodic operation/destroy/zero-delay guarantee | Not added; delay>=1 | [timer.h](../include/karqri/timer.h) |
| Lock/dispatch inside every primitive | Caller/port responsibility; preserve enclosing operation completion | [Event contract](../include/karqri/event.h), [Timer contract](../include/karqri/timer.h) |
| POSIX foreign-thread/signal/live-destruction support | Outside the limited contract; general diagnostics not guaranteed | [POSIX port contract](../ports/posix/README.md) |
| Pico wait delivery busy loop | Use PendSV delivery and abnormal-state diagnostics instead | [pico_switch.S](../ports/pico/pico_switch.S) |
| Elapsed-time compensation for missed ticks | None; finite miss-recovery budget | [pico_tick.c](../ports/pico/pico_tick.c) |

“Deferred” is not a promise of future implementation. The alternatives here reflect existing design choices, not newly proposed guarantees.

## 11. Conceptual similarities to and differences from CHARM-II

Common behavior includes priority Tasks, wait/suspend bits, Ready rotation, Event ANY/ALL with snapshot consumption, delta Timers, and notification-driven readying. Some distinctive historical states remain; others, such as equal-priority Ready ordering, were explicitly changed to FIFO.

Karqri uses caller-owned objects, stdint types, callback-based generic Events/Timers, Scheduler adapters, and port separation. POSIX uses ucontext; Pico uses PSP/MSP/SVC/PendSV. Old 68000 registers/interrupt frames and old syscall numbers are not public API compatibility requirements. Memory and the old communications services as a whole are not current features.

This is a conceptual/design comparison. Source-expression provenance, AI inputs, and rights ownership require separate evidence. Structural differences do not prove that no code was reused. As described in the [project history](history.md), the 2026 research implementation contributed specific Pico and demo code as well as knowledge; this must be distinguished from the historical MC68000 source.

## 12. Source and verification index

These files provide implementation and test definitions. A test's presence is not evidence that every branch, execution context, or interrupt interleaving has been verified. Normal execution results and supplementary checks are separate from special-path hardware verification.

| Area | Implementation | Test or verification scope |
| --- | --- | --- |
| Queue | [queue.c](../kernel/queue.c) | [test_queue.c](../tests/test_queue.c) |
| Task | [task.c](../kernel/task.c) | [test_task.c](../tests/test_task.c) |
| Event | [event.c](../kernel/event.c), [scheduler_event.c](../kernel/scheduler_event.c) | [test_event.c](../tests/test_event.c), [test_scheduler.c](../tests/test_scheduler.c) |
| Timer | [timer.c](../kernel/timer.c) | [test_timer.c](../tests/test_timer.c) |
| Timer/Event | [timer_event.c](../kernel/timer_event.c) | [test_timer_event.c](../tests/test_timer_event.c), [test_scheduler.c](../tests/test_scheduler.c) |
| Scheduler | [scheduler.c](../kernel/scheduler.c) | [test_scheduler.c](../tests/test_scheduler.c) |
| Cross-subsystem contracts | [Public and integration headers](../include/karqri/scheduler_event.h) and sections 2–6 | The seven host suites listed in the [Makefile](../Makefile) have distinct scopes |
| POSIX contexts and integration | [posix_port.c](../ports/posix/posix_port.c), [posix_kernel.c](../ports/posix/posix_kernel.c) | [test_posix_kernel.c](../tests/test_posix_kernel.c) |
| Pico context, handoff, and lifecycle | [pico_context.c](../ports/pico/pico_context.c), [pico_port.c](../ports/pico/pico_port.c), [pico_switch.S](../ports/pico/pico_switch.S), [pico_tick.c](../ports/pico/pico_tick.c) | [validation_main.c](../ports/pico/validation_main.c); normal hardware results do not cover every special path |
| Shared demo and CDC startup | [Common workload](../demos/common/track_demo.c), [POSIX adapter](../demos/posix/track_demo.c), [Pico adapter](../demos/pico/track_demo.c) | Demo result checks; see [README verification status](../README.md#verification-status) |
| Optional trace and Windows tools | [pico_trace.c](../ports/pico/pico_trace.c), [capture.py](../tools/pico_validation/capture.py), [trace_viewer.py](../tools/pico_validation/trace_viewer.py) | [test_capture.py](../tools/pico_validation/test_capture.py), [test_trace_viewer.py](../tools/pico_validation/test_trace_viewer.py); tool tests are not hardware timing tests |

## 13. Constraints affecting extensions and unverified behavior

- Multiple instances, cores/owner threads, or a signal bridge would require reconsidering global lookup and CPU-context ownership. Adding a mutex alone does not transfer native contexts to another thread.
- Cancellation/deletion/timeouts would require coordinated lifetime handling of Event waiters, Timer notifications, Scheduler registrations, port frames, and delayed validity. There is no contract that current remove/free operations alone implement this.
- Physical public/internal header placement does not match the logical classification. Header location alone does not establish application-level support.
- Atomicity covers maskable IRQs on the same core. PRIMASK does not guarantee synchronization with NMI, HardFault, or another core.
- Stack guards and saved_psp range checks do not measure sufficient stack headroom. MSP high-water/IRQ nesting and trace overhead remain evaluation items.
- Unverified portions remain for repeated waiters' delayed values in real contexts, same-tick wake FIFO execution, early wake/self-PendSV/P/H replacement and cancellation/empty requests, initial/final SVC IRQ boundaries, and alarm miss/stop/restart. Normal demo PASS does not verify these.

Recorded POSIX normal/fast runs completed 6000 logical ticks. CDC-updated Pico completion of 6000 ticks/PASS is a report of the developer's hardware screen observation, not independent board operation or flashed-binary hash verification by Codex. The recorded cooperative trace has 38 records/8 handoffs/ADGBEHCFI/overwritten=0; its log has no firmware hash, and the executed binary has not been independently identified. Normal runs do not establish completion of all special-path verification. See [README verification status](../README.md#verification-status) and section 19 of the [API Reference](api-reference.md).
