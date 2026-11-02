# POSIX port: initial-release execution contract

This port uses `ucontext` for synchronous Task switching. These are port/integration
interfaces; they do not add synchronization or cancellation to the Kernel APIs.
The initial release supports one owner OS thread and one active kernel environment,
with a fixed group of contexts that all finish normally before teardown.

## Owner thread and non-reentry

Initialize, create, run, access and destroy the environment on the same OS thread.
Do not call its port/Kernel APIs, including shared-state reads, from other threads
or signal handlers. The port has no locking, async notification/dispatch bridge,
or thread-affinity enforcement. A mutex around one wrapper would not establish
that native context switching happens on the owner thread.

Callbacks execute synchronously. Event/Timer non-reentry and storage-lifetime
contracts still apply. Do not switch CPU context before an enclosing Event signal
or Timer tick completes. The existing Timer/Event adapter may update Scheduler
selection; `karqri_posix_timer_tick()` dispatches after the whole tick returns.
The Timer integration test drives ticks explicitly from a driver Task; it is not
a signal-driven preemption test. External threads/signals may exist in the process
only without accessing this shared environment through these APIs.

## Normal execution, suppression and host return

Create the group before running it. When a Task waits or self-suspends, leave a
runnable driver/signaler/idle context. Make every waiting/suspended Task runnable
and let it finish before the final executable Task returns. All Tasks may return;
no permanent idle Task is required. Ending the group with unfinished contexts and
later resuming it from the host is not an initial-release guarantee.

Task entry functions must return with `reschedule_suppressed == false`. Do not
block or self-suspend under suppression. It is a bool, not a nesting counter or
interrupt lock. Restore the prior mode after a suppressed operation; when allowing
selection again, the integration caller must explicitly reschedule and arrange
dispatch. Merely clearing the bool does not select a new Task.

`karqri_posix_run()` returning true means a dispatch occurred, not that every
context finished. Currently, entry return removes its Task, reads logical current,
and returns to host if current is NULL. It does not scan remaining contexts. Thus
a last Ready return can leave Event waiters or Suspended contexts; return under
suppression can leave Ready Tasks as well. These are unsupported completion paths,
not successful shutdown or an implicit waiter cancellation. POSIX does not promise
Pico's diagnostic-stop behavior for these violations. Do not interpret a host
return or an empty Ready queue alone as permission to free the environment.

## Destruction and storage lifetime

The supported lifecycle is:

1. Initialize unused Scheduler/port/Task objects and successfully create contexts.
2. Run the fixed group, resolving waits/suspends and allowing all entries to return.
3. On the owner host thread, verify all context `finished` flags and satisfy all
   external references/notification-source obligations before teardown.
4. Destroy each successfully created context. Normal entry return already removed
   its Task from Scheduler. Destroy only releases its trampoline slot and stack.
5. Retire the old port, or reinitialize it only after every context is destroyed.
   Release context/Task storage only after no old port/registry/adapter references
   can access it. Start a new group with fully initialized objects.

`active` denotes allocated native-context resources, not the executing Task;
`finished` denotes normal entry return, not resource release. Destroy does not
remove Scheduler registrations, cancel Event waits, stop Timer notifications,
shrink `context_count` or remove pointers from `port->contexts`. Context objects
must remain valid while those pointers can be scanned. Do not destroy an executing,
Ready, waiting or Suspended context as a way to stop a Task. Do not call destroy
after failed creation on an otherwise uninitialized context. Mid-wait cancellation
and deletion remain outside the initial-release contract.

## Port-local state and verification boundary

`trampoline_slots[]` maps the integer passed to `makecontext()` back to its context.
It is process-local, registered on create and cleared on destroy; normal Task
return alone does not free its slot. Port-owned context pointers, host context,
executing context and running flag serve lookup and CPU switching. These are
necessary in the current implementation, not support for concurrent instances.
Reinitializing a port before destroying its contexts loses its lookup table without
clearing their trampoline slots. Do not do this. Sequential groups after complete
teardown are covered by the existing test structure; concurrent groups are not.

`tests/test_posix_kernel.c` covers cooperative execution, normal Event resume,
explicit Timer-driven wakeup and suspend/resume, followed by host-side destruction.
Cooperative/Event/suspend tests check finished contexts; the Timer test checks its
execution trace, result, empty active timers and Scheduler. These positive tests
do not demonstrate early destruction, unfinished-group host return, suppressed
return, foreign-thread/signal access or concurrent instances. These tests do not
establish support for operations outside the documented usage contract.

See the [API Reference](../../docs/api-reference.md) for interface contracts and
the [API Design Notes](../../docs/api-design.md#7-posix-port-cooperative-contexts-and-a-limited-usage-contract)
for the POSIX execution model and its limitations.
