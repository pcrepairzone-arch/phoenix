/*
 * select.c – I/O multiplexing for RISC OS Phoenix
 * Implements select() and poll() with POSIX semantics
 * Author: R Andrews Grok 4 – 26 Nov 2025
 */

#include "kernel.h"
#include "vfs.h"
#include <string.h>

/* Maximum file descriptors per task */
#define MAX_FD 1024

/* fd_set operations */
#define FD_ZERO(set) memset(set, 0, sizeof(fd_set))
#define FD_SET(fd, set) ((set)->fds_bits[fd / 8] |= (1 << (fd % 8)))
#define FD_CLR(fd, set) ((set)->fds_bits[fd / 8] &= ~(1 << (fd % 8)))
#define FD_ISSET(fd, set) ((set)->fds_bits[fd / 8] & (1 << (fd % 8)))

/* select() – wait for I/O readiness on multiple fds */
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
    task_t *task = current_task;
    int ready = 0;
    uint64_t timeout_ns = timeout ? timeout->tv_sec * 1000000000ULL + timeout->tv_usec * 1000ULL : 0;
    timer_t timer;

    if (nfds > MAX_FD) nfds = MAX_FD;

    // Fast poll check
    for (int fd = 0; fd < nfds; fd++) {
        file_t *file = task_get_file(task, fd);
        if (!file) continue;

        int events = file->f_ops->poll ? file->f_ops->poll(file) : 0;

        if (readfds && FD_ISSET(fd, readfds) && (events & POLLIN)) ready++;
        if (writefds && FD_ISSET(fd, writefds) && (events & POLLOUT)) ready++;
        if (exceptfds && FD_ISSET(fd, exceptfds) && (events & (POLLERR | POLLHUP))) ready++;
    }

    if (ready || !timeout_ns) return ready;

    // Set up timeout timer
    if (timeout_ns) {
        timer_init(&timer, timeout_callback, task);
        timer_schedule(&timer, timeout_ns / 1000000);
    }

    // Block until event or timeout
    task_block(TASK_BLOCKED);
    schedule();

    // Check again after wake
    ready = 0;
    FD_ZERO(readfds); FD_ZERO(writefds); FD_ZERO(exceptfds);

    for (int fd = 0; fd < nfds; fd++) {
        file_t *file = task_get_file(task, fd);
        if (!file) continue;

        int events = file->f_ops->poll ? file->f_ops->poll(file) : 0;

        if (events & POLLIN) { FD_SET(fd, readfds); ready++; }
        if (events & POLLOUT) { FD_SET(fd, writefds); ready++; }
        if (events & (POLLERR | POLLHUP)) { FD_SET(fd, exceptfds); ready++; }
    }

    return ready;
}

/* poll() – scalable alternative to select() */
int poll(struct pollfd *fds, nfds_t nfds, int timeout_ms) {
    task_t *task = current_task;
    int ready = 0;
    uint64_t timeout_ns = timeout_ms >= 0 ? timeout_ms * 1000000ULL : 0;
    timer_t timer;

    // Fast poll check
    for (nfds_t i = 0; i < nfds; i++) {
        int fd = fds[i].fd;
        if (fd < 0 || fd >= MAX_FD || !task->files[fd]) {
            fds[i].revents = POLLNVAL;
            ready++;
            continue;
        }

        file_t *file = task->files[fd];
        int events = file->f_ops->poll ? file->f_ops->poll(file) : 0;

        fds[i].revents = fds[i].events & events;
        if (fds[i].revents) ready++;
    }

    if (ready || timeout_ms == 0) return ready;

    // Set up timeout timer
    if (timeout_ms > 0) {
        timer_init(&timer, timeout_callback, task);
        timer_schedule(&timer, timeout_ms);
    }

    // Block until event or timeout
    task_block(TASK_BLOCKED);
    schedule();

    // Check again after wake
    ready = 0;
    for (nfds_t i = 0; i < nfds; i++) {
        int fd = fds[i].fd;
        if (fd < 0 || fd >= MAX_FD || !task->files[fd]) {
            fds[i].revents = POLLNVAL;
            ready++;
            continue;
        }

        file_t *file = task->files[fd];
        int events = file->f_ops->poll ? file->f_ops->poll(file) : 0;

        fds[i].revents = fds[i].events & events;
        if (fds[i].revents) ready++;
    }

    return ready;
}

/* Timeout callback – wake blocked task */
static void timeout_callback(timer_t *timer) {
    task_t *task = timer->private;
    task_wakeup(task);
}