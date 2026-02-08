/*
 * task.c – Task management for RISC OS Phoenix
 * Includes task_create, fork, execve, wait
 * Full ELF64 loader in execve: Ehdr, Phdr, Shdr, dynamic linking (DT_NEEDED, relocs, symbols)
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

    mmu_init_task(task);

    int cpu = __builtin_ctzll(task->cpu_affinity);  // First affinity CPU
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

    // Add to parent's children
    unsigned long flags;
    spin_lock_irqsave(&parent->children_lock, flags);
    parent->children = krealloc(parent->children, (parent->child_count + 1) * sizeof(task_t*));
    if (!parent->children) { spin_unlock_irqrestore(&parent->children_lock, flags); return -1; }
    parent->children[parent->child_count++] = child;
    spin_unlock_irqrestore(&parent->children_lock, flags);

    int cpu = get_cpu_id();
    cpu_sched_t *sched = &cpu_sched[cpu];
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
    ssize_t read_size = vfs_read(file, &ehdr, sizeof(ehdr));
    if (read_size != sizeof(ehdr)) goto fail;

    // Validate ELF64
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr.e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr.e_ident[EI_DATA] != ELFDATA2LSB ||
        ehdr.e_machine != EM_AARCH64 ||
        ehdr.e_type != ET_EXEC) {
        vfs_close(file);
        return -1;
    }

    // Free existing user memory
    mmu_free_usermemory(task);

    uint64_t entry = ehdr.e_entry;
    uint64_t phoff = ehdr.e_phoff;
    uint64_t shoff = ehdr.e_shoff;
    int phnum = ehdr.e_phnum;
    int shnum = ehdr.e_shnum;
    int shstrndx = ehdr.e_shstrndx;

    // Load program headers
    Elf64_Phdr *phdrs = kmalloc(phnum