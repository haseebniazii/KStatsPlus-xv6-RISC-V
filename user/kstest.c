#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "user/user.h"
#include "user/kstats_util.h"

static struct kstats_global g0, g1;
static struct kstats_proc rows[KS_PROCS], a, b;
static struct kstats_syscalls c0, c1;
static struct kstats_event events[KS_EVENTS];
static int checks, self;

static void
check(int condition, char *name)
{
  if (!condition) {
    printf("KSTEST FAIL: %s\n", name);
    exit(1);
  }
  printf("ok %d - %s\n", ++checks, name);
}
static int
find(int pid, struct kstats_proc *out)
{
  int n = kprocs(rows, KS_PROCS);
  if (n < 0)
    return 0;
  for (int i = 0; i < n; i++)
    if (rows[i].pid == pid) {
      *out = rows[i];
      return 1;
    }
  return 0;
}
static void
mine(struct kstats_proc *p)
{
  if (!find(self, p)) {
    printf("KSTEST FAIL: self snapshot\n");
    exit(1);
  }
}
static void
zombie(int pid, struct kstats_proc *p)
{
  for (int i = 0; i < 30; i++) {
    if (find(pid, p) && p->state == 5)
      return;
    pause(1);
  }
  printf("KSTEST FAIL: child did not become zombie\n");
  exit(1);
}
static void
reap(int pid, int status)
{
  int actual = -100;
  check(wait(&actual) == pid && actual == status, "child exit status and reap");
}
static uint64
unknown(void)
{
  register uint64 a7 asm("a7") = 9999;
  register uint64 a0 asm("a0");
  asm volatile("ecall" : "=r"(a0) : "r"(a7) : "memory");
  return a0;
}

