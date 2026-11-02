#include "karqri/task.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct EntryArgument {
    uint32_t calls;
} EntryArgument;

static void fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    exit(EXIT_FAILURE);
}

static void expect(bool condition, const char *message)
{
    if (!condition) {
        fail(message);
    }
}

static void test_entry(void *argument)
{
    EntryArgument *entry_argument = argument;

    entry_argument->calls++;
}

static void test_initialization(void)
{
    KarqriTask task;
    EntryArgument argument = {0U};

    karqri_task_init(&task, test_entry, &argument, 17U);

    expect(task.entry == test_entry && task.entry_argument == &argument,
           "task entry or argument was not retained");
    task.entry(task.entry_argument);
    expect(argument.calls == 1U, "task entry boundary passed wrong argument");
    expect(karqri_task_priority(&task) == 17U,
           "initial priority mismatch");
    expect(karqri_task_state(&task) == KARQRI_TASK_STATE_READY,
           "initial task state is not READY");
    expect(!karqri_task_is_waiting(&task),
           "new task is unexpectedly waiting");
    expect(!karqri_task_has_delayed_return(&task),
           "new task has a delayed return value");
    expect(task.queue_node.next == &task.queue_node &&
               task.queue_node.previous == &task.queue_node,
           "new task queue node is not detached");
}

static void test_state_bit_operations(void)
{
    KarqriTask task;

    karqri_task_init(&task, test_entry, NULL, 1U);
    karqri_task_state_replace(
        &task,
        KARQRI_TASK_STATE_SEND_WAIT | KARQRI_TASK_STATE_SUSPENDED);
    expect(karqri_task_state_has_any(
               &task, KARQRI_TASK_STATE_SEND_WAIT) &&
               karqri_task_state_has_all(
                   &task,
                   KARQRI_TASK_STATE_SEND_WAIT |
                       KARQRI_TASK_STATE_SUSPENDED),
           "state query did not preserve combined bits");
    expect(karqri_task_is_waiting(&task),
           "SEND_WAIT was not recognized as a wait state");

    karqri_task_state_clear(&task, KARQRI_TASK_STATE_SEND_WAIT);
    karqri_task_state_set(&task, KARQRI_TASK_STATE_RECEIVE_WAIT);
    expect(karqri_task_state(&task) ==
               (KARQRI_TASK_STATE_RECEIVE_WAIT |
                KARQRI_TASK_STATE_SUSPENDED),
           "state set/clear changed unrelated state bits");
}

static void test_suspend_and_resume(void)
{
    KarqriTask task;

    karqri_task_init(&task, test_entry, NULL, 1U);
    expect(karqri_task_suspend(&task), "READY task suspend failed");
    expect(karqri_task_state(&task) == KARQRI_TASK_STATE_SUSPENDED,
           "suspend did not clear READY and set SUSPENDED");
    expect(!karqri_task_suspend(&task),
           "duplicate suspend did not report failure");

    expect(karqri_task_resume(&task), "suspended task resume failed");
    expect(karqri_task_state(&task) == KARQRI_TASK_STATE_READY,
           "resume did not restore logical READY state");
    expect(!karqri_task_resume(&task),
           "resume of a non-suspended task did not report failure");
}

static void test_wait_and_suspend_combination(void)
{
    KarqriTask task;

    karqri_task_init(&task, test_entry, NULL, 1U);
    karqri_task_state_replace(&task, KARQRI_TASK_STATE_RECEIVE_WAIT);
    expect(karqri_task_suspend(&task),
           "waiting task could not be suspended");
    expect(karqri_task_state(&task) ==
               (KARQRI_TASK_STATE_RECEIVE_WAIT |
                KARQRI_TASK_STATE_SUSPENDED),
           "wait and suspend state did not coexist");

    expect(karqri_task_resume(&task),
           "waiting suspended task could not be resumed");
    expect(karqri_task_state(&task) ==
               KARQRI_TASK_STATE_RECEIVE_WAIT,
           "resume incorrectly made a still-waiting task READY");
}

static void test_priority_and_delayed_return(void)
{
    KarqriTask first;
    KarqriTask second;
    const uintptr_t pointer_width_value = UINTPTR_MAX - (uintptr_t)7U;

    karqri_task_init(&first, test_entry, NULL, 10U);
    karqri_task_init(&second, test_entry, NULL, 20U);
    expect(karqri_task_set_priority(&first, 30U) == 10U &&
               karqri_task_priority(&first) == 30U,
           "priority update did not return and retain the expected values");
    expect(karqri_task_priority(&second) == 20U,
           "first task priority update changed second task");

    karqri_task_set_delayed_return(&first, pointer_width_value);
    expect(karqri_task_has_delayed_return(&first) &&
               karqri_task_delayed_return(&first) == pointer_width_value,
           "pointer-width delayed return value was not retained");
    expect(!karqri_task_has_delayed_return(&second),
           "delayed return value leaked into another task");
    karqri_task_clear_delayed_return(&first);
    expect(!karqri_task_has_delayed_return(&first),
           "delayed return ready flag was not cleared");
}

