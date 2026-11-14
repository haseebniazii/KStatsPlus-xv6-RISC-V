#include "kernel/types.h"
#include "user/user.h"
#include "user/kstats_util.h"

static struct kstats_proc rows[KS_PROCS];
int
main(int argc, char **argv)
{
  int pid = 0, states[6] = {0}, shown = 0;
  if (argc != 1) {
    if (argc != 3 || strcmp(argv[1], "-p") || (pid = ks_pid(argv[2])) < 0) {
      fprintf(2, "usage: psx [-p PID]\n");
      exit(1);
    }
  }
  int n = kprocs(rows, KS_PROCS);
  if (n < 0) {
    fprintf(2, "psx: snapshot failed\n");
    exit(1);
  }
  printf(
    "PID PPID NAME STATE CPU-TICKS CPU%% VIRT-KB PEAK-KB DISPATCHES HART\n");
  for (int i = 0; i < n; i++) {
    struct kstats_proc *p = &rows[i];
    if (pid && p->pid != pid)
      continue;
    shown++;
    if (p->state >= 0 && p->state < 6)
      states[p->state]++;
    printf("%d %d %s %s %lu %lu %lu %lu %lu %d\n", p->pid, p->ppid, p->name,
           ks_state(p->state), p->cpu_ticks, ks_percent(p), p->size / 1024,
           p->peak / 1024, p->dispatches, p->last_hart);
    printf("  sleeps=%lu wakes=%lu sleeping-ticks=%lu grows=%lu shrinks=%lu\n",
           p->sleeps, p->wakeups, p->sleep_ticks, p->grows, p->shrinks);
    printf("  faults load=%lu store=%lu resolved=%lu failed=%lu last=%p\n",
           p->loads, p->stores, p->resolved, p->failed, (void *)p->last_fault);
    printf("  syscalls=%lu unknown=%lu failures=%lu\n", p->syscalls, p->invalid,
           p->failures);
  }
  if (!shown && pid) {
    fprintf(2, "psx: PID not found\n");
    exit(1);
  }
  printf("State summary (displayed processes):");
  for (int i = 1; i < 6; i++)
    printf(" %s=%d", ks_state(i), states[i]);
  printf(
    "\nCPU%% is lifetime timer sampling; memory is virtual size, not resident RAM.\n");
  exit(0);
}
