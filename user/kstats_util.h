#ifndef XV6_KSTATS_UTIL_H
#define XV6_KSTATS_UTIL_H
#include "kernel/kstats.h"

static inline int
ks_pid(char *s)
{
  int n = 0;
  if (!*s)
    return -1;
  for (; *s; s++) {
    if (*s < '0' || *s > '9' || n > (2147483647 - (*s - '0')) / 10)
      return -1;
    n = n * 10 + *s - '0';
  }
  return n > 0 ? n : -1;
}
static inline uint64
ks_percent(struct kstats_proc *p)
{
  uint64 elapsed = p->snapshot_tick - p->start_tick;
  return elapsed ? 100 * p->cpu_ticks / elapsed : 0;
}
static inline char *
ks_state(int state)
{
  static char *names[] = {"UNUSED",   "USED",    "SLEEPING",
                          "RUNNABLE", "RUNNING", "ZOMBIE"};
  return state >= 0 && state < 6 ? names[state] : "UNKNOWN";
}
static inline char *
ks_callname(int n)
{
  static char *names[KS_SYSCALLS] = {
    "unknown", "fork",  "exit",   "wait",   "pipe",    "read",     "kill",
    "exec",    "fstat", "chdir",  "dup",    "getpid",  "sbrk",     "pause",
    "uptime",  "open",  "write",  "mknod",  "unlink",  "link",     "mkdir",
    "close",   "sync",  "kstats", "kprocs", "kevents", "ksyscalls"};
  return n >= 0 && n < KS_SYSCALLS ? names[n] : "unknown";
}
#endif
