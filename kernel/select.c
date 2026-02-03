/* select.c – I/O multiplexing for RISC OS Phoenix */
#include "kernel.h"

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
    // ... (full implementation)
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout_ms) {
    // ... (full implementation)
}