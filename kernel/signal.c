/*
 * signal.c – POSIX signal handling for RISC OS Phoenix
 * Supports signal, sigaction, kill, sigreturn, raise
 * Added handlers for SIGTERM, SIGINT, SIGQUIT, SIGCHLD, SIGUSR1, SIGUSR2
 * Author: R Andrews Grok 4 – 26 Nov 2025
 */

#include "kernel.h"
#include "spinlock.h"
#include <stdint.h>
#include <string.h>

#define NSIG            32
#define SIG_BLOCK       1
#define SIG_UNBLOCK     2
#define SIG_SETMASK     3

#define SIG_DFL         ((void(*)(int))0)
#define SIG_IGN         ((void(*)(int))1)

/* Signal action structure */
typedef struct {
    void (*sa_handler)(int);
    uint64_t sa_mask;
    int      sa_flags;
} sigaction_t;

/* Per-task signal state */
typedef struct {
    void (*handlers[NSIG])(int);
    uint64_t pending;
    uint64_t blocked;
    uint64_t old_mask;      // For sigreturn
    uint64_t sigreturn_sp;  // Saved SP for sigreturn
} signal_state_t;

/* signal() – simple interface */
void (*signal(int sig, void (*handler)(int)))(int)
{
    if (sig < 1 || sig >= NSIG || sig == SIGKILL || sig == SIGSTOP)
        return SIG_ERR;

    task_t *task = current_task;
    void (*old)(int) = task->signal_state.handlers[sig];
    task->signal_state.handlers[sig] = handler;

    return old;
}

/* sigaction() – full control */
int sigaction(int sig, const sigaction_t *act, sigaction_t *oldact)
{
    if (sig < 1 || sig >= NSIG || sig == SIGKILL || sig == SIGSTOP)
        return -1;

    task_t *task = current_task;

    if (oldact) {
        oldact->sa_handler = task->signal_state.handlers[sig];
        oldact->sa_mask = task->signal_state.blocked;
        oldact->sa_flags = 0;
    }

    if (act) {
        task->signal_state.handlers[sig] = act->sa_handler;
        if (act->sa_flags & SA_SIGINFO)
            return -1;  // Not supported yet
    }

    return 0;
}

/* Send signal to task */
int kill(pid_t pid, int sig)
{
    if (sig < 1 || sig >= NSIG || pid <= 0)
        return -1;

    task_t *target = find_task_by_pid(pid);
    if (!target || target == current_task)
        return -1;

    if (sig == 0) return 0;  // Just check existence

    __atomic_or_fetch(&target->signal_state.pending, (1ULL << sig), __ATOMIC_SEQ_CST);
    task_wakeup(target);  // Unblock if sleeping

    debug_print("kill: sent signal %d to PID %d\n", sig, pid);
    return 0;
}

/* raise() – send signal to self */
int raise(int sig)
{
    return kill(current_task->pid, sig);
}

/* Deliver pending signals */
static void deliver_signals(void)
{
    task_t *task = current_task;
    uint64_t pending = task->signal_state.pending;
    uint64_t blocked = task->signal_state.blocked;

    pending &= ~blocked;
    if (!pending) return;

    /* Find first pending signal */
    int sig = __builtin_ctzll(pending);
    void (*handler)(int) = task->signal_state.handlers[sig];

    __atomic_and_fetch(&task->signal_state.pending, ~(1ULL << sig), __ATOMIC_SEQ_CST);

    if (handler == SIG_DFL) {
        debug_print("Signal %d: default action → terminate\n", sig);
        exit(128 + sig);
    }
    if (handler == SIG_IGN)
        return;

    debug_print("Delivering signal %d to handler 0x%llx\n", sig, (uint64_t)handler