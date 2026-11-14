#include "kernel/types.h"
#include "user/user.h"
#include "kernel/kstats.h"

static struct kstats_event events[KS_EVENTS];
int
main(int argc, char **argv)
{
  if (argc != 1) {
    fprintf(2, "usage: klog\n");
    exit(1);
  }
  int n = kevents(events, KS_EVENTS);
  if (n < 0) {
    fprintf(2, "klog: snapshot failed\n");
    exit(1);
  }
  printf("SEQ TICK PID EVENT (most recent %d entries)\n", KS_EVENTS);
  for (int i = 0; i < n; i++) {
    struct kstats_event *e = &events[i];
    printf("%lu %lu %d %s\n", e->sequence, e->tick, e->pid,
           e->type == KS_CREATE ? "CREATE"
           : e->type == KS_EXIT ? "EXIT"
                                : "REAP");
  }
  exit(0);
}
