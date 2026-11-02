# From CHARM-II to Karqri — Making RTOS Behavior Visible

Karqri began with CHARM-II, an RTOS I helped develop in 1986.

Nearly 40 years later, in 2026, I started revisiting the surviving source code and documentation to investigate how the original system worked in a modern environment. At first, I intended to study the old implementation. But as I experimented with POSIX and the Raspberry Pi Pico, the work gradually evolved into developing a new RTOS. That became Karqri.

Karqri is not a direct port of CHARM-II. The design ideas from 1986, the restoration and research carried out in 2026, and newly developed implementations are connected, but they represent distinct stages of the project.

## 1. Developing CHARM-II in 1986

In 1986, I worked on CHARM-II, an RTOS targeting the Motorola MC68000 family. I used a SUN-2 workstation and Whitesmiths C for development and testing. The SUN-2 had a 68010 processor, which differed from the target 68000 in some respects but shared the same general instruction set architecture.

CHARM-II provided priority-based task selection, ready queues, event-based waiting and wakeup, timers, and related mechanisms. One characteristic of its design was representing task states as combinations of bits. A task could, for example, be waiting for an event and suspended at the same time.

At the time, I also referred to the specifications of VRTX and pSOS. I cannot, however, recall every design decision or its precise rationale. I want to distinguish what can be established from the surviving code from what I remember today.

Much of the implementation depended on the CPU. Context switching involved the 68000-family registers, exception stack frames, and exception-return mechanisms. Task management written in C worked together with CPU-specific routines written in assembly.

I also made a demo in which multiple tasks moved around tracks. Its original source code no longer survives, but I remember displaying the tasks' movements on screen. That memory would become the starting point for an experiment 40 years later.

## 2. Returning to the Source Code 40 Years Later

In 2026, I decided to revisit the surviving source listings and related CHARM-II documentation. I brought code recovered from paper into a modern development environment and began checking its behavior.

AI proved extremely useful. OCR, code analysis, proposed fixes, and test creation made it possible to work through tasks in short iterations that would have been laborious 40 years earlier.

At the same time, I learned not to trust OCR or AI output without verification. Errors were not limited to misread symbols or types; sometimes the meaning of the original code was changed. Successful compilation did not prove that I had accurately restored the 1986 implementation. Comparing the results with the original materials and testing small pieces of behavior were essential.

Working with old C code in modern compilers brought its own difficulties. Pointer widths, calling conventions, and stack layouts had been tied to the CPUs and tools of the time, and could not simply be assumed to work unchanged.

I therefore made a conscious distinction between restoring and studying the original source, and reproducing the behavior I understood in a modern environment.

## 3. From Restoration to a New RTOS

In the 2026 research project, I first tested queues, scheduling, task waiting, and wakeup in a POSIX environment. I also recreated a demo of six tasks moving around tracks, using my memories of the demo I had made in 1986 as a starting point. It was not a faithful reconstruction of the original demo's code or screen display; it was a new implementation of the idea as I remembered it.

I then extended the experiments to the Raspberry Pi Pico. Because its RP2040 uses ARM Cortex-M0+ cores, the 68000 assembly could not be reused directly. I designed new context-switching routines around Cortex-M mechanisms such as SVC, PendSV, and exception frames. I worked through the design with ChatGPT and used Codex to implement it.

As these experiments progressed, investigating historical CHARM-II and designing a practical modern RTOS gradually became separate challenges. In the latter half of 2026, I therefore started Karqri as a new codebase.

In Karqri, I separated the kernel's logical behavior from the ports that depend on a particular CPU or execution environment. A shared workload can run on both POSIX and Pico, allowing me to compare task progress, waiting and wakeup, and access to shared regions. I also reorganized APIs such as Queue, Task, Scheduler, Event, and Timer for modern use.

This was neither a simple port of the 1986 code to another CPU nor a completely independent rewrite unrelated to the 2026 research implementation. Karqri carries forward specific code and knowledge from that research, including parts of the Pico execution infrastructure and the demo, while adapting them to its own architecture.

I have not insisted on preserving every historical design choice. For example, Karqri uses FIFO ordering for ready tasks of equal priority. Nor am I trying to reimplement every service from the old RTOS: the current Karqri kernel does not provide a standard memory allocator. Deciding what to carry forward and what to change is part of the project.

## 4. Why I Named It Karqri

From the early stages of thinking about a new RTOS, I wanted to find a more visual way to express what happens inside it.

A task runs, waits for something, and resumes when another task sends a notification. A timer expires, and the scheduler selects the next task. I wanted to make these chains of events understandable by watching them, rather than only reading logs or numbers.

Another image I had in mind was the Japanese educational television program *PythagoraSwitch*, known for its playful chain-reaction contraptions: one action triggers the next, which sets another mechanism in motion. Could I show what happens inside an RTOS in a similar way?

That led me to the Japanese word *karakuri*, which can refer to mechanical contraptions, ingenious mechanisms, and playful automata. It captured both the machinery and the sense of play I had in mind.

But searching for `karakuri` brought up a large number of existing sites. So I altered the spelling slightly and chose `Karqri`.

The name comes less from a particular RTOS feature than from **how I want to make its behavior visible**.

## 5. What I Want to Explore Next

Karqri currently has POSIX and Pico execution environments, a six-task ANSI demo, and Pico validation traces that can be recorded and displayed as a timeline. These tools are beginning to let me observe task execution and parts of the context-switching process.

That does not mean the current demo already realizes the *PythagoraSwitch*-like visualization I have in mind. For now, I am building the basic RTOS mechanisms and the means to test and observe them.

Likewise, successful runs of ordinary demos and hardware tests do not establish safety under every possible interrupt timing. Normal Pico execution paths have been tested on hardware, but unusual interrupt interleavings and race conditions still need further verification.

In the future, I want to show not only that a task ran, but **why it waited, what caused it to resume, and how one mechanism set the next in motion**. I am also interested in comparing different CPUs and execution models using the same workload.

What began as revisiting the CHARM-II I worked on in 1986 has gradually grown into an effort to build a new RTOS and find ways to express its inner workings. Karqri is a project still in the middle of that journey.

---

For details of the current APIs, see the [API Reference](api-reference.md). For architectural boundaries and constraints, see the [API Design Notes](api-design.md).
