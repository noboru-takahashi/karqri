# Karqri RP2040 validation port

This port runs Karqri on a Raspberry Pi Pico (RP2040), with validation firmware
and a separate six-task track demo. Normal execution paths have recorded hardware
verification results; special interrupt races and boundary cases remain under
additional verification. The implementation and usage constraints are described
below and in the [API Reference](../../docs/api-reference.md).

## Build

The build requires Pico SDK 2.3.0, CMake, Ninja, and the ARM GNU embedded
toolchain. Set `PICO_SDK_PATH` to the SDK checkout, then run from the repository
root:

```sh
cmake -S ports/pico -B build/pico-rp2040 -G Ninja \
    -DPICO_BOARD=pico -DCMAKE_BUILD_TYPE=Release
cmake --build build/pico-rp2040
```

The build produces two independent firmware images:

- `build/pico-rp2040/karqri_pico_validation.uf2`
- `build/pico-rp2040/karqri_pico_track_demo.uf2`

Flash the required UF2 to the Pico. Output is sent through USB CDC. Open the
serial device at any conventional baud setting; USB CDC does not use the
configured baud rate. Validation waits 1.5 seconds before printing. The track
demo instead waits up to 10 seconds for USB CDC connection (including DTR in the
current SDK configuration), then gives a connected terminal 250 ms to prepare.
If no connection arrives it starts after the timeout. No Karqri Tasks or Karqri
hardware tick run during this wait. Connecting after the timeout may still miss
the one-time initial drawing.

## Validation output

The firmware runs separate phases for:

- one-task initial dispatch and task return;
- three-task cooperative switching and independent stacks;
- event block, signal, resume, and delayed return through saved R0;
- task suspend and resume;
- hardware timer IRQ, timer-to-event wakeup, higher-priority preemption, and
  equal-priority timeslice rotation.

Success ends with `KARQRI_PICO_VALIDATION_PASS`. Any failed assertion is printed
as `PICO_FAIL ...`, followed by `KARQRI_PICO_VALIDATION_FAIL`.

The validation also checks stack guards, repeated switch counts, scheduler
emptiness after task return, and overlap in the deliberately protected critical
region. ANSI escape sequences are not used.

## Port and tick lifetime

`started` means RUNNING, not ownership of a hardware alarm. It stays false
through initial SVC and is published by the first task bootstrap only after
checking Thread/PSP, active context and scheduler selection. Task retirement is
serialized; the last task clears RUNNING and cancels PendSV before final SVC.
All registered contexts must have finished for a normal return to main.
Returning under reschedule suppression or with PRIMASK set diagnoses in Release.

Initialize and run the port on one core. Tick start/stop are privileged Thread
operations on that core; IRQ/callback reentry and cross-core access diagnose
before changing registration. Stop also clears pending IRQ state before unclaim.
Keep the tick and port alive until stop returns, including after the port returns
to main. Runtime port wrappers require RUNNING; prepare initial
Event/Timer state with the serialized kernel primitives before starting the port.

An alarm may run before or after RUNNING. Such callbacks only update raw counters
and rearm; they do not access Timer queues, task contexts or Scheduler state.
`irq_count` counts callbacks (including forced IRQs); `kernel_ticks` counts only
RUNNING callbacks and drives validation/demo progress. The first kernel tick uses
the existing alarm phase, without elapsed-time compensation.

A missed callback rearm forces a subsequent IRQ instead of silently losing the
tick. `KARQRI_PICO_TICK_MAX_CONSECUTIVE_MISSES` in `pico_tick.h` defaults to eight:
eight transient misses may recover, while a ninth consecutive miss diagnoses
persistent overload. This is a finite recovery budget, not a measured timing
bound; tune it only with target IRQ/lock latency measurements. Successful rearm
resets the streak. This budget limits consecutive forced-IRQ recovery, not IRQ
load in general, and does not guarantee Thread/PendSV progress.
If the initial arm misses, start detaches the callback, clears peripheral/NVIC
pending state, removes the registration and releases the alarm before returning
false. It does not force an IRQ: SDK 2.3.0 may retain a previous owner's future
target_hi, which can prevent that IRQ from delivering a callback. Invalid
parameters, duplicate registration and unavailable alarms also return false
without a new registration.
Release diagnostics record `karqri_pico_fault_reason` and execute UDF.

