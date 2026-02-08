/*
 * pipe.c – UNIX pipes for RISC OS Phoenix
 * Implements pipe(2), read/write, poll for pipes
 * Uses ring buffer for bidirectional communication
 * Author:R Andrews Grok 4 – 26 Nov 2025
 */

#include "kernel.h"
#include "vfs.h"
#include "spinlock.h"
#include <stdint.h>
#include <string.h>

#define PIPE_BUFFER_SIZE 4096

typedef struct pipe_buffer {
    uint8_t data[PIPE_BUFFER_SIZE];
    size_t read_pos;
    size_t write_pos;
    size_t count;
    spinlock_t lock;
    task_t *read_waiter;
    task_t *write_waiter;
} pipe_buffer_t;

typedef struct pipe_file {
    pipe_buffer_t *pipe;
    int is_reader;
} pipe_file_t;

/* Create pipe – returns two file descriptors */
int pipe(int pipefd[2]) {
    pipe_buffer_t *pipe = kmalloc(sizeof(pipe_buffer_t));
    if (!pipe) return -1;

    memset(pipe, 0, sizeof(*pipe));
    spinlock_init(&pipe->lock);

    file_t *read_file = vfs_alloc_file();
    if (!read_file) { kfree(pipe); return -1; }
    read_file->f_ops = &pipe_ops;
    read_file->private = kmalloc(sizeof(pipe_file_t));
    ((pipe_file_t*)read_file->private)->pipe = pipe;
    ((pipe_file_t*)read_file->private)->is_reader = 1;

    file_t *write_file = vfs_alloc_file();
    if (!write_file) { vfs_free_file(read_file); kfree(pipe); return -1; }
    write_file->f_ops = &pipe_ops;
    write_file->private = kmalloc(sizeof(pipe_file_t));
    ((pipe_file_t*)write_file->private)->pipe = pipe;
    ((pipe_file_t*)write_file->private)->is_reader = 0;

    pipe->read_waiter = current_task;
    pipe->write_waiter = current_task;

    int read_fd = alloc_fd(read_file);
    int write_fd = alloc_fd(write_file);
    if (read_fd < 0 || write_fd < 0) {
        // Cleanup on fail
        vfs_free_file(read_file);
        vfs_free_file(write_file);
        kfree(pipe);
        return -1;
    }

    pipefd[0] = read_fd;
    pipefd[1] = write_fd;

    debug_print("Pipe created: FD %d -> %d\n", read_fd, write_fd);
    return 0;
}

/* Read from pipe */
ssize_t pipe_read(file_t *file, char *buf, size_t count) {
    pipe_file_t *pfile = file->private;
    pipe_buffer_t *pipe = pfile->pipe;

    unsigned long flags;
    spin_lock_irqsave(&pipe->lock, flags);

    while (pipe->count == 0) {
        if (file->f_flags & O_NONBLOCK) {
            spin_unlock_irqrestore(&pipe->lock, flags);
            return -1;  // EAGAIN
        }

        pipe->read_waiter = current_task;
        spin_unlock_irqrestore(&pipe->lock, flags);
        task_block(TASK_BLOCKED);
        spin_lock_irqsave(&pipe->lock, flags);
    }

    size_t to_read = count < pipe->count ? count : pipe->count;
    size_t read = 0;

    while (read < to_read) {
        size_t avail = PIPE_BUFFER_SIZE - pipe->read_pos;
        size_t chunk = avail > to_read - read ? to_read - read : avail;
        memcpy(buf + read, pipe->data + pipe->read_pos, chunk);
        pipe->read_pos = (pipe->read_pos + chunk) % PIPE_BUFFER_SIZE;
        pipe->count -= chunk;
        read += chunk;

        // Wake writer if space now available
        if (pipe->write_waiter && pipe->count < PIPE_BUFFER_SIZE - 1) {
            task_wakeup(pipe->write_waiter);
            pipe->write_waiter = NULL;
        }
    }

    spin_unlock_irqrestore(&pipe->lock, flags);
    return read;
}

/* Write to pipe */
ssize_t pipe_write(file_t *file, const char *buf, size_t count) {
    pipe_file_t *pfile = file->private;
    pipe_buffer_t *pipe = pfile->pipe;

    unsigned long flags;
    spin_lock_irqsave(&pipe->lock, flags);

    while (pipe->count == PIPE_BUFFER_SIZE) {
        if (file->f_flags & O_NONBLOCK) {
            spin_unlock_irqrestore(&pipe->lock, flags);
            return -1;  // EAGAIN
        }

        pipe->write_waiter = current_task;
        spin_unlock_irqrestore(&pipe->lock, flags);
        task_block(TASK_BLOCKED);
        spin_lock_irqsave(&pipe->lock, flags);
    }

    size_t to_write = count < PIPE_BUFFER_SIZE - pipe->count ? count : PIPE_BUFFER_SIZE - pipe->count;
    size_t written = 0;

    while (written < to_write) {
        size_t avail = PIPE_BUFFER_SIZE - pipe->write_pos;
        size_t chunk = avail > to_write - written ? to_write - written : avail;
        memcpy(pipe->data + pipe->write_pos, buf + written, chunk);
        pipe->write_pos = (pipe->write_pos + chunk) % PIPE_BUFFER_SIZE;
        pipe->count += chunk;
        written += chunk;

        // Wake reader if data available
        if (pipe->read_waiter && pipe->count > 0) {
            task_wakeup(pipe->read_waiter);
            pipe->read_waiter = NULL;
        }
    }

    spin_unlock_irqrestore(&pipe->lock, flags);
    return written;
}

/* Poll pipe */
int pipe_poll(file_t *file) {
    pipe_file_t *pfile = file->private;
    pipe_buffer_t *pipe = pfile->pipe;

    unsigned long flags;
    spin_lock_irqsave(&pipe->lock, flags);

    int events = 0;
    if (pfile->is_reader) {
        if (pipe->count > 0) events |= POLLIN;
    } else {
        if (pipe->count < PIPE_BUFFER_SIZE) events |= POLLOUT;
    }

    spin_unlock_irqrestore(&pipe->lock, flags);
    return events;
}

/* Close pipe end */
void pipe_close(file_t *file) {
    pipe_file_t *pfile = file->private;
    pipe_buffer_t *pipe = pfile->pipe;

    // Flush waiter
    unsigned long flags;
    spin_lock_irqsave(&pipe->lock, flags);
    if (pfile->is_reader && pipe->write_waiter) {
        task_wakeup(pipe->write_waiter);
    } else if (!pfile->is_reader && pipe->read_waiter) {
        task_wakeup(pipe->read_waiter);
    }

    // If both ends closed, free buffer
    // (Stub – track refcount)

    spin_unlock_irqrestore(&pipe->lock, flags);
    kfree(pfile);
}

/* Pipe operations */
file_ops_t pipe_ops = {
    .read = pipe_read,
    .write = pipe_write,
    .poll = pipe_poll,
    .close = pipe_close,
    .seek = NULL
};