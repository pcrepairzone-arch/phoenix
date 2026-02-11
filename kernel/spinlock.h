/*
 * spinlock.h – Spinlock headers
 */

#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>

typedef struct {
    uint32_t value;
} spinlock_t;

#define SPINLOCK_INIT {0}

void spin_lock_irqsave(spinlock_t *lock, unsigned long *flags);
void spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags);
void spin_lock(spinlock_t *lock);
void spin_unlock(spinlock_t *lock);

#endif /* SPINLOCK_H */