## Optional cooperative validation trace

Tracing is off by default. To build the validation with tracing in a separate
directory, use the same SDK/toolchain setup as above:

```sh
cmake -S ports/pico -B build/pico-rp2040-trace -G Ninja \
    -DPICO_BOARD=pico -DCMAKE_BUILD_TYPE=Release -DKARQRI_PICO_TRACE=ON
cmake --build build/pico-rp2040-trace
```

Only `cooperative_three_task` arms the recorder. The track demo always compiles
with tracing disabled. `KARQRI_PICO_TRACE=OFF` removes the buffer and all recording
calls, including the PendSV hook. There is no dynamic allocation or hot-path USB
output. The private `pico_trace.h` interface is validation instrumentation, not a
Kernel/application API. Begin/freeze/dump run on the port's main thread; the port,
contexts and labels must remain alive through dump. Writers use saved/restored
PRIMASK on the same core; NMI/HardFault and other-core recording are unsupported.

The ring holds 256 records of exactly 32 bytes (8192 bytes), plus management and
an old-context scratch pointer. A record contains sequence, raw hardware timer low
word, active-context pointer, Scheduler-current Task pointer, pending-context
pointer, subject, value, event, source and flags. Sources are 0=unknown, 1=yield,
2=Task return. Request records are emitted only for P/H transitions; subject is
the previous pending context. Commit subject is the old context and value is the
request sequence. Task-mark value is the existing ASCII marker. Flags are bit
0=PendSV pending, 1=suppressed, 2=active delayed-return pending, 3=active delayed
result valid, 4=Handler mode. Current is a logical Task selection, not a context.

After port return, main freezes the ring, performs the existing checks, then
prints `PICO_TRACE_BEGIN`, context/Task/name mappings, `PICO_TRACE` records and
`PICO_TRACE_END`. Existing PASS/FAIL checks and the collector are unchanged. Task
names are A/D/G, matching the cooperative entry labels; their per-round marks
are A/B/C, D/E/F, G/H/I. Normal execution records 38 events with marks ADGBEHCFI
and eight PendSV handoffs. The other four scenarios produce no trace records.

Full rings retain the latest 256 records and report `overwritten`. Sequence
overflow stops recording and sets `seq_overflow`. Time is sampled at 1-us
resolution, not sub-us accuracy; equal timestamps are ordered by sequence.
Output `us` is the unsigned low-word offset from begin. `time_valid=0` means the
capture lasted at least 2^32 us (about 71.6 minutes) and those offsets are not a
complete chronology. Overwrite, sequence overflow or missing BEGIN/END makes the
trace incomplete independently of the unchanged validation result.

HANDOFF_COMMIT is sampled after restoring next's R4-R11, PSP and switch count,
before restoring PRIMASK. It observes readiness for exception return, **not**
execution of a next-Task instruction: another IRQ may run first. The hook uses
MSP; EXC_RETURN remains in the handler's saved slot and the C ABI preserves the
restored R4-R11. Empty PendSV takes the existing return path without the hook.
Tracing adds stack use and masked execution time and can move race windows.
Compare trace-on/off firmware; a trace-on run is not proof of trace-off timing.
Faults that prevent main return require SWD RAM recovery instead of USB dump.

## Six-task track demo

The track demo is separate from validation and visually follows the CHARM-II
reconstruction demo. ANSI cursor movement and color draw three nested rectangular
tracks with task pairs A/B, C/D, and E/F. Old task positions are restored from
the fixed track image before the six colored task markers are redrawn.

