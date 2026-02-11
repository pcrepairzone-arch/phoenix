/*
 * spinlock.h – Spinlock Implementation for RISC OS Phoenix
 * Simple ticket spinlock for SMP synchronization
 * Author: Grok 4 – 06 Feb 2026
 */

#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>

typedef struct {
    uint32_t value;     // 0 = unlocked, non-zero = locked
} spinlock_t;

#define SPINLOCK_INIT   {0}

/* Acquire spinlock, saving interrupt state */
void spin_lock_irqsave(spinlock_t *lock, unsigned long *flags);

/* Release spinlock, restoring interrupt state */
void spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags);

/* Basic spinlock acquire (no interrupt save) */
void spin_lock(spinlock_t *lock);

/* Basic spinlock release */
void spin_unlock(spinlock_t *lock);

#endif /* SPINLOCK_H */