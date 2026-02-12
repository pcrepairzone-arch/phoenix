/*
 * kernel.h – Core Kernel Headers for RISC OS Phoenix
 * Self-contained version – all types defined or forward-declared
 * Author: R andrews Grok 4 – 06 Feb 2026
 */

#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include <stddef.h>

/* ==================== Basic Types ==================== */

typedef int64_t ssize_t;
typedef int64_t off_t;
typedef int32_t pid_t;

/* ==================== Constants ==================== */

#define TASK_NAME_LEN       32
#define MAX_CPUS            8
#define MAX_FD              1024
#define PAGE_SIZE           4096

#define MOUSE_SELECT        1
#define MOUSE_MENU          2
#define MOUSE_ADJUST        4

#define SIG_DFL             ((void(*)(int))0)
#define SIG_IGN             ((void(*)(int))1)
#define SIG_ERR             ((void(*)(int))-1)

#define TASK_MIN_PRIORITY   0
#define TASK_MAX_PRIORITY   255

/* ==================== Spinlock ==================== */

typedef struct {
    uint32_t value;
} spinlock_t;

#define SPINLOCK_INIT {0}

/* ==================== Signal State ==================== */

typedef struct signal_state {
    void (*handlers[32])(int);
    uint64_t pending;
    uint64_t blocked;
    uint64_t old_mask;
    uint64_t sigreturn_sp;
} signal_state_t;

/* ==================== Task Structure ==================== */

typedef enum {
    TASK_RUNNING,
    TASK_READY,
    TASK_BLOCKED,
    TASK_ZOMBIE
} task_state_t;

typedef struct task task_t;

struct task {
    uint64_t        regs[31];
    uint64_t        sp_el0;
    uint64_t        elr_el1;
    uint64_t        spsr_el1;
    uint64_t        stack_top;
    task_t         *next;
    task_t         *prev;
    char            name[TASK_NAME_LEN];
    int             pid;
    int             priority;
    task_state_t    state;
    uint64_t        cpu_affinity;
    task_t         *parent;
    task_t        **children;
    int             child_count;
    spinlock_t      children_lock;
    int             exit_status;
    void           *pgtable_l0;
    struct file    *files[MAX_FD];
    struct inode   *cwd;
    signal_state_t  signal_state;
};

/* ==================== Function Prototypes ==================== */

void kernel_main(uint64_t dtb_ptr);
void halt_system(void);
void debug_print(const char *fmt, ...);

void sched_init(void);
void sched_init_cpu(int cpu_id);
void schedule(void);
void yield(void);
void task_block(task_state_t state);
void task_wakeup(task_t *task);

task_t *task_create(const char *name, void (*entry)(void), int priority, uint64_t cpu_affinity);
int fork(void);
int execve(const char *pathname, char *const argv[], char *const envp[]);
pid_t wait(int *wstatus);
pid_t waitpid(pid_t pid, int *wstatus, int options);

void mmu_init(void);
void mmu_init_task(task_t *task);
int mmu_map(task_t *task, uint64_t virt, uint64_t size, int prot, int guard);
int mmu_duplicate_pagetable(task_t *parent, task_t *child);
void mmu_free_usermemory(task_t *task);
void data_abort_handler(uint64_t esr, uint64_t far);

void (*signal(int sig, void (*handler)(int)))(int);
int kill(pid_t pid, int sig);
void check_signals(void);
void sig_init(task_t *task);

uint64_t get_time_ns(void);
void timer_init(void);
void timer_schedule(timer_t *timer, uint64_t ms);

void irq_init(void);
void irq_set_handler(int vector, void (*handler)(int, void*), void *private);
void irq_unmask(int vector);
void irq_eoi(int vector);
void send_ipi(uint64_t target_cpus, int ipi_id, uint64_t arg);

void pci_scan_bus(void);

struct blockdev *blockdev_register(const char *name, uint64_t size, uint32_t block_size);
struct blockdev *blockdev_get(const char *name, int unit);

int Wimp_Poll(int mask, struct wimp_event *event);
struct window *wimp_create_window(struct wimp_window_def *def);
void wimp_redraw_request(struct window *win, struct bbox *clip);

extern task_t *current_task;
extern int nr_cpus;

#endif /* KERNEL_H */