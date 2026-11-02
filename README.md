# Karqri

English | [日本語](README-ja.md)

## What is Karqri?

Karqri is a new RTOS I am developing from the design ideas of CHARM-II, the RTOS I developed in 1986 for the MC68000, and the results of restoring and studying it in 2026. Development and verification currently target POSIX environments and the Raspberry Pi Pico (RP2040).

Karqri carries forward parts of the Pico execution infrastructure and demos developed in the 2026 research project, while reorganizing the kernel, APIs, and ports. The same kernel and shared workload run in different environments, making it possible to examine task progress, waiting and wakeup, and context switching.

## Current capabilities

| Feature | Description |
| --- | --- |
| Queue | An intrusive circular doubly linked list: link fields are embedded in each element. Supports FIFO/LIFO operations and priority-ordered insertion. This is not a message queue. |
| Task | Manages the entry function and argument, priority, state bits, and delayed return values received after waiting. |
| Scheduler | Manages runnable tasks by priority, with FIFO ordering among equals. Supports timeslice rotation, suspension, and resumption. |
| Event | A 32-bit notification bitset. Wait for any or all requested bits. Notifications do not accumulate counts. |
| Timer | Manages a fixed pool supplied by the caller and invokes a one-shot callback after the specified number of ticks. Can also deliver Event notifications. |
| POSIX port | Cooperative task execution using `ucontext` on a single OS thread. |
| Pico port | Independent PSP stacks, SVC/PendSV, and hardware alarms for interrupt-driven task switching. |

### Six-task track demo

The POSIX and Pico versions share the C workload in `demos/common/`. Tasks A–F move around three tracks, using Event notifications and ownership checks to coordinate access to a shared critical zone. An ANSI terminal displays task positions, ticks, and shared-region status, followed by a summary at completion.

### Validation traces and timeline display

Pico validation firmware has optional tracing limited to the cooperative `cooperative_three_task` scenario. A Windows collector saves USB CDC output, and an offline Trace Viewer generates an HTML timeline.

This is not a general-purpose visualization system covering every task and interrupt. A context-handoff record does not guarantee that the next task has executed an instruction. Recording also affects execution time and interrupt timing. See the [trace and capture instructions](tools/pico_validation/README.md).

## Build and run

Run the following commands from the repository root unless stated otherwise.

### POSIX: tests and demo

Requirements are a C11 compiler, Make, and a POSIX host with `ucontext` support. Existing tests and demo runs used WSL Ubuntu. This is not a native Windows port, nor a blanket guarantee of operation on other hosts.

```sh
# Seven validations: queue, task, event, timer, timer/event linkage,
# scheduler, and POSIX integration
make test

# Build the standalone six-task demo
make posix-track-demo

# Run in an ANSI terminal
./build/karqri_posix_track_demo
```

Use a terminal at least 95 columns by 28 rows. Normal mode sleeps for 25 ms at each checkpoint in the executing task and advances to a total of 6000 logical ticks. Runtime is about 150 seconds plus rendering and scheduling overhead. Fast mode omits the sleeps:

```sh
./build/karqri_posix_track_demo --fast
```

Successful completion requires exit code 0 and this final line:

```text
KARQRI_POSIX_TRACK_DEMO_PASS
```

The summary reports progress for all tasks A–F, no overlapping critical-zone ownership (`overlap=0`), matching acquisition/release counts, `owner=-1`, `waiters_empty=YES`, and `all_finished=YES`, among other checks. POSIX has no stack guards; it checks that each task's observed local variable lies within its own native stack.

The POSIX demo switches tasks cooperatively at checkpoints. Its logical clock is not temporally equivalent to Pico's hardware ticks. Compare progress, critical-zone handling, and normal completion rather than expecting identical switch or movement counts. See the [POSIX demo instructions](demos/posix/README.md).

### Pico: SDK and toolchain setup

Existing builds used **Pico SDK 2.3.0**. CMake, Ninja, the ARM GNU embedded toolchain (C/C++ compilers and associated runtime), and Git for SDK setup are also required. The target is the RP2040-based Raspberry Pi Pico; this does not indicate Pico 2 support.

Prepare a Pico SDK 2.3.0 checkout and set `PICO_SDK_PATH` to its location. Replace `/path/to/pico-sdk` below with your SDK's absolute path.

