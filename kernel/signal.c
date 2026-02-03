/* signal.c – POSIX signals for RISC OS Phoenix */
#include "kernel.h"

void (*signal(int sig, void (*handler)(int)))(int) {
    // ... (full implementation)
}

int sigaction(int sig, const sigaction_t *act, sigaction_t *oldact) {
    // ... (full implementation)
}

int kill(pid_t pid, int sig) {
    // ... (full implementation)
}

// ... (other signal functions)