#include "kernel/types.h"
#include "user/user.h"
#include "user/kstats_util.h"

static struct kstats_global g;
static struct kstats_syscalls calls;

int
main(int argc, char **argv)
{
  if (argc == 3 && !strcmp(argv[1], "calls")) {
    int pid = ks_pid(argv[2]);
    if (pid < 0 || ksyscalls(pid, &calls) < 0) {
      fprintf(2, "kstats: PID unavailable or invalid\n");
      exit(1);
    }
    printf("PID %d syscall frequencies (failures)\n", pid);
    for (int i = 1; i < KS_SYSCALLS; i++)
      if (calls.frequency[i])
        printf("%s %lu (%lu)\n", ks_callname(i), calls.frequency[i],
               calls.failures[i]);
    printf("Most-used syscall: %s\n", ks_callname(calls.top));
    exit(0);
  }
  if (argc > 2 || (argc == 2 && strcmp(argv[1], "boot") &&
                   strcmp(argv[1], "faults") && strcmp(argv[1], "cpu"))) {
    fprintf(2, "usage: kstats [boot|faults|cpu|calls PID]\n");
    exit(1);
  }
  if (kstats(&g) < 0) {
    fprintf(2, "kstats: snapshot failed\n");
    exit(1);
  }
  if (argc == 2 && !strcmp(argv[1], "boot")) {
    printf("BOOT PROFILER - raw RISC-V time-counter units\n");
    for (int i = 0; i < KS_STAGES; i++)
      printf("%s: %lu (%lu%%)\n", g.boot[i].name, g.boot[i].time,
             g.boot_sum ? 100 * g.boot[i].time / g.boot_sum : 0);
    printf("Slowest stage: %s\nMeasured stages: %lu\nBoot window: %lu\n",
           g.boot[g.slowest].name, g.boot_sum, g.boot_window);
    printf(
      "Shares use measured stage sum; later fsinit is outside these stages.\n");
  } else if (argc == 2 && !strcmp(argv[1], "faults")) {
    printf(
      "User load/store faults: %lu loads=%lu stores=%lu resolved=%lu failed=%lu\n",
      g.faults, g.loads, g.stores, g.resolved, g.failed);
  } else if (argc == 2 && !strcmp(argv[1], "cpu")) {
    printf("Uptime: %lu ticks; timer interrupts: %lu\n", g.uptime,
           g.interrupts);
    for (int i = 0; i < KS_HARTS; i++)
      if (g.hart[i])
        printf("HART %d: %lu interrupts\n", i, g.hart[i]);
    printf("Per-HART interrupt sum is distinct from global uptime.\n");
  } else {
    printf("KStats+ - Kernel Monitoring Summary\n");
    printf("Boot window: %lu time-counter units\nUptime: %lu ticks\n",
           g.boot_window, g.uptime);
    printf("Processes created=%lu exited=%lu reaped=%lu\n", g.created, g.exited,
           g.reaped);
    printf("Sleep transitions=%lu wakeups=%lu\n", g.sleeps, g.wakeups);
    printf("User faults=%lu resolved=%lu failed=%lu\n", g.faults, g.resolved,
           g.failed);
    printf("Timer interrupts=%lu\nScheduler dispatches=%lu\n", g.interrupts,
           g.dispatches);
    printf("Most-dispatched active PID=%d dispatches=%lu\n", g.top_pid,
           g.top_dispatches);
    printf("Valid syscalls=%lu unknown=%lu failures=%lu\n", g.syscalls,
           g.invalid, g.failures);
  }
  exit(0);
}
