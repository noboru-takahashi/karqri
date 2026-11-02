# POSIX six-task track demo

Build and run on a POSIX host with a C11 compiler and `ucontext` (including WSL
Ubuntu):

```sh
make posix-track-demo
./build/karqri_posix_track_demo
# Same 6000 logical ticks and workload, without checkpoint sleeps:
./build/karqri_posix_track_demo --fast > build/posix-track.log
```

Use an ANSI terminal at least 95 columns by 28 rows. Only Task A renders the
moving scene. The default run sleeps 25 ms at each checkpoint, taking at least
150 seconds plus rendering/scheduling overhead; it is not a realtime clock.
`--fast` removes only those sleeps, not ticks, movement, Event waits or rendering.

## Shared workload and different clocks

`demos/common/track_demo.c` contains the same six A–F Task entries, track geometry,
initial positions/speeds, no-passing rule, Event gate, critical-zone ownership,
ANSI drawing and result aggregation for both backends. Two Tasks share each of
three tracks. A/B/C/D/E/F have movement delays 2/0/3/1/2/0 respectively. A Task
updates at most once per observed logical tick and releases its critical-zone
ownership before returning.

The Pico adapter continues using its hardware alarm, kernel ticks and PendSV,
with six independent PSP stacks. The POSIX adapter creates exactly six independent
64 KiB `ucontext` stacks on one owner OS thread. There is no driver Task, signal
handler or background tick thread. After a Task loop iteration, that executing
Task's checkpoint advances the logical tick, completes the empty Timer queue's
tick, rotates the executing Task through the existing Scheduler API and then
calls POSIX dispatch. Event wait/signal can also dispatch synchronously.

POSIX is **cooperative / ucontext**, not hardware preemption. All six Tasks share
a total of 6000 logical ticks, rather than receiving 6000 ticks each. The clocks,
wall times, switch counts, move counts and interleavings need not match Pico.
`rotations` counts Scheduler selection changes at checkpoints, not CPU context
switches. Compare progress, gate ownership and clean termination across ports.

## Event gate and lifetime

The gate uses the existing broadcast Event semantics. A waking Task checks
`critical_owner` under reschedule suppression; losing contenders repeat Event
wait. Suppression is restored before waiting or returning. Release clears owner
and signals while suppressed; the POSIX adapter explicitly reselects and
dispatches when suppression is cleared. Suppression is not a lock or an interrupt
mask. All accesses here are synchronous on the owner thread.

On reaching the tick limit, Tasks drain normally, including outstanding gate
waits. Host-side teardown requires every context's `finished` flag, empty Ready,
Suspended and Event-waiter queues, NULL Scheduler current, released owner, no
per-Task critical ownership and suppression false. Only then are all six stacks
destroyed. `run()` return alone is not treated as a join. Construction failure
is fatal: process exit releases any allocated prefix, without destroying live
Ready contexts. An incomplete group similarly refuses manual teardown.

## Result

Exit code 0 and `KARQRI_POSIX_TRACK_DEMO_PASS` require:

- 6000 logical ticks/checkpoints and positive progress/moves for each Task;
- positive critical entries, equal entries/releases, and `overlap=0`;
- all six contexts finished, independent observed local addresses within their
  own native stacks, and the clean teardown conditions above.

`KARQRI_TRACK` prints six `runs` and `moves` counters in A–F order, ownership,
waiter status and backend properties. Overlap counts invalid owner releases and
observed simultaneous critical ownership; it is not a general concurrency proof.
The demo exercises normal gate reuse and shutdown, not all Scheduler/Event
contracts, IRQ races or exception handoff paths. POSIX has no stack guard:
the native-stack address checks do not detect all stack corruption or measure
stack headroom.

## Recorded verification

The current demo was rebuilt with `make -B posix-track-demo` and executed in
both modes by Codex. Saved results report exit 0, 6000 ticks/checkpoints,
critical=25,25, waits=22, overlap=0, owner=-1, waiters_empty=YES and
all_finished=YES. Normal mode took 150.988 seconds; fast mode took 0.016 seconds.
A–F runs were 1088,990,1036,959,1009,918 and moves were
363,454,259,334,337,399 in both runs. These are observations, not required exact
counts for other environments. ANSI control bytes and marker-coordinate changes
were checked; visual terminal rendering was not checked by Codex.

The user separately reports RP2040 completion of the CDC-updated Pico demo.
See the [reported Pico hardware result](../../ports/pico/README.md#reported-cdc-updated-hardware-result)
for the observation and firmware identification limits. Normal demo completion
and the host's seven passing validations do not establish coverage of special
interrupt races or boundary cases; see the
[API verification status](../../docs/api-reference.md#19-verification-status-unverified-behavior-and-usage-cautions).
