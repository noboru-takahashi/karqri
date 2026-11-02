#include "track_demo.h"
#include "posix_kernel.h"
#include "posix_port.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STACK_BYTES (64U * 1024U)
static struct {
    KarqriPosixPort port;
    KarqriPosixTaskContext contexts[TASK_COUNT];
    uint32_t ticks;
    uint32_t checkpoints;
    uint32_t rotations;
    bool fast;
} posix;
volatile uint32_t *const track_demo_tick_count = &posix.ticks;
const char track_demo_title[] = "Karqri POSIX six-task track";
const char track_demo_scheduling[] = "cooperative / ucontext; logical tick at Task checkpoint";
const char track_demo_stacks[] = "A-F: equal priority, independent ucontext stacks";

void track_demo_checkpoint(void)
{
    KarqriTask *executing = karqri_posix_executing_task(&posix.port);
    if (executing == NULL || demo.scheduler.reschedule_suppressed) {
        fprintf(stderr, "Invalid demo checkpoint context\n");
        exit(EXIT_FAILURE);
    }
    if (posix.ticks >= TICK_LIMIT) {
        return;
    }
    if (!posix.fast) {
        struct timespec remaining = {0, (long)TICK_PERIOD_US * 1000L};
        while (nanosleep(&remaining, &remaining) != 0) {
            if (errno != EINTR) {
                perror("nanosleep");
                exit(EXIT_FAILURE);
            }
        }
    }
    posix.ticks++;
    posix.checkpoints++;
    /* All Timer callbacks finish before reselection/CPU dispatch. No async tick. */
    karqri_timer_tick(&demo.timer_queue);
    if (karqri_scheduler_rotate_task_timeslice(&demo.scheduler, executing)) {
        posix.rotations++;
    }
    (void)karqri_posix_dispatch(&posix.port);
}

void track_demo_wait_gate(DemoTask *mover)
{
    (void)karqri_posix_wait_event(&posix.port, &mover->gate_waiter,
                                &demo.gate_event, EVENT_GATE, KARQRI_EVENT_WAIT_ANY);
}
void track_demo_signal_gate(void)
{
    karqri_posix_signal_event(&posix.port, &demo.gate_event, EVENT_GATE);
}
bool track_demo_suppress(bool suppressed)
{
    const bool previous = karqri_scheduler_set_reschedule_suppressed(
        &demo.scheduler, suppressed);
    if (!suppressed) {
        (void)karqri_scheduler_reschedule(&demo.scheduler);
        (void)karqri_posix_dispatch(&posix.port);
    }
    return previous;
}
void track_demo_metrics(void)
{
    printf("tick=%-5lu checkpoints=%-5lu rotations=%-5lu",
           (unsigned long)posix.ticks, (unsigned long)posix.checkpoints,
           (unsigned long)posix.rotations);
}
void track_demo_summary_metrics(void)
{
    printf("ticks=%lu checkpoints=%lu rotations=%lu", (unsigned long)posix.ticks,
           (unsigned long)posix.checkpoints, (unsigned long)posix.rotations);
}
static bool all_finished(void)
{
    size_t index;
    for (index = 0U; index < TASK_COUNT; index++) {
        if (!posix.contexts[index].finished) {
            return false;
        }
    }
    return true;
}
bool track_demo_backend_valid(void)
{
    size_t index;
    for (index = 0U; index < TASK_COUNT; index++) {
        const uintptr_t base = (uintptr_t)posix.contexts[index].stack;
        const uintptr_t local = demo.movers[index].local_address;
        if (local < base || local - base >= posix.contexts[index].stack_size) {
            return false;
        }
    }
    return all_finished() && !posix.port.running &&
           posix.port.executing_context == NULL && posix.checkpoints == TICK_LIMIT;
}
void track_demo_properties(void)
{
    printf("KARQRI_TRACK tasks=A-F equal_priority=YES scheduling=cooperative "
           "context_backend=ucontext hardware_preemption=NO all_finished=%s "
           "stack_bytes_per_task=%u pacing=%s\n", all_finished() ? "YES" : "NO",
           STACK_BYTES, posix.fast ? "none" : "25ms-per-checkpoint");
}
int main(int argc, char **argv)
{
    size_t index;
    bool passed;
    if (argc == 2 && strcmp(argv[1], "--fast") == 0) {
        posix.fast = true;
    } else if (argc != 1) {
        fprintf(stderr, "Usage: %s [--fast]\n", argv[0]);
        return EXIT_FAILURE;
    }
    printf("KARQRI_POSIX_TRACK_DEMO_START\n");
    track_demo_initialize();
    karqri_posix_port_init(&posix.port, &demo.scheduler);
    for (index = 0U; index < TASK_COUNT; index++) {
        if (!karqri_posix_task_context_create(&posix.port, &posix.contexts[index],
                                             &demo.tasks[index], STACK_BYTES)) {
            /* Fatal construction failure: no run or destruction of live Ready
             * contexts. Process exit releases the allocated prefix. */
            fprintf(stderr, "KARQRI_POSIX_TRACK_DEMO_INIT_FAIL task=%zu\n", index);
            return EXIT_FAILURE;
        }
    }
    track_demo_draw();
    track_demo_render(TASK_COUNT);
    passed = karqri_posix_run(&posix.port);
    passed = track_demo_results_valid() && passed;
    track_demo_summary(passed);
    /* run() is not a join: check the entire group and external references. */
    if (all_finished() && track_demo_group_quiescent()) {
        for (index = 0U; index < TASK_COUNT; index++) {
            karqri_posix_task_context_destroy(&posix.contexts[index]);
        }
    } else {
        fprintf(stderr, "Unsafe teardown refused; process exit releases resources\n");
        passed = false;
    }
    printf("KARQRI_POSIX_TRACK_DEMO_%s\n", passed ? "PASS" : "FAIL");
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
