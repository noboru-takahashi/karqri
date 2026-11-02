#include "track_demo.h"
#include <stdio.h>

TrackDemo demo;
static int displayed_x[TASK_COUNT];
static int displayed_y[TASK_COUNT];
static bool displayed_positions_valid;

static const DemoTask initial_movers[TASK_COUNT] = {
    {.label = 'A', .track = 0, .position = 48, .speed = 2},
    {.label = 'B', .track = 0, .position = 56, .speed = 0},
    {.label = 'C', .track = 1, .position = 44, .speed = 3},
    {.label = 'D', .track = 1, .position = 52, .speed = 1},
    {.label = 'E', .track = 2, .position = 40, .speed = 2},
    {.label = 'F', .track = 2, .position = 44, .speed = 0},
};

static const char *task_colors[TASK_COUNT] = {
    "\033[31m", "\033[32m", "\033[33m",
    "\033[34m", "\033[35m", "\033[36m",
};

static int track_top(int track)
{
    return 4 + track * 2;
}

static int track_left(int track)
{
    return 2 + track * 2;
}

static int track_right(int track)
{
    return WIDTH - 4 - track * 2;
}

static int track_bottom(int track)
{
    return HEIGHT - 8 - track * 2;
}

static int track_length(int track)
{
    const int width = track_right(track) - track_left(track);
    const int height = track_bottom(track) - track_top(track);

    return 2 * (width + height);
}

static int critical_left(void)
{
    return ((track_left(0) + track_right(0) - CRITICAL_WIDTH) / 2) + 1;
}

static int critical_right(void)
{
    return critical_left() + CRITICAL_WIDTH - 1;
}

static void position_to_xy(int track, int position, int *x, int *y)
{
    const int left = track_left(track);
    const int right = track_right(track);
    const int top = track_top(track);
    const int bottom = track_bottom(track);
    const int width = right - left;
    const int height = bottom - top;
    const int length = track_length(track);

    position %= length;
    if (position < width) {
        *x = right - position;
        *y = top;
    } else if (position < width + height) {
        *x = left;
        *y = top + position - width;
    } else if (position < width + height + width) {
        *x = left + position - width - height;
        *y = bottom;
    } else {
        *x = right;
        *y = bottom - (position - width - height - width);
    }
}

static bool position_is_critical(int track, int position)
{
    int x;
    int y;

    position_to_xy(track, position, &x, &y);
    return y >= CRITICAL_TOP_ROW && y < CRITICAL_BOTTOM_ROW &&
           x >= critical_left() && x <= critical_right();
}

static void move_cursor(int row, int column)
{
    printf("\033[%d;%dH", row, column);
}

static char background_at(int row, int column)
{
    char result = ' ';
    int track;

    if ((row == CRITICAL_TOP_ROW || row == CRITICAL_BOTTOM_ROW) &&
        column >= critical_left() && column <= critical_right()) {
        result = '=';
    }
    for (track = 0; track < (int)TRACK_COUNT; track++) {
        const int left = track_left(track);
        const int right = track_right(track);
        const int top = track_top(track);
        const int bottom = track_bottom(track);

        if ((row == top || row == bottom) &&
            column >= left && column <= right) {
            result = '-';
        }
        if ((column == left || column == right) &&
            row >= top && row <= bottom) {
            result = '|';
        }
        if (row == top && column == left + 1) {
            result = '<';
        }
        if (row == bottom && column == right - 1) {
            result = '>';
        }
    }
    return result;
}

static void draw_track(int track)
{
    const int left = track_left(track);
    const int right = track_right(track);
    const int top = track_top(track);
    const int bottom = track_bottom(track);
    int coordinate;

    for (coordinate = left; coordinate <= right; coordinate++) {
        move_cursor(top, coordinate);
        putchar('-');
        move_cursor(bottom, coordinate);
        putchar('-');
    }
    for (coordinate = top; coordinate <= bottom; coordinate++) {
        move_cursor(coordinate, left);
        putchar('|');
        move_cursor(coordinate, right);
        putchar('|');
    }
    move_cursor(top, left + 1);
    putchar('<');
    move_cursor(bottom, right - 1);
    putchar('>');
}

