# Kernel design and snapshot ABI

The implementation adds monitoring to the existing xv6 operations; scheduler,
filesystem and virtual-memory policies remain upstream's. Kernel hooks and
shared accounting are in `kernel/kstats.c`; the ABI is `kernel/kstats.h`.
No separate loadable modules or replacement kernel are involved.

## Ownership and concurrency

`wait_lock -> p->lock -> kstats lock` is the allowed order. The KStats lock is a
leaf: no process/tick lock, allocation, copyout, sleeping, or printing occurs
while it is held. Existing timer paths may hold `tickslock` before taking
process locks through wakeup; hooks never acquire `tickslock` in reverse.

Process identity/state is pinned by `p->lock`; PPID uses `wait_lock` first.
Monitoring fields, including mirrored process name and virtual size, are
protected by the monitoring lock. Mirroring avoids reading fields that upstream
otherwise treats as private to a running process. Timer sampling takes the
monitoring lock once per interrupt and does not acquire a process lock.

Global counters and the event ring are consistent under the monitoring lock.
Process snapshots are individually consistent rows scanned over time; they do
not freeze the scheduler or promise one atomic system-wide process-table view.
The most-dispatched active PID is scanned before the global snapshot, so its
count cannot exceed the subsequent global total merely due to scan timing.
Boot records become immutable before secondary HARTs start scheduling.

All locks are released before user copying, including copyout to lazy pages.
The event syscall takes a one-page temporary snapshot to avoid overflowing a
kernel stack. Other snapshots use bounded local records. No kernel addresses
or internal pointers are exported; last-fault addresses are user virtual addresses.

## Syscalls

The pinned baseline ends at `SYS_sync=22`. Added numbers are:

| Number | C interface | Successful result |
| --- | --- | --- |
| 23 | `int kstats(struct kstats_global *out)` | 0 |
| 24 | `int kprocs(struct kstats_proc *out, int capacity)` | Number of rows copied, up to capacity |
| 25 | `int kevents(struct kstats_event *out, int capacity)` | Number of newest events copied, oldest first |
| 26 | `int ksyscalls(int pid, struct kstats_syscalls *out)` | 0 for an allocated PID |

The shared version is 1. Maximum process capacity is 64, event capacity 64,
HARTs 8, and syscall slots 27 (index 0 reserved). Kernel compile-time checks
reject incompatible NPROC/NCPU or syscall table changes rather than silently
truncating the ABI. Each snapshot object must use the shared struct definition.

Negative/excessive capacities, invalid/missing PIDs, out-of-range/null pointers,
read-only output pages, and allocation/copy failures return -1. Zero-capacity
process/event requests return 0 without accessing a pointer. Output bounds are
checked against the current process's size without pointer-addition overflow;
copyout enforces page permissions. A failed multi-page/row copy can leave a
partial result in the buffer: callers must discard output when return is -1.

Every valid syscall is counted before invoking its handler, including exit and
exec. Unknown attempts get separate global/per-PID counters. Failure buckets
use the pinned handlers' exact -1 error convention; exit (nonreturning), getpid,
uptime and sync are excluded from failure classification. In particular,
uptime's unsigned tick value is never misclassified as an error on signed wrap.
Most-used syscall ties select the lowest numbered bucket.

## Module implementation map

| Module and extension | Hooks/data | Repeatable coverage in kstest |
| --- | --- | --- |
| 1 Boot timing, slowest stage and shares | hart-0 main, r_time, nine stages | Positive durations, sum/window, maximum/share checks; host reboot checks |
| 2 Creation/exit/reap and bounded history | userinit, successful kfork, kexit, successful kwait | Exact PID sequence/totals, fork exhaustion, ring wrap and ordered sequence numbers |
| 3 Sleep/wake and sleeping ticks | Actual sleep transition, wakeup, kill-sleeper | Pause duration, pipe-blocked kill, global/per-PID deltas |
| 4 Load/store resolved/failed per PID and last VA | Existing usertrap vmfault call, exactly once | Lazy load/store plus invalid load/store in separate children; inspect zombies before reap |
| 5 Uptime and per-HART interrupts | Original clockintr and ticks update | Tick progress, sum identity, each configured HART active |
| 6 PID/PPID/name/state, filtering and state summaries | Locked process scan and psx | Self/child state snapshots; host CLI filtering and invalid-PID checks |
| 7 CPU samples and utilization | One sample per clockintr with current process | CPU-bound versus sleeping children over ten ticks, zero elapsed guard |
| 8 Virtual size, peak and growth/shrink | Successful fork/exec/eager/lazy sbrk | Exact size deltas, operation counts, retained peak, slot reuse |
| 9 Dispatch counts, last HART and top PID | Scheduler before original swtch | Repeated sleep/resume, valid HART, global/top invariants |
| 10 Syscall valid/unknown/frequencies/top/errors | Existing dispatcher entry and handler result | Exact getpid/open/close buckets, unknown ecall, top selection, global/per-PID deltas |

Additional tests check buffer capacities/canaries, null/read-only/out-of-range
outputs, lazy output allocation, 40 slot reuses, and four concurrent workers.
Host tests run unchanged user commands and reboot each tested HART configuration.
