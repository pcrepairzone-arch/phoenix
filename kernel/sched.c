/*
 * sched.c – 64-bit multi-core scheduler for RISC OS Phoenix
 * Final clean version – cpu_sched is global, no static nr_cpus
 * Author: Grok 4 – 06 Feb 2026
 */

#include "kernel.h"
#include "spinlock.h"
#include <string.h>

cpu_sched_t cpu_sched[MAX_CPUS];   // Global, visible to boot.c and kernel.c

extern task_t *current_task;
extern int nr_cpus;

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
        task->next = prev->next;
        task->prev = prev;
        if (prev->next) prev->next->prev = task;
        else sched->runqueue_tail = task;
        prev->next = task;
    }
}

/* Dequeue task from runqueue */
static inline void dequeue_task(cpu_sched_t *sched, task_t *task) {
    if (task->prev) task->prev->next = task->next;
    else sched->runqueue_head = task->next;
    if (task->next) task->next->prev = task->prev;
    else sched->runqueue_tail = task->prev;
}

/* Pick next task to run */
static inline task_t *pick_next_task(cpu_sched_t *sched) {
    if (!sched->runqueue_head) {
        return sched->idle_task;
    }
    task_t *next = sched->runqueue_head;
    dequeue_task(sched, next);
    enqueue_task(sched, next);
    return next;
}

/* Context switch */
void context_switch(task_t *prev, task_t *next) {
    current_task = next;

    __asm__ volatile (
        "stp x0, x1, [sp, #-16]!\n"
        "stp x2, x3, [sp, #-16]!\n"
        "stp x4, x5, [sp, #-16]!\n"
        "stp x6, x7, [sp, #-16]!\n"
        "stp x8, x9, [sp, #-16]!\n"
        "stp x10, x11, [sp, #-16]!\n"
        "stp x12, x13, [sp, #-16]!\n"
        "stp x14, x15, [sp, #-16]!\n"
        "stp x16, x17, [sp, #-16]!\n"
        "stp x18, x19, [sp, #-16]!\n"
        "stp x20, x21, [sp, #-16]!\n"
        "stp x22, x23, [sp, #-16]!\n"
        "stp x24, x25, [sp, #-16]!\n"
        "stp x26, x27, [sp, #-16]!\n"
        "stp x28, x29, [sp, #-16]!\n"
        "str x30, [sp, #-16]!\n"
        "mrs %0, sp_el0\n"
        "mrs %1, elr_el1\n"
        "mrs