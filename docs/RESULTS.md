# KStats+ actual validation results

Validation date: **2026-11-14 (Asia/Karachi, UTC+05:00)**.
Tested implementation commit: `ebfb59052d347ee00f0fffce3dea574f75e2fbca`.
Original upstream commit: `fa2ddcb7ca87a77af579380a6eafd5aff163b44f`.
The subsequent delivery commit adds documentation/evidence and selects the same
verified regression wrappers in CI; it does not alter the tested kernel/user code.

The existing root README remains a design specification and was preserved
byte-for-byte. This document records implemented behavior and actual results.

## Commands and results

Executed from the KStatsPlus checkout after activating the prepared toolchain:

```sh
bash tests/validate.sh
```

The complete command returned **exit 0**. It executed the following serially:

| Check | Actual result | Evidence |
| --- | --- | --- |
| `make clean`, `make -j4 kernel/kernel fs.img` | PASS, fresh kernel and all user programs built | [build](evidence/clean-build.log) |
| Shell commands, kstest, reboot with `CPUS=1` | PASS, 88 feature assertions | [1 HART](evidence/features-1hart.log) |
| Shell commands, kstest, reboot with `CPUS=3` | PASS, 90 feature assertions | [3 HARTs](evidence/features-3hart.log) |
| Shell commands, kstest, reboot with `CPUS=8` | PASS, 95 feature assertions | [8 HARTs](evidence/features-8hart.log) |
| Original `./test-xv6.py usertests` through cleanup wrapper | PASS, 75 enabled cases, ALL TESTS PASSED, exit 0; 199.3 seconds | [usertests](evidence/usertests.log) |
| Original `./test-xv6.py crash` through cleanup wrapper | PASS, 3 recovery checks, exit 0; 15.4 seconds | [crash recovery](evidence/crash.log) |
| clang-format 22.1.8 dry-run on kernel/user headers and C files and mkfs | PASS, exit 0 | Version recorded below |
| `git diff --check` | PASS | No whitespace errors |
| Root README byte comparison against baseline commit | PASS | SHA-256 below |

QEMU used its `virt` machine, 128 MiB RAM, VirtIO block device, `-bios none`,
and the Makefile's unmodified scheduling/VM settings. Upstream regressions used
the default **3 HARTs**. Feature checks total **273 parent-process assertions**
across the three configurations, plus shell-output, argument-validation, and
reboot checks in the host runner. Only the upstream commented-out
`linkoverflow` test is disabled; it was not enabled or represented as passed.
There are no failed or unresolved required checks in this validation run.

All required shell commands were exercised: `kstats`, `kstats boot`,
`kstats faults`, `kstats cpu`, `kstats calls 1`, `psx`, `psx -p 1`, `klog`,
and `kstest` with each tested HART count. Invalid PID/argument forms were also
checked. A second boot per configuration verified fresh boot measurements,
initial creation counts, and reset unknown/error totals.

## Ten-module acceptance record

Every row includes both the core feature and its mini-extension bundle.
See [implementation details](IMPLEMENTATION.md) for code locations and ABI.

| Module | Status | Actual acceptance evidence |
| --- | --- | --- |
| 1 Boot-Time Profiler | PASS | All nine durations positive; sum/window relationship, slowest stage and integer shares verified; reboot measured again |
| 2 Process Lifecycle Monitor | PASS | Exact successful create/exit/reap deltas and PID event order; full table failed fork produces no creation event; 64-entry ring wraps with ordered sequences/timestamps |
| 3 Sleep/Wakeup Activity Monitor | PASS | Global/per-PID counts, pause sleeping ticks, and kill waking a pipe-blocked child |
| 4 Page Fault Counter | PASS | Resolved lazy load/store faults; invalid load and store in children; last addresses, per-PID categories and retained global totals |
| 5 Kernel Tick & Uptime Monitor | PASS | Uptime progress, interrupt sum identity, and activity on all configured HARTs (1, 3, 8) |
| 6 Process Information Monitor | PASS | PID/PPID/name/state snapshots, one-PID CLI filter and displayed-state summary, missing/invalid PID handling |
| 7 CPU Time Monitor | PASS | CPU-bound child has more sampled running ticks and utilization than sleeping child; zero elapsed guard |
| 8 Process Memory Usage Monitor | PASS | Exact eager/lazy virtual-size growth, operation counts, shrinking and retained peak, independent fork/reused-slot initialization |
| 9 Context Switch Monitor | PASS | Repeated resumes increase dispatch counts; valid last HART; top active PID and global dispatch invariants |
| 10 System Call Usage Monitor | PASS | Exact getpid/open/close frequency/failure deltas, most-used syscall, unknown ecall and global/per-PID error accounting |

Additional boundary/stress checks cover negative/excessive capacities, null,
read-only and out-of-range pointers, one-row canaries, snapshots into lazy user
pages, zero capacities, nonexistent PIDs, 40 process-slot reuses, full process
table exhaustion, and four concurrent syscall/memory/snapshot workers.

## Toolchain

```text
riscv64-linux-gnu-gcc (Debian 14.2.0-19) 14.2.0
QEMU emulator version 10.0.13 (Debian 1:10.0.13+ds-0+deb13u1)
GNU Make 4.4.1
Python 3.12.14
clang-format version 22.1.8
```

## Measurement limits and scope

No remaining failures were observed in the required checks. CPU utilization is
approximate timer sampling; virtual size is not resident RAM; boot units remain
raw time-counter units; scheduler dispatches are not exact hardware switch-edge
counts. Per-process rows are snapshots taken over time, not an atomic freeze of
all processes. The most-dispatched PID considers allocated (including zombie)
process slots, and per-PID details disappear after reaping. The event ring keeps
only the newest 64 entries, while global totals persist until reboot. Fault
counts cover user load/store traps only, excluding kernel copyin/copyout lazy
resolution. Diagnostics count their own syscall entry. None of these limits
changes the finalized monitoring scope.

Validation ran on this Linux cloud/QEMU environment; physical RISC-V hardware
and non-Linux host execution were not tested. GitHub Actions execution is separate
from these local results and is not claimed as passed here.

Root `README.md` SHA-256 (same as baseline):
`d691b9cadbca49b92475258b8fa88f4ef2d8ce9efa2f9d187d269d50d7523831`.

See [user manual](USER_MANUAL.md), [raw summary](evidence/validation-summary.log),
and [future results template](RESULTS_TEMPLATE.md).