All six tasks have the same priority and advance through timeslice preemption.
Their initial positions and relative speeds match the reconstruction demo. Task
A updates the terminal animation; it is not assigned a special scheduler
priority. The demo runs at a 25 ms tick for 6000 ticks, approximately 150 seconds.

The shared critical zone is marked with `=`. An event is used as its gate. The
demo uses the existing event and reschedule-suppression boundaries and does not
add a semaphore or another kernel primitive.

The bottom of the animated screen retains compact diagnostics for hardware
ticks, context switches, critical-zone ownership, entries, releases, and waits.
After 6000 ticks, the cursor is restored and a plain-text summary reports the six
execution counters, overlap result, and stack-guard result. This final summary
can be captured even when the terminal does not render ANSI animation.

Expected final lines have this shape:

```text
KARQRI_TRACK result=PASS irq=... ticks=... switches=... critical=...,... waits=... overlap=0 runs=...,...,...,...,...,... moves=...,...,...,...,...,... owner=-1 waiters_empty=YES
KARQRI_TRACK tasks=A-F equal_priority=YES independent_psp=YES guards=intact hardware_preemption=YES
KARQRI_PICO_TRACK_DEMO_PASS
```

During the run, confirm that all six task markers continue moving, tick and
switch counts do not stop, and the critical-zone owner never overlaps. The final
summary must report `overlap=0`, `guards=intact`, and nonzero execution counts for
all six tasks.

### Windows BOOTSEL / CDC execution

Use an ANSI terminal at least 95 columns by 28 rows. Build the track demo UF2
above; do not substitute the validation UF2. Record HEAD, dirty state, build
configuration, SDK/toolchain and UF2 hash, then copy it to Windows, for example:

```sh
sha256sum build/pico-rp2040/karqri_pico_track_demo.uf2
cp build/pico-rp2040/karqri_pico_track_demo.uf2 /mnt/c/KarqriValidation/
```

Connect while holding BOOTSEL and identify the `RPI-RP2` drive. In Windows
PowerShell, replace `E:` with that drive:

```powershell
Get-FileHash C:\KarqriValidation\karqri_pico_track_demo.uf2 -Algorithm SHA256
Copy-Item C:\KarqriValidation\karqri_pico_track_demo.uf2 E:\
```

Promptly open the new CDC COM port in a terminal that interprets ANSI and asserts
DTR; save its session log. Use 115200 and no 1200-baud reset. The terminal must
connect within the 10-second wait to receive the full initial screen. Check
Critical Zone and status labels, six moving markers and advancing counters.
Allow at least 150 seconds plus startup/USB overhead to reach the terminal PASS;
a 240-second observation window is a practical test allowance, not a progress
guarantee. Missing output is not PASS. Preserve fault state for SWD diagnosis if
needed. The existing `tools/pico_validation/capture.py` is for the five-phase
validation protocol and its 30-second deadline; it is not a track-demo collector.

### Reported CDC-updated hardware result

The user reports visual confirmation on RP2040 of the initial Critical Zone and
status display, 6000-tick completion, irq=6000, switches=6027, critical=26,26,
waits=22, overlap=0, owner=-1, waiters_empty=YES, guards=intact and
`KARQRI_PICO_TRACK_DEMO_PASS`.
The user reports this build-time UF2 SHA-256:

```text
722d007b0f738284d28d76c283003cfcdafb4c32b777c1ca827186d354ff671f
```

This is a user screen-observation report. Codex did not operate the board or
independently compare the reported hash with the flashed firmware. No raw log
for this track run has been supplied to the repository. Counts are observations,
not fixed expected values for every run. See the
[verification status and usage cautions](../../docs/api-reference.md#19-verification-status-unverified-behavior-and-usage-cautions)
for the distinction between normal-path results and unverified special cases.
