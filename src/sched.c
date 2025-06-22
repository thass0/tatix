#include <tx/kvalloc.h>
#include <tx/sched.h>

static bool global_sched_initialized;

static struct sched_task global_main_task; // Main task.
static u16 global_next_id; // ID to use for the next task that's registered.
static struct sched_task *global_current_task; // Task that's currently executing.
static struct dlist global_sleep_list; // List of all sleeping tasks.

void sched_init(void)
{
    assert(!global_sched_initialized);

    byte_array_set(byte_array_new((void *)&global_main_task, sizeof(global_main_task)), 0);
    global_main_task.id = global_next_id++;
    global_current_task = &global_main_task;

    dlist_init_empty(&global_sleep_list);

    global_sched_initialized = true;
}

///////////////////////////////////////////////////////////////////////////////
// Sleep list                                                                //
///////////////////////////////////////////////////////////////////////////////

// Add a task to the sleep list. Ensures that the first task in the sleep list has the lowest wake time. I.e., the
// first task in the sleep list is the first to become ready.
static void sched_add_sleeping(struct sched_task *new_task)
{
    struct dlist *list = global_sleep_list.next;
    struct sched_task *task = NULL;

    while (list != &global_sleep_list) {
        task = __container_of(list, struct sched_task, sleep_list);

        if (task->wake_time.ms > new_task->wake_time.ms) {
            dlist_insert(task->sleep_list.prev, &new_task->sleep_list);
            return;
        }

        list = list->next;
    }

    dlist_insert(list->prev, &new_task->sleep_list);
}

// Remove a task from the sleep list.
static void sched_remove_sleeping(struct sched_task *task)
{
    dlist_remove(&task->sleep_list);
}

// Search the sleep list for sleeping tasks that are ready to run. Returns `NULL` if there are no such tasks.
static struct sched_task *sched_poll_sleeping(void)
{
    // NOTE: This works because the sleep list is ordered by wake time.

    struct sched_task *task = __container_of(global_sleep_list.next, struct sched_task, sleep_list);
    struct time_ms current_time = time_current_ms();

    if (current_time.ms < task->wake_time.ms)
        return NULL;

    return task;
}

// Returns a non-null pointer to a sleeping task that's ready to run.
static struct sched_task *sched_get_ready(void)
{
    struct sched_task *ready = sched_poll_sleeping();
    while (!ready)
        ready = sched_poll_sleeping();
    return ready;
}

///////////////////////////////////////////////////////////////////////////////
// Tasks                                                                     //
///////////////////////////////////////////////////////////////////////////////

// See sched.s
extern void sched_do_context_switch(u64 **old_sp, u64 *new_sp);
extern void sched_do_final_context_switch(u64 *new_sp);

static void sched_switch_task(struct sched_task *next_task)
{
    if (next_task == global_current_task)
        return; // Can skip the context switch.

    // If `next_task` isn't the current task, it came from the sleep list and must be removed there before being run.
    sched_remove_sleeping(next_task);

    struct sched_task *old = global_current_task;
    global_current_task = next_task;
    sched_do_context_switch(&old->stack_ptr, global_current_task->stack_ptr);
}

static void sched_final_switch_task(struct sched_task *next_task)
{
    assert(next_task != global_current_task);

    sched_remove_sleeping(next_task);

    global_current_task = next_task;
    sched_do_final_context_switch(next_task->stack_ptr);
}

static void sched_task_finish(void)
{
    // We don't allow the main task to finish so that there is always a task left to execute.
    assert(global_current_task != &global_main_task);

    kvalloc_free(byte_array_new((void *)global_current_task, sizeof(*global_current_task)));

    sched_final_switch_task(sched_get_ready());

    crash("Can't return from final context switch, current task is deleted\n");
}

static void sched_task_entry(void)
{
    assert(global_current_task);
    assert(global_current_task->callback);

    global_current_task->callback(global_current_task->context);

    sched_task_finish();
}

struct result sched_create_task(sched_callback_func_t callback, void *context)
{
    assert(global_sched_initialized);

    assert(callback);

    struct option_byte_array task_mem_opt = kvalloc_alloc(sizeof(struct sched_task), alignof(struct sched_task));
    if (task_mem_opt.is_none)
        return result_error(ENOMEM);
    struct sched_task *task = byte_array_ptr(option_byte_array_checked(task_mem_opt));

    task->callback = callback;
    task->context = context;

    task->stack_ptr = (u64 *)(task->stack + TASK_STACK_SIZE) - 1;

    // Set up the stack so that context switches return to `sched_task_entry`.
    *(task->stack_ptr) = (u64)sched_task_entry;
    *(task->stack_ptr - 1) = (u64)task->stack_ptr; // rbp
    *(task->stack_ptr - 2) = 0; // rbx
    *(task->stack_ptr - 3) = 0; // r12
    *(task->stack_ptr - 4) = 0; // r13
    *(task->stack_ptr - 5) = 0; // r14
    *(task->stack_ptr - 6) = 0; // r15

    task->stack_ptr = task->stack_ptr - 6;

    task->id = global_next_id++;

    task->wake_time = time_ms_new(0); // Will be woken up as soon as possible.
    sched_add_sleeping(task);

    return result_ok();
}

u16 sched_current_id(void)
{
    if (!global_sched_initialized)
        return 0;
    return global_current_task->id;
}

///////////////////////////////////////////////////////////////////////////////
// Sleep                                                                     //
///////////////////////////////////////////////////////////////////////////////

void sleep_ms(struct time_ms duration)
{
    assert(global_sched_initialized);

    struct time_ms start_time = time_current_ms();
    global_current_task->wake_time = time_ms_new(start_time.ms + duration.ms);

    sched_add_sleeping(global_current_task);
    sched_switch_task(sched_get_ready());
    sched_remove_sleeping(global_current_task);

    // Verify that the sleep didn't end prematurely.
    assert(time_current_ms().ms - start_time.ms >= duration.ms);
}
