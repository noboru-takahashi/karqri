CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror

BUILD_DIR := build
QUEUE_TEST := $(BUILD_DIR)/test_queue
EVENT_TEST := $(BUILD_DIR)/test_event
TIMER_TEST := $(BUILD_DIR)/test_timer
TIMER_EVENT_TEST := $(BUILD_DIR)/test_timer_event
TASK_TEST := $(BUILD_DIR)/test_task
SCHEDULER_TEST := $(BUILD_DIR)/test_scheduler
POSIX_TEST := $(BUILD_DIR)/test_posix_kernel
TESTS := $(QUEUE_TEST) $(TASK_TEST) $(EVENT_TEST) \
         $(TIMER_TEST) $(TIMER_EVENT_TEST) $(SCHEDULER_TEST) $(POSIX_TEST)

.PHONY: all test clean

all: $(TESTS)

$(QUEUE_TEST): kernel/queue.c tests/test_queue.c include/karqri/queue.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -Iinclude kernel/queue.c tests/test_queue.c -o $@

$(TASK_TEST): kernel/task.c kernel/queue.c tests/test_task.c \
             include/karqri/task.h include/karqri/queue.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -Iinclude \
		kernel/task.c kernel/queue.c tests/test_task.c -o $@

$(EVENT_TEST): kernel/event.c kernel/queue.c \
              tests/test_event.c tests/event_probe.h include/karqri/event.h \
              include/karqri/queue.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -Iinclude \
		kernel/event.c kernel/queue.c \
		tests/test_event.c -o $@

$(TIMER_TEST): kernel/timer.c kernel/queue.c tests/test_timer.c \
              include/karqri/timer.h include/karqri/queue.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -Iinclude \
		kernel/timer.c kernel/queue.c tests/test_timer.c -o $@

$(TIMER_EVENT_TEST): kernel/timer_event.c kernel/timer.c \
                    kernel/event.c kernel/queue.c \
                    tests/test_timer_event.c tests/event_probe.h \
                    include/karqri/timer_event.h include/karqri/timer.h \
                    include/karqri/event.h include/karqri/queue.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -Iinclude \
		kernel/timer_event.c kernel/timer.c \
		kernel/event.c kernel/queue.c tests/test_timer_event.c -o $@

$(SCHEDULER_TEST): kernel/scheduler_event.c kernel/scheduler.c \
                  kernel/timer_event.c kernel/timer.c kernel/event.c \
                  kernel/task.c kernel/queue.c tests/test_scheduler.c \
                  include/karqri/scheduler_event.h \
                  include/karqri/scheduler.h include/karqri/timer_event.h \
                  include/karqri/timer.h include/karqri/event.h \
                  include/karqri/task.h include/karqri/queue.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -Iinclude \
		kernel/scheduler_event.c kernel/scheduler.c kernel/timer_event.c \
		kernel/timer.c kernel/event.c kernel/task.c kernel/queue.c \
		tests/test_scheduler.c -o $@

$(POSIX_TEST): ports/posix/posix_kernel.c ports/posix/posix_port.c \
              kernel/scheduler_event.c kernel/scheduler.c \
              kernel/timer_event.c kernel/timer.c kernel/event.c \
              kernel/task.c kernel/queue.c tests/test_posix_kernel.c \
              ports/posix/posix_kernel.h ports/posix/posix_port.h \
              include/karqri/scheduler_event.h \
              include/karqri/scheduler.h include/karqri/timer_event.h \
              include/karqri/timer.h include/karqri/event.h \
              include/karqri/task.h include/karqri/queue.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -D_XOPEN_SOURCE=700 -Iinclude -Iports/posix \
		ports/posix/posix_kernel.c ports/posix/posix_port.c \
		kernel/scheduler_event.c kernel/scheduler.c kernel/timer_event.c \
		kernel/timer.c kernel/event.c kernel/task.c kernel/queue.c \
		tests/test_posix_kernel.c -o $@

test: $(TESTS)
	@for test_binary in $(TESTS); do ./$$test_binary || exit 1; done

clean:
	rm -rf $(BUILD_DIR)

# Standalone interactive demo; deliberately not part of make test.
POSIX_TRACK_SOURCES := demos/common/track_demo.c demos/posix/track_demo.c \
    ports/posix/posix_kernel.c ports/posix/posix_port.c \
    kernel/scheduler_event.c kernel/scheduler.c kernel/timer_event.c \
    kernel/timer.c kernel/event.c kernel/task.c kernel/queue.c
.PHONY: posix-track-demo
posix-track-demo: $(BUILD_DIR)/karqri_posix_track_demo

$(BUILD_DIR)/karqri_posix_track_demo: $(POSIX_TRACK_SOURCES) \
    demos/common/track_demo.h ports/posix/posix_kernel.h ports/posix/posix_port.h \
    $(wildcard include/karqri/*.h)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -D_XOPEN_SOURCE=700 -DTRACK_DEMO_COOPERATIVE=1 \
		-Iinclude -Iports/posix -Idemos/common $(POSIX_TRACK_SOURCES) -o $@
