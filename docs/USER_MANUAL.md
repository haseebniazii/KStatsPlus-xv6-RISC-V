# KStats+ user manual

KStats+ extends the MIT xv6-RISC-V baseline recorded in `XV6_UPSTREAM.md`.
The root `README.md` remains the original specification, deliberately unchanged.
Implementation details and actual validation results live in this directory.

## Build and boot

On Debian/Ubuntu, install `build-essential`, `gcc-riscv64-linux-gnu`,
`binutils-riscv64-linux-gnu`, `qemu-system-riscv`, `perl`, `bc`, and `python3`.
QEMU must be at least 7.2. On the prepared cloud machine, first run:

```sh
. /workspace/xv6-tools/activate.sh
cd /workspace/KStatsPlus-xv6-RISC-V
```

Then use the normal xv6 workflow:

```sh
make -j4 kernel/kernel fs.img
make CPUS=3 qemu
```

`CPUS` can be 1 through 8. Wait for `init: starting sh` and `$`. Exit QEMU
with **Ctrl-a, then x**. `make clean` removes generated binaries and `fs.img`;
the guest filesystem's contents are lost when the image is rebuilt.

## Commands inside xv6

| Command | Output |
| --- | --- |
| `kstats` | Global boot, uptime, lifecycle, sleep, faults, interrupts, dispatches, syscall and error totals; most-dispatched active PID |
| `kstats boot` | Nine stage measurements in raw RISC-V time units, measured-stage percentages, slowest stage, and separate boot window |
| `kstats faults` | User load/store faults split into load/store and resolved/failed totals |
| `kstats cpu` | Global uptime ticks and individual HART interrupt totals |
| `kstats calls PID` | Per-syscall invocation/failure buckets and most-used syscall for a live or zombie PID |
| `psx` | PID, PPID, name, state, sampled CPU ticks/percent, virtual/peak KB, dispatches, last HART; sleep, fault, growth/shrink, and syscall details |
| `psx -p PID` | One matching PID; state summary covers only displayed rows |
| `klog` | Last 64 lifecycle events, oldest first, with sequence, tick, PID, and event type |
| `kstest` | Repeatable assertions covering all ten modules and extensions |
| `kstest 3` | Also require timer activity on HARTs 0, 1, and 2; use the actual configured HART count |

Invalid arguments and unavailable PIDs produce an error and nonzero exit status.
Use `psx` to discover a PID before requesting its detailed syscall buckets.
Snapshots are real observations; timings and counters vary between runs.

## Interpret measurements correctly

- Boot units are raw `r_time()` values. Shares use the sum of measured stages.
  The boot window includes surrounding initialization; later `fsinit()` is outside it.
- Global uptime follows hart 0's existing `ticks`; timer interrupts count every
  serviced interrupt on every HART. These quantities are distinct.
- CPU percent is `100 * sampled_running_ticks / elapsed_lifetime_ticks` (zero
  when elapsed is zero). Samples include executing kernel code for that process.
  Short tasks may have no samples; timer phase/skew makes this approximate.
- Virtual size is `p->sz`, including lazily reserved pages and the user layout's
  guard region. It is not resident physical RAM. Peak size lasts for the PID's
  lifetime, including across successful exec. Fork starts independent counters,
  with current/peak initialized to the child's inherited size.
- Growth/shrink counts increment once per successful size-changing operation,
  including exec and eager/lazy sbrk. No-op or failed operations do not count.
- Dispatches count scheduler selections, not all hardware context-switch edges.
  The most-dispatched PID is selected from currently allocated process slots.
- Lifecycle creation counts only successful fork and the initial process.
  Exit and reap are separate. The ring overwrites old entries without reducing
  lifetime totals. Failed allocations do not emit creation events.
- Per-PID counters reset on process-slot reuse; global totals last until reboot.
  Reaped PID details are gone, while bounded events and global totals remain.
- Diagnostic syscalls count themselves on entry. A syscall-error snapshot may
  exclude its own eventual failure because failure accounting happens on return.
- Fault coverage is user-mode scause 13/15 only. Kernel copyin/copyout lazy-page
  resolution and instruction faults are outside those counters.

## Reproduce validation

From the repository root, with the toolchain available:

```sh
bash tests/validate.sh
```

This performs a clean build, command/feature and reboot checks on 1, 3, and 8
HARTs, all enabled upstream usertests, and the three upstream crash-recovery
checks. Evidence is written under `docs/evidence/`. It takes several minutes.
Run suites serially: they share `fs.img`. Both Python helpers use only the
standard library and clean up the QEMU process groups they start.

Individual commands:

```sh
python3 tests/test-kstats.py --cpus 3 --log /tmp/features.log
python3 tests/run-upstream.py usertests --log /tmp/usertests.log
python3 tests/run-upstream.py crash --log /tmp/crash.log
```

Expected invalid-access children and disk-exhaustion tests intentionally print
fault/out-of-space messages. Use the runner's completion marker and exit status.
Upstream's commented-out `linkoverflow` case remains disabled.

For formatting, use clang-format 22.1.8 (the upstream config uses options that
older versions reject). CI installs this version explicitly. Formatting is
separate from the required build/runtime checks.
