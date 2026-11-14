#ifndef XV6_KSTATS_H
#define XV6_KSTATS_H

// Shared kernel/user ABI. Include types.h first. All times are unsigned ticks
// except boot measurements, which use raw RISC-V time-counter units.
#define KS_VERSION  1
#define KS_EVENTS   64
#define KS_HARTS    8
#define KS_STAGES   9
#define KS_SYSCALLS 27
#define KS_PROCS    64
#define KS_CREATE   1
#define KS_EXIT     2
#define KS_REAP     3

struct kstats_boot {
  char name[24];
  uint64 time;
};
struct kstats_global {
  uint64 version, uptime, boot_window, boot_sum;
  struct kstats_boot boot[KS_STAGES];
  uint64 created, exited, reaped, sleeps, wakeups;
  uint64 faults, loads, stores, resolved, failed;
  uint64 interrupts, hart[KS_HARTS];
  uint64 dispatches, syscalls, invalid, failures;
  uint64 top_dispatches;
  int top_pid, slowest;
};
struct kstats_proc {
  int pid, ppid, state, last_hart;
  char name[16];
  uint64 start_tick, snapshot_tick, cpu_ticks;
  uint64 sleeps, wakeups, sleep_ticks;
  uint64 loads, stores, resolved, failed, last_fault;
  uint64 size, peak, grows, shrinks;
  uint64 dispatches, syscalls, invalid, failures;
};
struct kstats_event {
  uint64 sequence, tick;
  int pid, type;
};
struct kstats_syscalls {
  uint64 frequency[KS_SYSCALLS], failures[KS_SYSCALLS];
  int pid, top;
};
#endif
