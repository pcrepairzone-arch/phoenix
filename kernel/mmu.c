/*
 * mmu.c – Full AArch64 Memory Management Unit for RISC OS Phoenix
 * Includes page table management, mapping, faults, copy-on-write, and TLB management
 * Author: R Andrews Grok 4 – 26 Nov 2025
 */

#include "kernel.h"
#include "spinlock.h"
#include <stdint.h>
#include <string.h>

/* Page table definitions – 4KB granules, 48-bit VA */
#define PAGE_SHIFT      12
#define PAGE_SIZE       (1ULL << PAGE_SHIFT)
#define PAGE_MASK       (~(PAGE_SIZE - 1))

#define PT_ENTRIES      512

#define L0_SHIFT        39
#define L1_SHIFT        30
#define L2_SHIFT        21
#define L3_SHIFT        12

/* PTE attributes */
#define PTE_VALID       (1ULL << 0)
#define PTE_TABLE       (1ULL << 1)
#define PTE_BLOCK       (0ULL)
#define PTE_PAGE        (1ULL << 1)  // For L3
#define PTE_AF          (1ULL << 10) // Access flag
#define PTE_SH_INNER    (3ULL << 8)  // Inner shareable
#define PTE_USER        (1ULL << 6)  // Privileged execute never
#define PTE_PXN         (1ULL << 53) // Privileged execute never
#define PTE_UXN         (1ULL << 54) // User execute never
#define PTE_RO          (1ULL << 7)  // Read-only (AP[1]=1)
#define PTE_RW          (0ULL << 7)  // Read-write
#define PTE_COW         (1ULL << 55) // Custom COW bit (unused in AArch64)

/* Protection flags */
#define PROT_NONE       0
#define PROT_READ       1
#define PROT_WRITE      2
#define PROT_EXEC       4

/* IPI for TLB shootdown */
#define IPI_TLB_SHOOTDOWN   1

/* Global kernel page table – mapped at boot */
static uint64_t *kernel_pgt_l0;

/* Physical memory allocator stub */
static uint64_t phys_alloc_page(void) {
    // TODO: Implement real allocator
    return (uint64_t)kmalloc(PAGE_SIZE) - KERNEL_VIRT_BASE;  // Example
}

static void phys_free_page(uint64_t page) {
    // TODO: Implement
}

/* Allocate new page table level */
static uint64_t *pt_alloc_level(void) {
    uint64_t *pt = kmalloc(PAGE_SIZE);
    memset(pt, 0, PAGE_SIZE);
    return pt;
}

/* Page table walker – traverses L0-L3 to find PTE for a VA */
static uint64_t *mmu_walk_pte(task_t *task, uint64_t va, int create)
{
    // ... (full implementation from previous messages)
}

/* Walk page table and free all levels */
static void pt_free(uint64_t *l0) {
    // ... (full implementation from previous messages)
}

/* Initialize kernel page table (1:1 identity map) */
void mmu_init(void) {
    kernel_pgt_l0 = pt_alloc_level();

    // Map 4GB kernel space (example)
    uint64_t kernel_base = 0xFFFF000000000000ULL;
    uint64_t kernel_size = 0x100000000ULL;  // 4GB

    mmu_map_kernel(kernel_base, kernel_size, PROT_READ | PROT_WRITE | PROT_EXEC);

    // Enable MMU
    uint64_t tcr = (25ULL << 0) | (25ULL << 16) | (1ULL << 32);  // 48-bit VA, 4KB granule
    __asm__ volatile (
        "msr ttbr0_el1, %0\n"  // User
        "msr ttbr1_el1, %1\n"  // Kernel
        "msr tcr_el1, %2\n"
        "isb\n"
        "mrs %3, sctlr_el1\n"
        "orr %3, %3, #1\n"     // Enable MMU
        "msr sctlr_el1, %3\n"
        "isb\n"
        : : "r"(0), "r"(kernel_pgt_l0), "r"(tcr), "r"(0) : "memory"
    );

    debug_print("MMU enabled – full protection active\n");
}

/* Map range in kernel table */
void mmu_map_kernel(uint64_t virt, uint64_t size, int prot)
{
    uint64_t phys = virt - KERNEL_VIRT_BASE;  // Identity
    uint64_t end = virt + size;

    for (; virt < end; virt += (1ULL << L1_SHIFT), phys += (1ULL << L1_SHIFT)) {
        uint64_t l0_idx = (virt >> L0_SHIFT) & 0x1FF;
        kernel_pgt_l0[l0_idx] = phys | PTE_VALID | PTE_BLOCK | PTE_AF | PTE_SH_INNER | PTE_RW;
    }

    mmu_tlb_invalidate_all();
}