static void test_queue_node_integrity(void)
{
    KarqriQueueNode queue;
    KarqriTask first;
    KarqriTask second;

    karqri_queue_init(&queue);
    karqri_task_init(&first, test_entry, NULL, 1U);
    karqri_task_init(&second, test_entry, NULL, 2U);
    karqri_queue_push_back(&queue, &first.queue_node);
    karqri_queue_push_back(&queue, &second.queue_node);
    expect(queue.next == &first.queue_node &&
               first.queue_node.next == &second.queue_node &&
               queue.previous == &second.queue_node &&
               second.queue_node.previous == &first.queue_node,
           "task queue nodes were not linked explicitly");

    karqri_queue_remove(&first.queue_node);
    expect(queue.next == &second.queue_node &&
               second.queue_node.previous == &queue &&
               first.queue_node.next == &first.queue_node &&
               first.queue_node.previous == &first.queue_node,
           "task queue node removal broke queue integrity");
    expect(karqri_task_state(&first) == KARQRI_TASK_STATE_READY &&
               karqri_task_priority(&first) == 1U,
           "queue operation changed task control fields");
}

static void test_priority_boundaries(void)
{
    KarqriTask task;

    karqri_task_init(&task, test_entry, NULL, 0U);
    expect(karqri_task_priority(&task) == 0U,
           "zero initial priority was not retained");
    expect(karqri_task_set_priority(&task, UINT32_MAX) == 0U &&
               karqri_task_priority(&task) == UINT32_MAX,
           "maximum priority update failed");
    expect(karqri_task_set_priority(&task, 0U) == UINT32_MAX &&
               karqri_task_priority(&task) == 0U,
           "zero priority update failed");
}

static void test_multiple_wait_bits(void)
{
    KarqriTask task;
    const KarqriTaskState waits =
        KARQRI_TASK_STATE_EVENT_WAIT | KARQRI_TASK_STATE_RECEIVE_WAIT |
        KARQRI_TASK_STATE_SEND_WAIT;

    karqri_task_init(&task, test_entry, NULL, 1U);
    karqri_task_state_replace(&task, waits | KARQRI_TASK_STATE_SUSPENDED);
    expect(karqri_task_is_waiting(&task) &&
               karqri_task_state_has_all(&task, waits),
           "combined wait bits were not recognized");
    karqri_task_state_clear(&task, KARQRI_TASK_STATE_EVENT_WAIT);
    expect(karqri_task_is_waiting(&task) &&
               karqri_task_state_has_any(&task, waits) &&
               !karqri_task_state_has_all(&task, waits),
           "partial clear lost the remaining waits");
    karqri_task_state_clear(&task, KARQRI_TASK_STATE_RECEIVE_WAIT);
    expect(karqri_task_is_waiting(&task) &&
               karqri_task_state(&task) ==
                   (KARQRI_TASK_STATE_SEND_WAIT | KARQRI_TASK_STATE_SUSPENDED),
           "last remaining wait or unrelated state bit was lost");
    karqri_task_state_clear(&task, KARQRI_TASK_STATE_SEND_WAIT);
    expect(!karqri_task_is_waiting(&task) &&
               karqri_task_state(&task) == KARQRI_TASK_STATE_SUSPENDED,
           "clearing all waits changed unrelated state bits");
}

static void test_delayed_return_lifecycle(void)
{
    KarqriTask task;

    karqri_task_init(&task, test_entry, NULL, 1U);
    karqri_task_set_delayed_return(&task, 0U);
    expect(karqri_task_has_delayed_return(&task) &&
               karqri_task_delayed_return(&task) == 0U,
           "zero result was not valid");
    expect(karqri_task_has_delayed_return(&task),
           "reading consumed the result");
    karqri_task_set_delayed_return(&task, (uintptr_t)42U);
    expect(karqri_task_has_delayed_return(&task) &&
               karqri_task_delayed_return(&task) == (uintptr_t)42U,
           "result overwrite failed");
    karqri_task_clear_delayed_return(&task);
    expect(!karqri_task_has_delayed_return(&task) &&
               karqri_task_delayed_return(&task) == (uintptr_t)42U,
           "clear did not invalidate the flag while retaining the value");
    karqri_task_set_delayed_return(&task, (uintptr_t)7U);
    expect(karqri_task_has_delayed_return(&task) &&
               karqri_task_delayed_return(&task) == (uintptr_t)7U,
           "result was not reusable after clear");
}

int main(void)
{
    test_initialization();
    test_state_bit_operations();
    test_suspend_and_resume();
    test_wait_and_suspend_combination();
    test_priority_and_delayed_return();
    test_queue_node_integrity();
    test_priority_boundaries();
    test_multiple_wait_bits();
    test_delayed_return_lifecycle();
    puts("task validation: PASS");
    return EXIT_SUCCESS;
}
