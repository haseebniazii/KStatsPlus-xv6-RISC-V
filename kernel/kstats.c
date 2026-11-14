#include "types.h"
#include "param.h"
#include "riscv.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "syscall.h"

#if NCPU > KS_HARTS || NPROC != KS_PROCS
#error Update the shared KStats ABI limits to match param.h
#endif

// Lock order: wait_lock -> p->lock -> lock. Never acquire any other lock
// while holding this lock. In particular, do not call copyout or printk here.
static struct spinlock lock;
static struct kstats_global g;
static struct kstats_event events[KS_EVENTS];
static uint64 sequence;
static char *stages[KS_STAGES] = {
  "Allocator",   "Virtual memory",       "Process table",
  "Traps",       "Interrupt controller", "Buffer cache",
  "Inode table", "File table",           "VirtIO device"};

void
ks_init(void)
{
  initlock(&lock, "kstats");
  g.version = KS_VERSION;
  for (int i = 0; i < KS_STAGES; i++)
    safestrcpy(g.boot[i].name, stages[i], sizeof(g.boot[i].name));
}

void
ks_boot(int stage, uint64 start)
{
  uint64 elapsed = r_time() - start;
  g.boot[stage].time = elapsed; // hart 0 only, before other harts start
  g.boot_sum += elapsed;
  if (elapsed > g.boot[g.slowest].time)
    g.slowest = stage;
}

void
ks_boot_end(uint64 start)
{
  g.boot_window = r_time() - start;
}

void
ks_reset(struct proc *p)
{
  acquire(&lock);
  memset(&p->mon, 0, sizeof(p->mon));
  memset(&p->calls, 0, sizeof(p->calls));
  p->mon.start_tick = g.uptime;
  p->mon.last_hart = -1;
  p->sleep_start = 0;
  release(&lock);
}

void
ks_name(struct proc *p, char *name)
{
  acquire(&lock);
  safestrcpy(p->mon.name, name, sizeof(p->mon.name));
  release(&lock);
}

void
ks_size(struct proc *p, uint64 size, int initial)
{
  acquire(&lock);
  if (!initial) {
    if (size > p->mon.size)
      p->mon.grows++;
    if (size < p->mon.size)
      p->mon.shrinks++;
  }
  p->mon.size = size;
  if (size > p->mon.peak)
    p->mon.peak = size;
  release(&lock);
}

void
ks_event(struct proc *p, int type)
{
  acquire(&lock);
  if (type == KS_CREATE)
    g.created++;
  if (type == KS_EXIT)
    g.exited++;
  if (type == KS_REAP)
    g.reaped++;
  struct kstats_event *e = &events[sequence % KS_EVENTS];
  e->sequence = ++sequence;
  e->tick = g.uptime;
  e->pid = p->pid;
  e->type = type;
  release(&lock);
}

void
ks_sleep(struct proc *p)
{
  acquire(&lock);
  g.sleeps++;
  p->mon.sleeps++;
  p->sleep_start = g.uptime;
  release(&lock);
}

void
ks_wake(struct proc *p)
{
  acquire(&lock);
  g.wakeups++;
  p->mon.wakeups++;
  p->mon.sleep_ticks += g.uptime - p->sleep_start;
  release(&lock);
}

void
ks_clock(uint tick)
{
  acquire(&lock);
  g.uptime = tick;
  release(&lock);
}

void
ks_timer(struct proc *p, int hart)
{
  acquire(&lock);
  g.interrupts++;
  g.hart[hart]++;
  if (p)
    p->mon.cpu_ticks++; // exactly once per timer, user or kernel mode
  release(&lock);
}

void
ks_dispatch(struct proc *p, int hart)
{
  acquire(&lock);
  g.dispatches++;
  p->mon.dispatches++;
  p->mon.last_hart = hart;
  release(&lock);
}

void
ks_fault(struct proc *p, int load, uint64 addr, int resolved)
{
  acquire(&lock);
  g.faults++;
  if (load) {
    g.loads++;
    p->mon.loads++;
  } else {
    g.stores++;
    p->mon.stores++;
  }
  if (resolved) {
    g.resolved++;
    p->mon.resolved++;
  } else {
    g.failed++;
    p->mon.failed++;
  }
  p->mon.last_fault = addr;
  release(&lock);
}

