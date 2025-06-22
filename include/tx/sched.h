// Simple cooperative scheduling logic.

#ifndef __TX_SCHED_H__
#define __TX_SCHED_H__

#include <tx/base.h>
#include <tx/error.h>
#include <tx/list.h>
#include <tx/time.h>

#define TASK_STACK_SIZE 0x4000

typedef void (*sched_callback_func_t)(void *context);

struct sched_task {
    byte stack[TASK_STACK_SIZE];
    u64 *stack_ptr;

    struct time_ms wake_time;
    u16 id;

    sched_callback_func_t callback;
    void *context;

    struct dlist sleep_list;
};

// Initialize the scheduling subsystem. The current flow of execution that calls `sched_init` becomes the main task.
// Whenever all tasks have run to completion, the main task is scheduled. The main task cannot run to completion (only
// tasks created with `sched_create_task` can).
//
// The main task can start other tasks with `sched_create_task` once the scheduler is initialized. Then, the main
// task should periodically sleep to allow the new tasks to run.
void sched_init(void);

// Create a new task. The task is scheduled the first time `sleep_*` is called after this function return. It can yield
// control by itself calling `sleep_*`. When `callback` returns, the task is deleted and other tasks are scheduled.
struct result sched_create_task(sched_callback_func_t callback, void *context);

// Return the ID of the task that is currently running. This function can be called even before the scheduling
// subsystem was initialized. It will return 0 in that case. This is consitent with the fact that the main task
// that's executed by calling `sched_init` has ID 0.
u16 sched_current_id(void);

// Relinquish control of execution for `duration` milliseconds. Execution of the task calling this function will
// resume once at least `duration` milliseconds have passed. Other tasks will run in the meantime. If these other
// tasks don't frequently yield control (by calling sleep or completing), the waiting task may be delayed longer
// than `duration` milliseconds.
void sleep_ms(struct time_ms duration);

#endif // __TX_SCHED_H__