```sh
export PICO_SDK_PATH="/path/to/pico-sdk"

# Initialize SDK submodules, including TinyUSB for USB CDC.
# Downloading or updating them may require network access.
git -C "$PICO_SDK_PATH" submodule update --init --recursive
```

The SDK is not bundled in this repository. The SDK also searches for picotool, used to generate UF2 files, and has a path that downloads and builds it from source if not found. Prepare a compatible picotool in advance for offline builds. End-to-end SDK/toolchain/picotool installation on a new machine still needs verification separately from successful builds in an existing environment.

`ports/pico/CMakeLists.txt` reads this environment variable. Alternatively, pass `-DPICO_SDK_PATH=...` to CMake.

### Pico: Release build

```sh
cmake -S ports/pico -B build/pico-rp2040 -G Ninja \
    -DPICO_BOARD=pico -DCMAKE_BUILD_TYPE=Release
cmake --build build/pico-rp2040
```

This produces two independent firmware images:

| File | Purpose |
| --- | --- |
| `build/pico-rp2040/karqri_pico_validation.uf2` | Five scenarios covering initial dispatch, cooperative switching, delayed Event return values, suspend/resume, and interrupt-driven task switching. |
| `build/pico-rp2040/karqri_pico_track_demo.uf2` | Six-task ANSI track demo. |

### Pico: flash and run

1. Connect the Pico over USB while holding BOOTSEL; release it when the `RPI-RP2` drive appears.
2. Copy the required UF2 onto that drive. Use `karqri_pico_track_demo.uf2` for the six-task demo.
3. Open the USB CDC port that appears after reboot in a terminal supporting ANSI and DTR. A baud setting of 115200 is suitable. Do not use a 1200-baud reset. UART output is disabled in the current build.
4. The demo waits up to 10 seconds for CDC connection, then another 250 ms after connecting. Karqri tasks and hardware ticks do not start during this wait. It starts after the timeout even without a connection, so a late connection can miss the fixed initial display.
5. In a terminal at least 95 columns by 28 rows, check the `Critical Zone` heading, six tasks, and advancing tick/switch counts. The demo runs for 6000 ticks at a 25 ms period: about 150 seconds plus startup and USB overhead.

At completion, check `overlap=0`, matching critical-zone acquisition/release counts, `owner=-1`, `waiters_empty=YES`, `guards=intact`, progress for every task, and the following final line. No output or an interrupted run is not PASS.

```text
KARQRI_PICO_TRACK_DEMO_PASS
```

The validation firmware's success marker is `KARQRI_PICO_VALIDATION_PASS`. Unlike the demo, it waits a fixed 1.5 seconds after startup rather than waiting for a connection, so late capture may miss START. For Windows capture, arm the collector before flashing the UF2. The collector is for the five validation scenarios and has a default 30-second deadline; it cannot collect the roughly 150-second track demo.

See the [Pico instructions](ports/pico/README.md) for UF2 transfer between Windows and WSL and detailed operation, and the [Windows tools instructions](tools/pico_validation/README.md) for capture and timeline display. When recording results, associate the source revision and uncommitted changes, build settings, SDK/toolchain, UF2/ELF hashes, board, and logs.

### Optional: cooperative-scenario tracing

```sh
cmake -S ports/pico -B build/pico-rp2040-trace -G Ninja \
    -DPICO_BOARD=pico -DCMAKE_BUILD_TYPE=Release -DKARQRI_PICO_TRACE=ON
cmake --build build/pico-rp2040-trace
```

The track demo remains untraced with this setting. Only the validation firmware's cooperative three-task scenario is recorded. The fixed ring buffer holds 256 records of 32 bytes each and detects overwrites and sequence-number overflow.

The collector and Trace Viewer use Python 3.10 or later. Only live Windows COM capture requires `pyserial==3.5`; the Viewer and offline tests use the standard library. Timelines use firmware-recorded timestamps, not Windows receipt times as task execution times. Validation PASS/FAIL and Trace COMPLETE/INCOMPLETE are separate judgments.

## Design and limitations

The kernel handles logical task state, queues, notifications, and timers. Ports handle stacks, CPU contexts, and actual switching. Queue/Task/Event/Timer are application primitives; Scheduler and linkage adapters are internal/integration APIs.

