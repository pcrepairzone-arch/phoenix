/*
 * blockdriver.c – Block Device Driver Glue for RISC OS Phoenix
 * Integrates block devices (NVMe, USB, SATA, MMC) with VFS
 * Handles mounting, I/O dispatch, multi-device support
 * Author: Grok 4 – 06 Feb 2026
 */

#include "kernel.h"
#include "blockdev.h"
#include "vfs.h"
#include <string.h>

#define MAX_BLOCKDEVS   16

blockdev_t *blockdev_list[MAX_BLOCKDEVS];
int blockdev_count = 0;
spinlock_t blockdev_lock = SPINLOCK_INIT;

/* Register block device */
blockdev_t *blockdev_register(const char *name, uint64_t size, int block_size)
{
    unsigned long flags;
    spin_lock_irqsave(&blockdev_lock, flags);

    if (blockdev_count >= MAX_BLOCKDEVS) {
        spin_unlock_irqrestore(&blockdev_lock, flags);
        return NULL;
    }

    blockdev_t *dev = kmalloc(sizeof(blockdev_t));
    if (!dev) {
        spin_unlock_irqrestore(&blockdev_lock, flags);
        return NULL;
    }

    strncpy(dev->name, name, 15);
    dev->size = size;
    dev->block_size = block_size;
    dev->unit = blockdev_count;  // Auto-assign unit

    blockdev_list[blockdev_count++] = dev;

    spin_unlock_irqrestore(&blockdev_lock, flags);

    debug_print("BlockDev: %s registered (unit %d, %ld blocks)\n", name, dev->unit, size);
    return dev;
}

/* Get block device by name/unit */
blockdev_t *blockdev_get(const char *name, int unit)
{
    unsigned long flags;
    spin_lock_irqsave(&blockdev_lock, flags);

    for (int i = 0; i < blockdev_count; i++) {
        blockdev_t *dev = blockdev_list[i];
        if ((unit == -1 && strcmp(dev->name, name) == 0) ||
            (unit >= 0 && dev->unit == unit)) {
            spin_unlock_irqrestore(&blockdev_lock, flags);
            return dev;
        }
    }

    spin_unlock_irqrestore(&blockdev_lock, flags);
    return NULL;
}

/* VFS block read – dispatch to device */
ssize_t vfs_block_read(blockdev_t *dev, uint64_t lba, uint32_t count, void *buf)
{
    if (!dev || !dev->ops->read) return -1;

    ssize_t read = dev->ops->read(dev, lba, count, buf);
    if (read < 0) debug_print("Block read error on %s\n", dev->name);
    return read;
}

/* VFS block write */
ssize_t vfs_block_write(blockdev_t *dev, uint64_t lba, uint32_t count, const void *buf)
{
    if (!dev || !dev->ops->write) return -1;

    ssize_t written = dev->ops->write(dev, lba, count, buf);
    if (written < 0) debug_print("Block write error on %s\n", dev->name);
    return written;
}

/* VFS trim (deallocate) */
int vfs_block_trim(blockdev_t *dev, uint64_t lba, uint64_t count)
{
    if (!dev || !dev->ops->trim) return -1;
    return dev->ops->trim(dev, lba, count);
}

/* Poll block device */
int vfs_block_poll(blockdev_t *dev)
{
    if (!dev || !dev->ops->poll) return 0;
    return dev->ops->poll(dev);
}

/* Close block device */
void vfs_block_close(blockdev_t *dev)
{
    if (!dev || !dev->ops->close) return;
    dev->ops->close(dev);
}

/* Mount block device as FS (stub – integrate with FileCore) */
int blockdev_mount(blockdev_t *dev, const char *mountpoint)
{
    // Call FileCore_Mount or similar (stub)
    debug_print("Mounted %s at %s\n", dev->name, mountpoint);
    return 0;
}

/* Module init */
_kernel_oserror *module_init(const char *arg, int podule)
{
    debug_print("BlockDriver glue loaded\n");
    return NULL;
}