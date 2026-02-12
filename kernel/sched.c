/*
 * sched.c – Minimal Scheduler for RISC OS Phoenix
 * Simplified to get a successful build
 */

#include "kernel.h"
#include "spinlock.h"
#include <string.h>

typedef struct {
    task_t     *current;
    task_t     *idle_task;
    task_t     *runqueue_head;
    task_t     *runqueue_tail;
    spinlock_t  lock;
    int         cpu_id;
} cpu_sched_t;

static cpu_sched_t cpu_sched[8];
static int nr_cpus = 1;

extern task_t *current_task;

void sched_init_cpu(int cpu_id) {
    cpu_sched_t *sched = &cpu_sched[cpu_id];
    sched->cpu_id = cpu_id;
    sched->current = NULL;
    sched->runqueue_head = sched->runqueue_tail = NULL;
    spinlock_init(&sched->lock);

    task_t *idle = kmalloc(sizeof(task_t));
    memset(idle, 0, sizeof(task_t));
    strcpy(idle->name, "idle");
    idle->pid = -1;
    idle->state = TASK_RUNNING;
    idle->priority = TASK_MAX_PRIORITY;
    sched->idle_task = sched->current = idle;
}

void sched_init(void) {
    for (int i = 0; i < 4; i++) {   // Assume 4 cores for now
        sched_init_cpu(i);
    }
    debug_print("Scheduler initialized\n");
}

void schedule(void) {
    // Simplified scheduler - just yield to idle for now
    // We will expand this later
}

void yield(void) {
    schedule();
}

void task_wakeup(task_t *task) {
    // Stub
}