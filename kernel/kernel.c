/*
 * kernel.c – Central Kernel Initialization for RISC OS Phoenix
 * Main kernel entry point and subsystem initialization
 * Author: Grok 4 – 06 Feb 2026
 */

#include "kernel.h"

extern void sched_init(void);
extern void irq_init(void);
extern void timer_init(void);
extern void mmu_init(void);
extern void vfs_init(void);
extern void pci_scan_bus(void);
extern void net_init(void);
extern void wimp_init(void);
extern void register_default_handlers(void);

/* Global kernel state */
int nr_cpus = 1;
task_t *current_task = NULL;

/* Main kernel entry point */
__attribute__((noreturn))
void kernel_main(uint64_t dtb_ptr)
{
    debug_print("\n");
    debug_print("========================================\n");
    debug_print("   RISC OS Phoenix Kernel Starting...\n");
    debug_print("========================================\n\n");

    /* 1. Early CPU detection */
    nr_cpus = detect_nr_cpus();
    debug_print("Detected %d CPU cores\n", nr_cpus);

    /* 2. Initialize core subsystems */
    mmu_init();                 // Memory Management Unit + protection
    sched_init();               // Multi-core scheduler
    irq_init();                 // GICv3 interrupt controller
    timer_init();               // ARM Generic Timer

    /* 3. Device & bus initialization */
    pci_scan_bus();             // Scan PCI devices (NVMe, xHCI, etc.)

    /* 4. Filesystem & VFS */
    vfs_init();
    filecore_init();            // RISC OS FileCore

    /* 5. Networking */
    net_init();                 // PhoenixNet TCP/IP stack

    /* 6. User Interface */
    wimp_init();

    /* 7. Default signal handlers */
    register_default_handlers();

    debug_print("\n");
    debug_print("========================================\n");
    debug_print("   RISC OS Phoenix Kernel Ready!\n");
    debug_print("========================================\n\n");

    /* Start the first user task (init) */
    task_t *init_task = task_create("init", init_process, 10, 0);
    if (init_task) {
        current_task = init_task;
    }

    /* Enter the scheduler – never returns */
    schedule();

    /* Should never reach here */
    while (1) {
        __asm__ volatile ("wfe");
    }
}

/* Example init process */
void init_process(void)
{
    debug_print("Init process started – launching desktop...\n");

    /* Start Wimp desktop task */
    task_create("Wimp", wimp_task, 0, (1ULL << 0));  // Pin to core 0

    /* Start example apps */
    task_create("Paint64", paint_task, 10, 0);
    task_create("NetSurf64", netsurf_task, 10, 0);

    /* Idle loop */
    while (1) {
        yield();
    }
}

/* Kernel panic / halt */
void halt_system(void)
{
    debug_print("!!! KERNEL PANIC - System halted !!!\n");
    while (1) {
        __asm__ volatile ("wfi");
    }
}