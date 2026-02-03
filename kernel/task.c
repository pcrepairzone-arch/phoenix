/*
 * task.c – Task management for RISC OS Phoenix
 * Includes task_create, fork, execve, wait
 * Author: R Andrews Grok 4 – 26 Nov 2025
 */

#include "kernel.h"
#include "mmu.h"
#include "vfs.h"
#include "elf64.h"
#include <string.h>

#define KERNEL_STACK_SIZE   (16 * 1024)
#define USER_STACK_SIZE     (8 * 1024 * 1024)

static volatile int next_pid = 1;

task_t *task_create(const char *name, void (*entry)(void), int priority, uint64_t cpu_affinity)
{
    task_t *task = kmalloc(sizeof(task_t));
    if (!task) return NULL;

    uint8_t *kernel_stack = kmalloc(KERNEL_STACK_SIZE);
    if (!kernel_stack) { kfree(task); return NULL; }

    uint8_t *user_stack = kmalloc(USER_STACK_SIZE);
    if (!user_stack) { kfree(kernel_stack); kfree(task); return NULL; }

    memset(task, 0, sizeof(task_t));
    strncpy(task->name, name, TASK_NAME_LEN-1);
    task->pid = __atomic_add_fetch(&next_pid, 1, __ATOMIC_SEQ_CST);
    task->priority = priority;
    task->state = TASK_READY;
    task->cpu_affinity = cpu_affinity ? cpu_affinity : (1ULL << get_cpu_id());

    task->stack_top = (uint64_t)(kernel_stack + KERNEL_STACK_SIZE);
    task->sp_el0 = (uint64_t)(user_stack + USER_STACK_SIZE);

    memset(task->regs, 0, sizeof(task->regs));
    task->regs[0] = 0;  // x0 = 0
    task->elr_el1 = (uint64_t)entry;
    task->spsr_el1 = 0;  // EL0, interrupts enabled

    int cpu = get_cpu_id();
    cpu_sched_t *sched = &cpu_sched[cpu];
    unsigned long flags;
    spin_lock_irqsave(&sched->lock, flags);
    enqueue_task(sched, task);
    spin_unlock_irqrestore(&sched->lock, flags);

    debug_print("Task created: '%s' PID=%d on CPU %d\n", task->name, task->pid, cpu);

    return task;
}

int fork(void)
{
    task_t *parent = current_task;
    task_t *child = kmalloc(sizeof(task_t));
    if (!child) return -1;

    int child_pid = __atomic_add_fetch(&next_pid, 1, __ATOMIC_SEQ_CST);

    memcpy(child, parent, sizeof(task_t));
    child->pid = child_pid;
    child->parent = parent;
    child->state = TASK_READY;
    strncpy(child->name, parent->name, TASK_NAME_LEN-1);
    strncat(child->name, "+", 1);

    if (mmu_duplicate_pagetable(parent, child) != 0) { kfree(child); return -1; }

    child->regs[0] = 0;  // Child returns 0

    uint8_t *new_stack = kmalloc(KERNEL_STACK_SIZE);
    if (!new_stack) { mmu_free_pagetable(child); kfree(child); return -1; }
    memcpy(new_stack, (void*)(parent->stack_top - KERNEL_STACK_SIZE), KERNEL_STACK_SIZE);
    child->stack_top = (uint64_t)(new_stack + KERNEL_STACK_SIZE);

    uint64_t offset = parent->sp_el0 - (parent->stack_top - KERNEL_STACK_SIZE);
    child->sp_el0 = child->stack_top - offset;

    int cpu = get_cpu_id();
    cpu_sched_t *sched = &cpu_sched[cpu];
    unsigned long flags;
    spin_lock_irqsave(&sched->lock, flags);
    enqueue_task(sched, child);
    spin_unlock_irqrestore(&sched->lock, flags);

    return child_pid;  // Parent returns child PID
}

int execve(const char *pathname, char *const argv[], char *const envp[])
{
    task_t *task = current_task;
    file_t *file = vfs_open(pathname, O_RDONLY);
    if (!file) return -1;

    Elf64_Ehdr ehdr;
    if (vfs_read(file, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) goto fail;

    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr.e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr.e_ident[EI_DATA] != ELFDATA2LSB ||
        ehdr.e_machine != EM_AARCH64) goto fail;

    mmu_free_usermemory(task);

    uint64_t entry = ehdr.e_entry;
    uint64_t phoff = ehdr.e_phoff;

    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf64_Phdr phdr;
        vfs_seek(file, phoff + i * ehdr.e_phentsize, SEEK_SET);
        vfs_read(file, &phdr, sizeof(phdr));

        if (phdr.p_type == PT_LOAD) {
            void *page = kmalloc((phdr.p_memsz + PAGE_SIZE - 1) & PAGE_MASK);
            vfs_seek(file, phdr.p_offset, SEEK_SET);
            vfs_read(file, page, phdr.p_filesz);
            memset(page + phdr.p_filesz, 0, phdr.p_memsz - phdr.p_filesz);

            mmu_map(task, phdr.p_vaddr, phdr.p_memsz, phdr.p_flags, 0);
        }
    }

    vfs_close(file);

    uint64_t sp = 0x0000fffffffff000ULL;
    int argc = 0, env