void track_demo_draw(void)
{
    int track;
    int x;

    printf("\033[2J\033[H\033[?25l");
    move_cursor(1, 2);
    printf("%s", track_demo_title);
    move_cursor(2, critical_left());
    printf("Critical Zone");
    for (x = critical_left(); x <= critical_right(); x++) {
        move_cursor(CRITICAL_TOP_ROW, x);
        putchar('=');
        move_cursor(CRITICAL_BOTTOM_ROW, x);
        putchar('=');
    }
    for (track = 0; track < (int)TRACK_COUNT; track++) {
        draw_track(track);
    }
    move_cursor(20, 2);
    printf("critical_owner:");
    move_cursor(21, 2);
    printf("critical_gate: Karqri event wait/signal");
    move_cursor(22, 2);
    printf("entries:          releases:          waits:");
    move_cursor(23, 2);
    printf("%s", track_demo_scheduling);
    move_cursor(24, 2);
    printf("%s", track_demo_stacks);
}

void track_demo_render(size_t running_index)
{
    size_t index;

    if (displayed_positions_valid) {
        for (index = 0U; index < TASK_COUNT; index++) {
            move_cursor(displayed_y[index], displayed_x[index]);
            putchar(background_at(displayed_y[index], displayed_x[index]));
        }
    }

    for (index = 0U; index < TASK_COUNT; index++) {
        int x;
        int y;

        position_to_xy(
            demo.movers[index].track,
            demo.movers[index].position,
            &x,
            &y);
        demo.movers[index].x = x;
        demo.movers[index].y = y;
        move_cursor(y, x);
        if (index == running_index) {
            printf("\033[1m%s%c\033[0m", task_colors[index],
                   demo.movers[index].label);
        } else {
            printf("%s%c\033[0m", task_colors[index],
                   demo.movers[index].label);
        }
        displayed_x[index] = x;
        displayed_y[index] = y;
    }
    displayed_positions_valid = true;

    move_cursor(1, 29);
    track_demo_metrics();
    move_cursor(20, 18);
    if (demo.critical_owner < 0) {
        printf("none ");
    } else {
        const size_t owner = (size_t)demo.critical_owner;

        printf("%s%c\033[0m    ", task_colors[owner],
               demo.movers[owner].label);
    }
    move_cursor(22, 11);
    printf("%-8lu", (unsigned long)demo.critical_entries);
    move_cursor(22, 31);
    printf("%-8lu", (unsigned long)demo.critical_releases);
    move_cursor(22, 48);
    printf("%-8lu", (unsigned long)demo.critical_waits);
    fflush(stdout);
}

static bool front_position_is_occupied(size_t self_index, int next_position)
{
    const DemoTask *self = &demo.movers[self_index];
    size_t index;

    for (index = 0U; index < TASK_COUNT; index++) {
        if (index != self_index &&
            demo.movers[index].track == self->track &&
            demo.movers[index].position == next_position) {
            return true;
        }
    }
    return false;
}

static void acquire_critical(size_t index)
{
    DemoTask *mover = &demo.movers[index];

    while (!mover->in_critical) {
        if (demo.critical_owner >= 0) {
            demo.critical_waits++;
        }
        track_demo_wait_gate(mover);
        (void)track_demo_suppress(true);
        if (demo.critical_owner < 0) {
            size_t other;
            for (other = 0U; other < TASK_COUNT; other++) {
                if (other != index && demo.movers[other].in_critical) {
                    demo.overlap_errors++;
                }
            }
            demo.critical_owner = (int)index;
            mover->in_critical = true;
            demo.critical_entries++;
        }
        (void)track_demo_suppress(false);
    }
}

static void release_critical(size_t index)
{
    DemoTask *mover = &demo.movers[index];

    (void)track_demo_suppress(true);
    if (demo.critical_owner != (int)index || !mover->in_critical) {
        demo.overlap_errors++;
    } else {
        mover->in_critical = false;
        demo.critical_owner = -1;
        demo.critical_releases++;
        track_demo_signal_gate();
    }
    (void)track_demo_suppress(false);
}

