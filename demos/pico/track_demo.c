#include "track_demo.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico_kernel.h"
#include "pico_port.h"
#include "pico_tick.h"
#define STACK_WORDS 512U
#define GUARD_WORDS 4U
#define GUARD_VALUE UINT32_C(0xa55aa55a)
#define CDC_CONNECT_TIMEOUT_MS 10000U
#define CDC_SETTLE_MS 250U
typedef struct TaskStack {
    uint32_t guard_low[GUARD_WORDS];
    _Alignas(8) uint32_t words[STACK_WORDS];
    uint32_t guard_high[GUARD_WORDS];
} TaskStack;


static struct {
    KarqriPicoPort port;
    KarqriPicoTick tick;
    KarqriPicoTaskContext contexts[TASK_COUNT];
    TaskStack stacks[TASK_COUNT];
} pico;
volatile uint32_t *const track_demo_tick_count = &pico.tick.kernel_ticks;
const char track_demo_title[] = "Karqri Pico six-task track";
const char track_demo_scheduling[] = "RP2040 timer IRQ -> Karqri tick -> PendSV; no same-track pass";
const char track_demo_stacks[] = "A-F: equal priority, independent PSP stacks";

static void initialize_stack(TaskStack *stack)
{
    size_t index;

    for (index = 0U; index < GUARD_WORDS; index++) {
        stack->guard_low[index] = GUARD_VALUE;
        stack->guard_high[index] = GUARD_VALUE;
    }
    for (index = 0U; index < STACK_WORDS; index++) {
        stack->words[index] = UINT32_C(0xcccccccc);
    }
}

static bool stack_guards_intact(void)
{
    size_t task_index;
    size_t guard_index;

    for (task_index = 0U; task_index < TASK_COUNT; task_index++) {
        for (guard_index = 0U; guard_index < GUARD_WORDS; guard_index++) {
            if (pico.stacks[task_index].guard_low[guard_index] != GUARD_VALUE ||
                pico.stacks[task_index].guard_high[guard_index] != GUARD_VALUE) {
                return false;
            }
        }
    }
    return true;
}


void track_demo_wait_gate(DemoTask *mover)
{
    KarqriPicoEventWaitCall call = {
        &pico.port, &mover->gate_waiter, &demo.gate_event,
        EVENT_GATE, KARQRI_EVENT_WAIT_ANY, 0U,
    };
    (void)karqri_pico_event_wait(&call);
}
void track_demo_signal_gate(void)
{
    karqri_pico_event_signal(&pico.port, &demo.gate_event, EVENT_GATE);
}
bool track_demo_suppress(bool suppressed)
{
    return karqri_pico_set_reschedule_suppressed(&pico.port, suppressed);
}
void track_demo_metrics(void)
{
    printf("tick=%-5lu irq=%-5lu switches=%-5lu",
           (unsigned long)pico.tick.kernel_ticks, (unsigned long)pico.tick.irq_count,
           (unsigned long)karqri_pico_switch_count);
}
void track_demo_summary_metrics(void)
{
    printf("irq=%lu ticks=%lu switches=%lu", (unsigned long)pico.tick.irq_count,
           (unsigned long)pico.tick.kernel_ticks, (unsigned long)karqri_pico_switch_count);
}
bool track_demo_backend_valid(void)
{
    size_t index;
    if (pico.port.started || karqri_pico_switch_count == 0U || !stack_guards_intact()) {
        return false;
    }
    for (index = 0U; index < TASK_COUNT; index++) {
        if (!pico.contexts[index].finished) {
            return false;
        }
    }
    return true;
}
void track_demo_properties(void)
{
    printf("KARQRI_TRACK tasks=A-F equal_priority=YES independent_psp=YES "
           "guards=%s hardware_preemption=YES\n", stack_guards_intact() ? "intact" : "broken");
}
static bool initialize_demo(void)
{
    size_t index;
    track_demo_initialize();
    karqri_pico_port_init(&pico.port, &demo.scheduler, 1U);
    for (index = 0U; index < TASK_COUNT; index++) {
        initialize_stack(&pico.stacks[index]);
        if (!karqri_pico_task_context_create(&pico.port, &pico.contexts[index],
                &demo.tasks[index], pico.stacks[index].words, STACK_WORDS)) {
            return false;
        }
    }
    return karqri_pico_tick_start(&pico.tick, &pico.port, &demo.timer_queue, TICK_PERIOD_US);
}

static void wait_for_console(void)
{
    const absolute_time_t deadline = make_timeout_time_ms(CDC_CONNECT_TIMEOUT_MS);

    /* With the SDK's default configuration, connected includes host DTR.
     * This runs before context creation and the Karqri hardware alarm start. */
    while (!stdio_usb_connected()) {
        if (time_reached(deadline)) {
            return; /* Headless operation must still start. */
        }
        sleep_ms(10U);
    }
    sleep_ms(CDC_SETTLE_MS);
}

int main(void)
{
    bool passed = false;

    stdio_init_all();
    wait_for_console();
    printf("KARQRI_PICO_TRACK_DEMO_START\n");

    if (!initialize_demo()) {
        printf("KARQRI_PICO_TRACK_DEMO_INIT_FAIL\n");
    } else {
        track_demo_draw();
        track_demo_render(TASK_COUNT);
        if (karqri_pico_start(&pico.port)) {
            karqri_pico_tick_stop(&pico.tick);
            passed = track_demo_results_valid();
            track_demo_summary(passed);
        } else {
            karqri_pico_tick_stop(&pico.tick);
            printf("\033[?25hKARQRI_PICO_TRACK_DEMO_DISPATCH_FAIL\n");
        }
    }
    printf("KARQRI_PICO_TRACK_DEMO_%s\n", passed ? "PASS" : "FAIL");
    fflush(stdout);
    for (;;) {
        tight_loop_contents();
    }
}
