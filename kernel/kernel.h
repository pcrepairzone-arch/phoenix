/*
 * kernel.h – Core Kernel Headers for RISC OS Phoenix
 * Central header file defining all major structures and prototypes
 * Author: Grok 4 – 06 Feb 2026
 */

#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include <stddef.h>

/* ==================== Basic Types & Constants ==================== */

#define TASK_NAME_LEN       32
#define MAX_CPUS            8
#define MAX_FD              1024
#define PAGE_SIZE           4096

#define SIG_DFL             ((void(*)(int))0)
#define SIG_IGN             ((void(*)(int))1)
#define SIG_ERR             ((void(*)(int))-1)

#define O_RDONLY            0x0000
#define O_WRONLY            0x0001
#define O_RDWR              0x0002
#define O_NONBLOCK          0x0004
#define O_CREAT             0x0008

#define S_IFIFO             (1ULL << 12)   // Pipe
#define S_IFREG             (1ULL << 13)   // Regular file
#define S_IFDIR             (1ULL << 14)   // Directory
#define S_IFBLK             (1ULL << 15)   // Block device

/* Mouse buttons (standard RISC OS) */
#define MOUSE_SELECT        1   // Left button
#define MOUSE_MENU          2   // Middle button (context menu)
#define MOUSE_ADJUST        4   // Right button

/* ==================== Spinlock ==================== */

typedef struct {
    uint32_t value;
} spinlock_t;

#define SPINLOCK_INIT       {0}

void spin_lock_irqsave(spinlock_t *lock, unsigned long *flags);
void spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags);

/* ==================== Task Structure ==================== */

typedef enum {
    TASK_RUNNING,
    TASK_READY,
    TASK_BLOCKED,
    TASK_ZOMBIE
} task_state_t;

typedef struct task task_t;

struct task {
    uint64_t        regs[31];           // x0-x30
    uint64_t        sp_el0;             // User stack pointer
    uint64_t        elr_el1;            // Exception Link Register
    uint64_t        spsr_el1;           // Saved Program Status Register
    uint64_t        stack_top;          // Kernel stack top
    task_t         *next;
    task_t         *prev;
    char            name[TASK_NAME_LEN];
    int             pid;
    int             priority;
    task_state_t    state;
    uint64_t        cpu_affinity;       // Bitmask of allowed CPUs
    task_t         *parent;
    task_t        **children;
    int             child_count;
    spinlock_t      children_lock;
    int             exit_status;
    void           *pgtable_l0;         // Page table root (MMU)
    struct file    *files[MAX_FD];      // Open file descriptors
    struct inode   *cwd;                // Current working directory
    struct signal_state signal_state;   // Signal handling state
};

/* ==================== File & VFS ==================== */

typedef struct inode inode_t;
typedef struct file file_t;
typedef struct file_ops file_ops_t;

struct inode {
    uint64_t i_mode;            // File type and permissions
    uint64_t i_size;            // File size in bytes
    uint64_t i_blocks;