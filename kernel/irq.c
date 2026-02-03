/*
 * irq.c – GICv3 Interrupt Controller for RISC OS Phoenix
 * Includes GIC initialization, IPI setup, and handling
 * Author: R Andrews Grok 4 – 26 Nov 2025
 */

#include "kernel.h"
#include "spinlock.h"
#include <stdint.h>

#define GIC_DIST_BASE   0xFF841000ULL  // Pi 5 GIC distributor
#define GIC_REDIST_BASE 0xFF842000ULL  // Pi 5 GIC redistributor base

#define GICD_CTLR       0x0000
#define GICD_IGROUPR(n) (0x0080 + (n)*4)
#define GICD_ISENABLER(n) (0x0100 + (n)*4)
#define GICD_ICENABLER(n) (0x0180 + (n)*4)
#define GICD_ISPENDR(n)  (0x0200 + (n)*4)
#define GICD_ICPENDR(n)  (0x0280 + (n)*4)
#define GICD_ISACTIVER(n) (0x0300 + (n)*4)
#define GICD_ICACTIVER(n) (0x0380 + (n)*4)
#define GICD_IPRIORITYR(n) (0x0400 + (n)*4)
#define GICD_ITARGETSR(n) (0x0800 + (n)*4)
#define GICD_ICFGR(n)     (0x0C00 + (n)*4)

#define GICR_CTLR       0x0000
#define GICR_WAKER      0x0014
#define GICR_IGROUPR0   0x0080
#define GICR_ISENABLER0 0x0100
#define GICR_ICENABLER0 0x0180
#define GICR_ISPENDR0   0x0200
#define GICR_ICPENDR0   0x0280
#define GICR_ISACTIVER0 0x0300
#define GICR_ICACTIVER0 0x0380
#define GICR_IPRIORITYR(n) (0x0400 + (n)*4)
#define GICR_ICFGR0     0x0C00
#define GICR_ICFGR1     0x0C04

/* SGI (Software Generated Interrupt) range: 0-15 */
#define IPI_TLB_SHOOTDOWN 1
#define IPI_RESCHEDULE    2

/* Per-CPU GIC redistributor */
static void *gic_redist[MAX_CPUS];

/* Initialize GICv3 distributor (core 0 only) */
static void gic_dist_init(void) {
    void *dist = ioremap(GIC_DIST_BASE, PAGE_SIZE);

    // Disable distributor
    writel(0, dist + GICD_CTLR);

    // Set all interrupts to group 1 (non-secure)
    for (int i = 0; i < 32; i++) {
        writel(0xFFFFFFFF, dist + GICD_IGROUPR(i));
    }

    // Set priority to 0xFF (lowest) for all
    for (int i = 0; i < 256; i++) {
        writel(0xFFFFFFFF, dist + GICD_IPRIORITYR(i));
    }

    // Enable distributor
    writel(1, dist + GICD_CTLR);

    debug_print("GIC: Distributor initialized\n");
}

/* Initialize GICv3 redistributor (per-core) */
static void gic_redist_init(int cpu_id)
{
    void *redist = ioremap(GIC_REDIST_BASE + cpu_id * 0x20000, 0x20000);

    // Wake redistributor
    writel(0, redist + GICR_WAKER);
    while (readl(redist + GICR_WAKER) & 4);

    // Enable SGIs
    writel(0xFFFFFFFF, redist + GICR_ISENABLER0);

    // Set SGIs to level-triggered (for IPIs)
    writel(0x00000000, redist + GICR_ICFGR0);  // 0 = level

    // Set priority for SGIs
    for (int i = 0; i < 4; i++) {
        writel(0x00000000, redist + GICR_IPRIORITYR(i));  // Highest priority
    }

    gic_redist[cpu_id] = redist;

    debug_print("GIC: Redistributor for CPU %d initialized\n", cpu_id);
}

/* GIC init – called from boot.c */
void irq_init(void)
{
    if (get_cpu_id() == 0) {
        gic_dist_init();
    }

    gic_redist_init(get_cpu_id());

    // Enable CPU interface
    __asm__ volatile ("msr daifclr, #2");  // Enable IRQs

    debug_print("GICv3 initialized – interrupts active\n");
}

/* Send IPI to target CPUs */
void send_ipi(uint64_t target_cpus, int ipi_id, uint64_t arg)
{
    // Target_cpus = bitmask (1 << cpu_id)
    for (int cpu = 0; cpu < MAX_CPUS; cpu++) {
        if (target_cpus & (1ULL << cpu)) {
            // SGI to target CPU
            uint64_t val = (1ULL << 40) | ((uint64_t)cpu << 16) | ipi_id;
            __asm__ volatile ("msr sgi1r_el1, %0" :: "r"(val));
            __asm__ volatile ("dsb sy");
        }
    }
}

/* IPI handler – called from irq_handler */
void ipi_handler(int ipi_id, uint64_t arg)
{
    switch (ipi_id) {
        case IPI_TLB_SHOOTDOWN:
            if (arg == 0) {
                __asm__ volatile ("tlbi vmalle1\n dsb ish\n isb");
            } else {
                __asm__ volatile ("tlbi vae1, %0\n dsb ish\n isb" :: "r"(arg >> PAGE_SHIFT));
            }
            debug_print("IPI: TLB shootdown on CPU %d for 0x%llx\n", get_cpu_id(), arg);
            break;

        case IPI_RESCHEDULE:
            schedule();
            break;

        // Add more IPIs as needed
    }

    // Ack IPI
    uint32_t iar = readl(gic_redist[get_cpu_id()] + GICR_ICPENDR0);
    writel(iar, gic_redist[get_cpu_id()] + GICR_EOIR0);
}

/* IRQ handler entry – from assembly */
void irq_handler(void)
{
    uint32_t iar = readl(gic_redist[get_cpu_id()] + GICR_IAR0);
    int irq = iar & 0x3FF;

    if (irq < 16) {
        // SGI – IPI
        ipi_handler(irq, 0);  // Arg from SGI if needed
    } else {
        // PPI/SPI – device interrupts
        device_irq_handler(irq);
    }

    writel(iar, gic_redist[get_cpu_id()] + GICR_EOIR0);
}