int
main(int argc, char **argv)
{
  int expected_harts = argc == 2 ? ks_pid(argv[1]) : 1;
  if (argc > 2 || expected_harts < 1 || expected_harts > KS_HARTS) {
    fprintf(2, "usage: kstest [expected-harts:1..8]\n");
    exit(1);
  }
  self = getpid();
  check(kstats(&g0) == 0 && g0.version == KS_VERSION, "snapshot ABI version");
  uint64 sum = 0, shares = 0;
  int slowest = 0;
  for (int i = 0; i < KS_STAGES; i++) {
    check(g0.boot[i].time > 0 && g0.boot[i].name[0], "measured boot stage");
    sum += g0.boot[i].time;
    if (g0.boot[i].time > g0.boot[slowest].time)
      slowest = i;
    shares += g0.boot_sum ? 100 * g0.boot[i].time / g0.boot_sum : 0;
  }
  check(sum == g0.boot_sum && sum <= g0.boot_window,
        "boot stage sum and boot window");
  check(g0.slowest == slowest && shares <= 100 && shares > 100 - KS_STAGES,
        "slowest stage and integer measured-stage percentages");

  mine(&a);
  check(a.pid == self && a.ppid > 0 && !strcmp(a.name, "kstest") &&
          a.state == 4,
        "PID PPID name and RUNNING state");
  kstats(&g0);
  int pid = fork();
  if (pid == 0)
    exit(23);
  check(pid > 0, "fork workload");
  reap(pid, 23);
  kstats(&g1);
  check(g1.created == g0.created + 1 && g1.exited == g0.exited + 1 &&
          g1.reaped == g0.reaped + 1,
        "successful create exit reap totals");
  int n = kevents(events, KS_EVENTS), seen = 0;
  for (int i = 0; i < n; i++)
    if (events[i].pid == pid) {
      check(events[i].type == ++seen, "PID lifecycle event order");
    }
  check(seen == 3, "complete PID lifecycle history");

  mine(&a);
  pause(3);
  mine(&b);
  check(b.sleeps > a.sleeps && b.wakeups > a.wakeups &&
          b.sleep_ticks >= a.sleep_ticks + 3,
        "per-PID sleep wake counts and sleeping ticks");
  kstats(&g1);
  check(g1.sleeps > g0.sleeps && g1.wakeups > g0.wakeups,
        "global sleep wake counters");
  int sleepgate[2];
  check(pipe(sleepgate) == 0, "sleeping child pipe");
  pid = fork();
  if (pid == 0) {
    char token;
    close(sleepgate[1]);
    read(sleepgate[0], &token, 1);
    exit(0);
  }
  check(pid > 0, "sleeping child creation");
  for (int tries = 0; tries < 20; tries++) {
    if (find(pid, &a) && a.state == 2)
      break;
    pause(1);
  }
  check(find(pid, &a) && a.state == 2, "sleeping child state snapshot");
  check(kill(pid) == 0, "kill sleeping child");
  zombie(pid, &b);
  check(b.wakeups > a.wakeups && b.sleep_ticks >= a.sleep_ticks,
        "kill-induced wakeup accounted");
  reap(pid, -1);
  close(sleepgate[0]);
  close(sleepgate[1]);

  mine(&a);
  char *memory = sbrklazy(8192);
  check(memory != SBRK_ERROR, "lazy virtual allocation");
  volatile char *v = memory;
  check(v[0] == 0, "lazy load is zero-filled");
  v[4096] = 7;
  mine(&b);
  check(b.loads > a.loads && b.stores > a.stores &&
          b.resolved >= a.resolved + 2,
        "per-PID resolved load/store faults");
  check(b.last_fault == (uint64)(memory + 4096), "last resolved fault address");
  check(sbrk(-8192) != SBRK_ERROR, "release lazy virtual memory");
  kstats(&g0);
  for (int store = 0; store < 2; store++) {
    pid = fork();
    if (pid == 0) {
      volatile char *bad = (char *)0x4000000000UL;
      if (store)
        *bad = 1;
      else {
        volatile char x = *bad;
        (void)x;
      }
      exit(77);
    }
    check(pid > 0, "invalid-access child creation");
    zombie(pid, &a);
    check(a.failed == 1 && a.resolved == 0 && a.last_fault == 0x4000000000UL &&
            (store ? a.stores == 1 : a.loads == 1),
          "per-PID failed fault classification");
    reap(pid, -1);
  }
  kstats(&g1);
  check(g1.failed >= g0.failed + 2 && g1.faults == g1.loads + g1.stores &&
          g1.faults == g1.resolved + g1.failed,
        "global fault totals survive child reaping");

  kstats(&g0);
  pause(3);
  kstats(&g1);
  sum = 0;
  for (int i = 0; i < KS_HARTS; i++)
    sum += g1.hart[i];
  check(g1.uptime >= g0.uptime + 3 && g1.interrupts > g0.interrupts &&
          sum == g1.interrupts,
        "uptime and exact per-HART interrupt sum");
  for (int i = 0; i < expected_harts; i++)
    check(g1.hart[i] > 0, "configured HART serviced timer interrupts");

  // Compare two children over the same 10-tick window, one running and one asleep.
  int pipes[2][2], children[2];
  for (int i = 0; i < 2; i++) {
    check(pipe(pipes[i]) == 0, "CPU comparison pipe");
    children[i] = fork();
    if (children[i] == 0) {
      int me = getpid();
      int start = uptime();
      if (i == 0)
        while ((uint)(uptime() - start) < 10)
          getpid();
      else
        pause(10);
      if (!find(me, &a) || write(pipes[i][1], &a, sizeof(a)) != sizeof(a))
        exit(1);
      exit(0);
    }
    check(children[i] > 0, "CPU comparison child");
  }
  check(read(pipes[0][0], &a, sizeof(a)) == sizeof(a) &&
          read(pipes[1][0], &b, sizeof(b)) == sizeof(b),
        "CPU sample snapshots");
  check(a.cpu_ticks > b.cpu_ticks && a.cpu_ticks > 0 &&
          ks_percent(&a) > ks_percent(&b),
        "CPU-bound child exceeds sleeping child sampled CPU and utilization");
  for (int i = 0; i < 2; i++) {
    close(pipes[i][0]);
    close(pipes[i][1]);
    int status;
    int done = wait(&status);
    check((done == children[0] || done == children[1]) && status == 0,
          "CPU comparison child completed");
  }
  memset(&a, 0, sizeof(a));
  check(ks_percent(&a) == 0, "zero elapsed CPU utilization guard");

  mine(&a);
  check(sbrk(4096) != SBRK_ERROR && sbrklazy(8192) != SBRK_ERROR,
        "eager and lazy memory growth");
  mine(&b);
  check(b.size == a.size + 12288 && b.peak >= b.size && b.grows == a.grows + 2,
        "virtual size peak and successful growth counts");
  uint64 peak = b.peak;
  check(sbrk(-12288) != SBRK_ERROR, "memory shrink");
  mine(&b);
  check(b.size == a.size && b.peak == peak && b.shrinks == a.shrinks + 1,
        "shrinking preserves lifetime peak");
  mine(&a);
  for (int i = 0; i < 3; i++)
    pause(1);
  mine(&b);
  kstats(&g1);
  check(b.dispatches >= a.dispatches + 3 && b.last_hart >= 0 &&
          b.last_hart < KS_HARTS,
        "scheduler dispatches and valid last HART");
  check(g1.top_pid > 0 && g1.top_dispatches > 0 &&
          g1.dispatches >= g1.top_dispatches,
        "global dispatch total and most-dispatched active PID");

  mine(&a);
  kstats(&g0);
  check(ksyscalls(self, &c0) == 0, "syscall frequency snapshot");
  getpid();
  getpid();
  getpid();
  check(open("/missing-kstats-file", O_RDONLY) == -1 && close(-1) == -1,
        "controlled syscall failures");
  check(unknown() == (uint64)-1,
        "unknown syscall preserves original error return");
  check(ksyscalls(self, &c1) == 0, "second syscall frequency snapshot");
  check(c1.frequency[SYS_getpid] == c0.frequency[SYS_getpid] + 3 &&
          c1.frequency[SYS_open] == c0.frequency[SYS_open] + 1 &&
          c1.failures[SYS_open] == c0.failures[SYS_open] + 1 &&
          c1.failures[SYS_close] == c0.failures[SYS_close] + 1 &&
          c1.failures[SYS_getpid] == 0,
        "exact per-syscall frequencies and error buckets");
  int top = 0;
  for (int i = 1; i < KS_SYSCALLS; i++)
    if (c1.frequency[i] > c1.frequency[top])
      top = i;
  check(c1.top == top, "most-used syscall selection");
  mine(&b);
  kstats(&g1);
  check(b.invalid == a.invalid + 1 && b.failures >= a.failures + 2 &&
          g1.invalid == g0.invalid + 1 && g1.failures >= g0.failures + 2,
        "per-PID and global unknown/error totals");

  check(kstats(0) == -1 && kstats((void *)0x4000000000UL) == -1 &&
          kstats((void *)main) == -1,
        "null out-of-range and read-only snapshot pointers");
  check(kprocs(rows, -1) == -1 && kprocs(rows, KS_PROCS + 1) == -1 &&
          kevents(events, -1) == -1 && kevents(events, KS_EVENTS + 1) == -1,
        "negative and excessive buffer capacities");
  check(kprocs(0, 0) == 0 && kevents(0, 0) == 0, "zero-capacity snapshots");
  check(ksyscalls(-1, &c0) == -1 && ksyscalls(2147483647, &c0) == -1,
        "invalid and nonexistent PIDs");
  check(kstats((void *)(sbrk(0) - 1)) == -1 && kprocs(0, 1) == -1 &&
          kevents(0, 1) == -1 && ksyscalls(self, 0) == -1,
        "bounded output regions");
  struct {
    uint64 left;
    struct kstats_proc row;
    uint64 right;
  } guard;
  guard.left = 123;
  guard.right = 456;
  check(kprocs(&guard.row, 1) == 1 && guard.left == 123 && guard.right == 456,
        "one-row snapshot cannot overrun caller buffer");

  for (int i = 0; i < 40; i++) {
    pid = fork();
    if (pid == 0) {
      int me = getpid();
      if (ksyscalls(me, &c0) || c0.frequency[SYS_fork] || c0.failures[SYS_open])
        exit(1);
      if (!find(me, &a) || a.invalid || a.peak != a.size)
        exit(1);
      exit(0);
    }
    if (pid < 0) {
      printf("KSTEST FAIL: slot reuse fork\n");
      exit(1);
    }
    int status;
    if (wait(&status) != pid || status) {
      printf("KSTEST FAIL: reused slot counters\n");
      exit(1);
    }
  }
  check(1, "40 reused child slots start with independent accounting");
  n = kevents(events, KS_EVENTS);
  check(n == KS_EVENTS && events[0].sequence > 1,
        "bounded lifecycle ring overwrites oldest records");
  for (int i = 1; i < n; i++)
    if (events[i].sequence != events[i - 1].sequence + 1 ||
        events[i].tick < events[i - 1].tick) {
      printf("KSTEST FAIL: event sequence\n");
      exit(1);
    }
  check(1, "event ring sequence and timestamps remain ordered after wrap");

  // Hold children on a pipe until the process table is exhausted. Failed
  // allocation must not produce a successful CREATE counter or event.
  int gate[2], held = 0;
  check(pipe(gate) == 0, "process exhaustion gate");
  for (int i = 0; i <= KS_PROCS; i++) {
    pid = fork();
    if (pid == 0) {
      char token;
      close(gate[1]);
      int count = read(gate[0], &token, 1);
      exit(count == 1 ? 0 : 1);
    }
    if (pid < 0)
      break;
    held++;
  }
  check(pid == -1 && held > 0, "process table exhaustion returns fork failure");
  kstats(&g0);
  n = kevents(events, KS_EVENTS);
  uint64 last_sequence = events[n - 1].sequence;
  check(fork() == -1, "repeat fork at exhausted capacity");
  kstats(&g1);
  n = kevents(events, KS_EVENTS);
  check(events[n - 1].sequence == last_sequence && g1.created == g0.created &&
          g1.exited == g0.exited && g1.reaped == g0.reaped,
        "failed fork does not create lifecycle events or totals");
  for (int i = 0; i < held; i++) {
    if (write(gate[1], "x", 1) != 1)
      exit(1);
  }
  close(gate[0]);
  close(gate[1]);
  for (int i = 0; i < held; i++) {
    int status;
    if (wait(&status) <= 0 || status)
      exit(1);
  }
  check(1, "all exhaustion children released and reaped");

  // copyout may fault in these user pages; it must run without monitor locks.
  char *lazyout = sbrklazy(16384);
  check(lazyout != SBRK_ERROR && kprocs((void *)lazyout, KS_PROCS) > 0,
        "snapshot into lazily allocated user pages");
  check(sbrk(-16384) != SBRK_ERROR, "release lazy snapshot buffer");

  for (int i = 0; i < 4; i++) {
    pid = fork();
    if (pid == 0) {
      for (int j = 0; j < 200; j++) {
        getpid();
        if (j % 20 == 0) {
          if (sbrk(4096) == SBRK_ERROR || sbrk(-4096) == SBRK_ERROR)
            exit(1);
          if (kstats(&g0) || kprocs(rows, KS_PROCS) < 1 ||
              kevents(events, KS_EVENTS) < 1)
            exit(1);
        }
      }
      exit(0);
    }
    check(pid > 0, "concurrent monitor worker started");
  }
  for (int i = 0; i < 4; i++) {
    int status;
    check(wait(&status) > 0 && status == 0,
          "concurrent monitor worker completed");
  }
  printf("KSTEST: ALL TESTS PASSED (%d checks)\n", checks);
  exit(0);
}
