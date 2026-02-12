/*
 * mmu.h – MMU function prototypes
 */

#ifndef MMU_H
#define MMU_H

void mmu_init(void);
void mmu_init_task(task_t *task);
int mmu_map(task_t *task, uint64_t virt, uint64_t size, int prot, int guard);
int mmu_duplicate_pagetable(task_t *parent, task_t *child);
void mmu_free_usermemory(task_t *task);
void data_abort_handler(uint64_t esr, uint64_t far);

#endif /* MMU_H */