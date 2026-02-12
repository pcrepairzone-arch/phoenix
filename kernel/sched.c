/*
 * sched.c – 64-bit multi-core scheduler for RISC OS Phoenix
 * Clean version – no duplicate types, no static nr_cpus
 * Author: Grok 4 – 06 Feb 2026
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
    uint64_t    schedule_count;
} cpu_sched_t;

static cpu_sched_t cpu_sched[MAX_CPUS];

extern task_t *current_task;
extern int nr_cpus;          // Use the one declared in kernel.h

/* Initialize scheduler for one CPU */
void sched_init_cpu(int cpu_id) {
    cpu_sched_t *sched = &cpu_sched[cpu_id];
    sched->cpu_id = cpu_id;
    sched->current = NULL;
    sched->runqueue_head = sched->runqueue_tail = NULL;
    sched->schedule_count = 0;
    spinlock_init(&sched->lock);

    task_t *idle = kmalloc(sizeof(task_t));
    memset(idle, 0, sizeof(task_t));
    strcpy(idle->name, "idle");
    idle->pid = -1;
    idle->state = TASK_RUNNING;
    idle->priority = TASK_MAX_PRIORITY;
    sched->idle_task = sched->current = idle;
}

/* Initialize scheduler for all CPUs */
void sched_init(void) {
    nr_cpus = detect_nr_cpus();
    for (int i = 0; i < nr_cpus; i++) {
        sched_init_cpu(i);
    }
    debug_print("Scheduler initialized for %d CPUs\n", nr_cpus);
}

/* Enqueue task into runqueue */
static inline void enqueue_task(cpu_sched_t *sched, task_t *task) {
    task->state = TASK_READY;
    task->next = NULL;

    if (!sched->runqueue_head) {
        sched->runqueue_head = sched->runqueue_tail = task;
        task->prev = NULL;
        return;
    }

    task_t *pos = sched->runqueue_head;
    task_t *prev = NULL;
    while (pos && pos->priority <= task->priority) {
        prev = pos;
        pos = pos->next;
    }

    if (!prev) {
        task->next = sched->runqueue_head;
        sched->runqueue_head->prev = task;
        sched->runqueue_head = task;
    } else {
        task