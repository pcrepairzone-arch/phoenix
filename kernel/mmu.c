/*
 * mmu.c – Full AArch64 Memory Management Unit for RISC OS Phoenix
 * Includes page table management, mapping, faults, copy-on-write, TLB management
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
    uint64_t *pgd = task->pgtable_l0;
    uint64_t *pud, *pmd, *pte;

    // L0
    uint64_t idx = (va >> L0_SHIFT) & (PT_ENTRIES - 1);
    if (!(pgd[idx] & PTE_VALID)) {
        if (!create) return NULL;
        pud = pt_alloc_level();
        pgd[idx] = (uint64_t)pud | PTE_VALID | PTE_TABLE;
    } else {
        pud = (uint64_t*)(pgd[idx] & PAGE_MASK);
    }

    // L1
    idx = (va >> L1_SHIFT) & (PT_ENTRIES - 1);
    if (!(pud[idx] & PTE_VALID)) {
        if (!create) return NULL;
        pmd = pt_alloc_level();
        pud[idx] = (uint64_t)pmd | PTE_VALID | PTE_TABLE;
    } else {
        pmd = (uint64_t*)(pud[idx] & PAGE_MASK);
    }

    // L2
    idx = (va >> L2_SHIFT) & (PT_ENTRIES - 1);
    if (!(pmd[idx] & PTE_VALID)) {
        if (!create) return NULL;
        pte = pt_alloc_level();
        pmd[idx] = (uint64_t)pte | PTE_VALID | PTE_TABLE;
    } else {
        pte = (uint64_t*)(pmd[idx] & PAGE_MASK);
    }

    // L3
    idx = (va >> L3_SHIFT) & (PT_ENTRIES - 1);
    if (!(pte[idx] & PTE_VALID)) {
        if (!create) return NULL;
        uint64_t page = phys_alloc_page();
        pte[idx] = page | PTE_VALID | PTE_PAGE | PTE_AF | PTE_SH_INNER | PTE_USER | PTE_RW;  // Default RW
    }

    return &pte[idx];
}

/* Walk page table and free all levels */
static void pt_free(uint64_t *l0) {
    for (int i = 0; i < PT_ENTRIES; i++) {
        if (l0[i] & PTE_VALID) {
            uint64_t *l1 = (uint64_t*)(l0[i] & PAGE_MASK);
            for (int j = 0; j < PT_ENTRIES; j++) {
                if (l1[j] & PTE_VALID) {
                    uint64_t *l2 = (uint64_t*)(l1[j] & PAGE_MASK);
                    for (int k = 0; k < PT_ENTRIES; k++) {
                        if (l2[k] & PTE_VALID) {
                            uint64_t *l3 = (uint64_t*)(l2[k] & PAGE_MASK);
                            for (int m = 0; m < PT_ENTRIES; m++) {
                                if (l3[m] & PTE_VALID && !(l3[m] & PTE_TABLE)) {
                                    phys_free_page(l3[m] & PAGE_MASK);
                                }
                            }
                            kfree(l3);
                        }
                    }
                    kfree(l2);
                }
            }
            kfree(l1);
        }
    }
    kfree(l0);
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
    uint64_t *new_l0 = pt_alloc_level();
    if (!new_l0) return -1;

    memcpy(new_l0 + 256, parent->pgtable_l0 + 256, 256 * 8);  // Kernel

    // COW user pages (lower half)
    for (int i = 0; i < 256; i++) {
        if (parent->pgtable_l0[i] & PTE_VALID) {
            uint64_t *old_l1 = (uint64_t*)(parent->pgtable_l0[i] & PAGE_MASK);
            uint64_t *new_l1 = pt_alloc_level();
            new_l0[i] = (uint64_t)new_l1 | PTE_VALID | PTE_TABLE;

            for (int j = 0; j < PT_ENTRIES; j++) {
                if (old_l1[j] & PTE_VALID) {
                    if (old_l1[j] & PTE_TABLE) {
                        // Recurse lower levels (L2/L3)
                        uint64_t *old_l2 = (uint64_t*)(old_l1[j] & PAGE_MASK);
                        uint64_t *new_l2 = pt_alloc_level();
                        old_l1[j] = (uint64_t)new_l2 | PTE_VALID | PTE_TABLE;

                        for (int k = 0; k < PT_ENTRIES; k++) {
                            if (old_l2[k] & PTE_VALID) {
                                if (old_l2[k] & PTE_TABLE) {
                                    // L3
                                    uint64_t *old_l3 = (uint64_t*)(old_l2[k] & PAGE_MASK);
                                    uint64_t *new_l3 = pt_alloc_level();
                                    old_l2[k] = (uint64_t)new_l3 | PTE_VALID | PTE_TABLE;

                                    for (int m = 0; m < PT_ENTRIES; m++) {
                                        if (old_l3[m] & PTE_VALID) {
                                            new_l3[m]