void
ks_call(struct proc *p, int num)
{
  acquire(&lock);
  if (num > 0 && num < KS_SYSCALLS) {
    g.syscalls++;
    p->mon.syscalls++;
    p->calls.frequency[num]++;
  } else {
    g.invalid++;
    p->mon.invalid++;
  }
  release(&lock);
}

void
ks_result(struct proc *p, int num, uint64 result)
{
  // These calls do not report errors via -1 (exit never returns). All other
  // handlers in this pinned revision use precisely -1, including sbrk.
  if (num == SYS_exit || num == SYS_getpid || num == SYS_uptime ||
      num == SYS_sync || result != (uint64)-1)
    return;
  acquire(&lock);
  g.failures++;
  p->mon.failures++;
  p->calls.failures[num]++;
  release(&lock);
}

// Caller holds p->lock; parent identity is supplied separately by proc.c.
void
ks_read_proc(struct proc *p, struct kstats_proc *out)
{
  acquire(&lock);
  *out = p->mon;
  out->snapshot_tick = g.uptime;
  if (p->state == SLEEPING)
    out->sleep_ticks += g.uptime - p->sleep_start;
  release(&lock);
  out->pid = p->pid;
  out->state = p->state;
}

// Check the entire advertised output region before any copying. copyout
// still checks page permissions and handles lazy allocation outside locks.
static int
output_ok(uint64 addr, uint64 len)
{
  struct proc *p = myproc();
  return addr != 0 && addr < p->sz && len <= p->sz - addr;
}

uint64
sys_kstats(void)
{
  uint64 addr;
  struct kstats_global out;
  argaddr(0, &addr);
  if (!output_ok(addr, sizeof(out)))
    return -1;
  int top_pid;
  uint64 top_dispatches;
  ks_top(&top_pid, &top_dispatches);
  acquire(&tickslock);
  uint now = ticks;
  release(&tickslock);
  acquire(&lock);
  out = g;
  release(&lock);
  out.uptime = now;
  out.top_pid = top_pid;
  out.top_dispatches = top_dispatches;
  struct proc *p = myproc();
  return copyout(p->pagetable, p->sz, addr, (char *)&out, sizeof(out));
}

uint64
sys_kprocs(void)
{
  uint64 addr;
  int capacity;
  argaddr(0, &addr);
  argint(1, &capacity);
  if (capacity < 0 || capacity > NPROC)
    return -1;
  if (capacity == 0)
    return 0;
  if (!output_ok(addr, (uint64)capacity * sizeof(struct kstats_proc)))
    return -1;
  return ks_procs(addr, capacity);
}

uint64
sys_kevents(void)
{
  uint64 addr;
  int capacity;
  argaddr(0, &addr);
  argint(1, &capacity);
  if (capacity < 0 || capacity > KS_EVENTS)
    return -1;
  if (capacity == 0)
    return 0;
  if (!output_ok(addr, (uint64)capacity * sizeof(struct kstats_event)))
    return -1;
  // The complete ring fits in one page; avoid using most of a kernel stack.
  struct kstats_event *out = (struct kstats_event *)kalloc();
  if (!out)
    return -1;
  acquire(&lock);
  int n = sequence < (uint64)capacity ? sequence : capacity;
  for (int i = 0; i < n; i++)
    out[i] = events[(sequence - n + i) % KS_EVENTS];
  release(&lock);
  struct proc *p = myproc();
  int result =
    copyout(p->pagetable, p->sz, addr, (char *)out, n * sizeof(*out));
  kfree(out);
  return result < 0 ? -1 : n;
}

// Snapshot per-syscall buckets while the caller pins p->pid with p->lock.
void
ks_read_calls(struct proc *p, struct kstats_syscalls *out)
{
  acquire(&lock);
  *out = p->calls;
  release(&lock);
  out->pid = p->pid;
  out->top = 0;
  for (int i = 1; i < KS_SYSCALLS; i++)
    if (out->frequency[i] > out->frequency[out->top])
      out->top = i;
}

uint64
sys_ksyscalls(void)
{
  uint64 addr;
  int pid;
  argint(0, &pid);
  argaddr(1, &addr);
  if (pid <= 0 || !output_ok(addr, sizeof(struct kstats_syscalls)))
    return -1;
  return ks_calls(pid, addr);
}