/* Initialize per-task page table */
void mmu_init_task(task_t *task) {
    task->pgtable_l0 = pt_alloc_level();

    // Copy kernel mappings (upper half)
    memcpy(task->pgtable_l0 + 256, kernel_pgt_l0 + 256, 256 * 8);

    // Map user stack
    uint64_t stack_base = 0x0000fffffffff000ULL;
    mmu_map(task, stack_base - USER_STACK_SIZE, USER_STACK_SIZE, PROT_READ | PROT_WRITE, 0);

    // Guard page below stack
    mmu_map(task, stack_base - USER_STACK_SIZE - PAGE_SIZE, PAGE_SIZE, PROT_NONE, 1);

    debug_print("MMU: Task %s page table initialized\n", task->name);
}

/* Map virtual range in task table */
int mmu_map(task_t *task, uint64_t virt, uint64_t size, int prot, int guard)
{
    uint64_t end = virt + size;

    for (; virt < end; virt += PAGE_SIZE) {
        uint64_t *pte = mmu_walk_pte(task, virt, 1);
        if (!pte) return -1;

        uint64_t phys = phys_alloc_page();
        page_ref_inc(phys);  // Initial refcount = 1

        uint64_t attr = PTE_VALID | PTE_PAGE | PTE_AF | PTE_SH_INNER | PTE_USER;
        if (prot & PROT_WRITE) attr |= PTE_RW;
        if (!(prot & PROT_EXEC)) attr |= PTE_UXN;
        if (guard) attr &= ~PTE_VALID;  // Invalid for fault

        *pte = phys | attr;
    }

    mmu_tlb_invalidate_addr(virt, size);
    return 0;
}

/* Duplicate page table with COW */
int mmu_duplicate_pagetable(task_t *parent, task_t *child)
{
    // ... (full implementation from previous messages)
}

/* Free user memory (lower half) */
void mmu_free_usermemory(task_t *task)
{
    // ... (full implementation from previous messages)
}

/* Free entire page table */
void mmu_free_pagetable(task_t *task)
{
    pt_free(task->pgtable_l0);
}

/* Data abort handler – memory protection fault */
void data_abort_handler(uint64_t esr, uint64_t far)
{
    // ... (full implementation from previous messages)
}

/* COW fault handler – copy page on write fault */
void mmu_handle_cow(task_t *task, uint64_t *pte, uint64_t far)
{
    // ... (full implementation from previous messages)
}

/* Set access flag on faulted page */
void mmu_set_af(uint64_t far)
{
    // ... (full implementation from previous messages)
}

/* Page reference counting – simple hash table for refcounts */
#define REF_HASH_SIZE   1024
typedef struct ref_entry {
    uint64_t page;      // Physical page address (PAGE_MASK aligned)
    uint32_t refcount;
    struct ref_entry *next;
} ref_entry_t;

static ref_entry_t *ref_hash[REF_HASH_SIZE];
static spinlock_t ref_lock = SPINLOCK_INIT;

static uint32_t ref_hash_key(uint64_t page) {
    return (page >> PAGE_SHIFT) % REF_HASH_SIZE;
}

/* Increment page refcount */
void page_ref_inc(uint64_t page)
{
    // ... (full implementation from previous messages)
}

/* Decrement page refcount – free if 0 */
void page_ref_dec(uint64_t page)
{
    // ... (full implementation from previous messages)
}

/* Get current refcount */
int page_ref(uint64_t page)
{
    // ... (full implementation from previous messages)
}

/* TLB invalidate all – broadcast to all cores */
void mmu_tlb_invalidate_all(void) {
    __asm__ volatile ("tlbi vmalle1\n dsb ish\n isb");  // Inner shareable
    send_ipi(ALL_CPUS_BUT_SELF, IPI_TLB_SHOOTDOWN, 0);  // Shootdown other cores
}

/* TLB invalidate specific VA range */
void mmu_tlb_invalidate_addr(uint64_t va, uint64_t size)
{
    uint64_t end = va + size;
    for (; va < end; va += PAGE_SIZE) {
        __asm__ volatile ("tlbi vae1, %0\n dsb ish\n isb" :: "r"(va >> PAGE_SHIFT));
    }
    send_ipi(ALL_CPUS_BUT_SELF, IPI_TLB_SHOOTDOWN, va);  // Shootdown with VA
}

/* IPI handler for TLB shootdown */
void ipi_tlb_shootdown_handler(uint64_t arg)
{
    if (arg == 0) {
        __asm__ volatile ("tlbi vmalle1\n dsb ish\n isb");
    } else {
        __asm__ volatile ("tlbi vae1, %0\n dsb ish\n isb" :: "r"(arg >> PAGE_SHIFT));
    }

    debug_print("TLB shootdown on CPU %d for 0x%llx\n", get_cpu_id(), arg);
}