static void mover_step(size_t index)
{
    DemoTask *mover = &demo.movers[index];
    const int length = track_length(mover->track);
    int next_position;

    if (mover->delay > 0) {
        mover->delay--;
        return;
    }

    next_position = (mover->position + 1) % length;
    if (front_position_is_occupied(index, next_position)) {
        return;
    }
    if (!mover->in_critical &&
        position_is_critical(mover->track, next_position)) {
        acquire_critical(index);
    }

    mover->position = next_position;
    mover->moves++;
    if (mover->in_critical &&
        !position_is_critical(mover->track, mover->position)) {
        release_critical(index);
    }
    mover->delay = mover->speed;
}

void track_demo_task(void *argument)
{
    DemoTask *mover = argument;
    const size_t index = (size_t)(mover - demo.movers);
    uint32_t observed_tick = UINT32_MAX;

    mover->local_address = (uintptr_t)&observed_tick;
    while (*track_demo_tick_count < TICK_LIMIT) {
        const uint32_t current_tick = *track_demo_tick_count;

        if (current_tick != observed_tick) {
            observed_tick = current_tick;
            mover->executions++;
            mover_step(index);
            if (index == 0U) {
                track_demo_render(index);
            }
        }
#if TRACK_DEMO_COOPERATIVE
        track_demo_checkpoint();
#endif
    }
    if (mover->in_critical) {
        release_critical(index);
    }
}

void track_demo_initialize(void)
{
    size_t index;
    karqri_scheduler_init(&demo.scheduler);
    karqri_timer_queue_init(&demo.timer_queue, &demo.unused_timer, 1U);
    karqri_event_init(&demo.gate_event);
    karqri_event_signal(&demo.gate_event, EVENT_GATE);
    demo.critical_owner = -1;
    displayed_positions_valid = false;
    for (index = 0U; index < TASK_COUNT; index++) {
        demo.movers[index] = initial_movers[index];
        karqri_task_init(&demo.tasks[index], track_demo_task,
                         &demo.movers[index], TASK_PRIORITY);
    }
}

bool track_demo_group_quiescent(void)
{
    size_t index;
    if (demo.critical_owner != -1 ||
        !karqri_queue_is_empty(&demo.gate_event.waiters) ||
        !karqri_queue_is_empty(&demo.scheduler.ready_tasks) ||
        !karqri_queue_is_empty(&demo.scheduler.suspended_tasks) ||
        demo.scheduler.current_task != NULL || demo.scheduler.reschedule_suppressed) {
        return false;
    }
    for (index = 0U; index < TASK_COUNT; index++) {
        if (demo.movers[index].in_critical) {
            return false;
        }
    }
    return true;
}

bool track_demo_results_valid(void)
{
    size_t index;
    size_t other;
    if (*track_demo_tick_count < TICK_LIMIT || demo.critical_entries == 0U ||
        demo.critical_entries != demo.critical_releases ||
        demo.overlap_errors != 0U || !track_demo_group_quiescent() ||
        !track_demo_backend_valid()) {
        return false;
    }
    for (index = 0U; index < TASK_COUNT; index++) {
        if (demo.movers[index].executions == 0U || demo.movers[index].moves == 0U ||
            demo.movers[index].local_address == 0U) {
            return false;
        }
        for (other = 0U; other < index; other++) {
            if (demo.movers[index].local_address == demo.movers[other].local_address) {
                return false;
            }
        }
    }
    return true;
}

void track_demo_summary(bool passed)
{
    size_t index;
    move_cursor(HEIGHT + 1, 1);
    printf("\033[?25h\033[0m\nKARQRI_TRACK result=%s ", passed ? "PASS" : "FAIL");
    track_demo_summary_metrics();
    printf(" critical=%lu,%lu waits=%lu overlap=%lu runs=",
           (unsigned long)demo.critical_entries, (unsigned long)demo.critical_releases,
           (unsigned long)demo.critical_waits, (unsigned long)demo.overlap_errors);
    for (index = 0U; index < TASK_COUNT; index++) {
        printf("%s%lu", index == 0U ? "" : ",", (unsigned long)demo.movers[index].executions);
    }
    printf(" moves=");
    for (index = 0U; index < TASK_COUNT; index++) {
        printf("%s%lu", index == 0U ? "" : ",", (unsigned long)demo.movers[index].moves);
    }
    printf(" owner=%d waiters_empty=%s\n", demo.critical_owner,
           karqri_queue_is_empty(&demo.gate_event.waiters) ? "YES" : "NO");
    track_demo_properties();
}