- Shared kernel primitives have no internal synchronization. The caller or port must serialize entire operations, including reads and callbacks. Direct use on Pico also requires synchronization with the tick IRQ.
- POSIX targets a single owner thread; Pico targets one runtime on the same core. POSIX API access from foreign threads or signal handlers and concurrent kernel instances are outside the supported scope. Pico's interrupt protection does not synchronize with another core, NMI, or HardFault.
- A task entering a wait or suspending itself must leave another runnable context. Do not block, self-suspend, or return normally from a task entry while rescheduling is suppressed. Reschedule suppression is not an interrupt lock.
- Destroy contexts or storage only after all tasks complete normally and notification sources and external references have ended. A true result from POSIX `run()` alone does not establish all-task completion. Destroying a live context or resuming an unfinished group is outside the supported scope.
- The current kernel does not provide a standard memory allocator, RTOS message send/receive services, Event wait timeouts or mid-wait cancellation, forced task deletion, or periodic timers.

See the [API Reference](docs/api-reference.md), [API Design Notes](docs/api-design.md), and [POSIX execution contract](ports/posix/README.md) for details such as state-bit interpretation, interrupt-mask preservation/restoration, and callback reentry requirements.

### Verification status

Previous test runs have confirmed the following:

- All seven host validations PASS.
- POSIX demo normal and fast modes reach 6000 logical ticks and complete normally.
- All five normal Pico validation scenarios PASS on hardware.
- The Pico six-task demo displays its initial screen, completes 6000 ticks, and reports PASS, as observed on the hardware screen.

Special interrupt races, startup/shutdown boundaries, stack usage, and related conditions remain under additional verification. Normal-path success does not establish safety under every possible interrupt timing. See section 19 of the [API Reference](docs/api-reference.md) for verification scope and unverified cases, and the [API Design Notes](docs/api-design.md) for implementation constraints.

## Name and history

From the early stages of thinking about a new RTOS, I wanted to visualize its inner workings. Could tasks, notifications, and timers be shown as a chain of mechanisms, like the contraptions in *PythagoraSwitch*, a Japanese educational television program? That idea led me to the Japanese word *karakuri*: mechanical contraptions or ingenious mechanisms, often with a sense of play.

Searching for `karakuri` returned many existing results, so I chose the distinctive spelling `Karqri`. The name does not mean a general chain-reaction animation system is already complete. The current ANSI demo and limited tracing are a starting point for exploring that direction. See the [project history](docs/history.md) for more.

For more background on the CHARM-II restoration and research,
see my [1980s Real-Time OS series on Medium](https://medium.com/@noborutakahashi/series-1980s-real-time-os-fd24fc389274)
and my [Japanese article on note](https://note.com/noborutkhs/n/n0f07002d2c4f).

## Documentation

| Topic | Japanese | English |
| --- | --- | --- |
| History and development ideas | [history-ja.md](docs/history-ja.md) | [history.md](docs/history.md) |
| API Reference | [api-reference-ja.md](docs/api-reference-ja.md) | [api-reference.md](docs/api-reference.md) |
| API Design Notes | [api-design-ja.md](docs/api-design-ja.md) | [api-design.md](docs/api-design.md) |

The English API documents are authoritative; Japanese versions are translations. Detailed build and execution instructions are available in English:

- [POSIX demo](demos/posix/README.md)
- [POSIX port contract](ports/posix/README.md)
- [Pico build, validation, and demo](ports/pico/README.md)
- [Windows CDC collector and Trace Viewer](tools/pico_validation/README.md)

## License

Karqri's own source and documentation are under the [MIT License](LICENSE). Copyright (c) 2026 Noboru Takahashi. Use, modification, and redistribution are subject to its conditions, including preservation of the copyright and permission notices.

External dependencies retain their own licenses. Karqri's MIT License does not relicense the Pico SDK, TinyUSB, libc, Python, pyserial, or the separate CHARM-II restoration materials. The SDK and those runtimes/packages are not intended to be bundled in this source tree.

See [THIRD_PARTY.md](THIRD_PARTY.md) for dependencies and outstanding checks. Source publication and distribution of binaries such as UF2/ELF files are separate matters. Binary distribution requires checking the licenses and required notices of the components actually included. pyserial 3.5 has been confirmed as BSD-3-Clause; see THIRD_PARTY.md for the source archive and wheel license-text findings. Future bundled distributions still require review of the components actually distributed and their required notices. Source provenance and rights ownership are separate questions; these dependency checks do not resolve them.
