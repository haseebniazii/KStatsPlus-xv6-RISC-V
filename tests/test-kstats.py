#!/usr/bin/env python3
"""Boot xv6, exercise real commands and kstest, then reboot and check reset.

Uses only Python's standard library. Build first, then run from repository root:
  python3 tests/test-kstats.py --cpus 3 --log docs/evidence/features-3hart.log
"""
import argparse
import os
from pathlib import Path
import re
import signal
import subprocess
import time


class Console:
    def __init__(self, cpus, log):
        self.log = log
        self.data = ""
        self.offset = 0
        self.proc = subprocess.Popen(
            ["make", f"CPUS={cpus}", "qemu"], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
        os.set_blocking(self.proc.stdout.fileno(), False)

    def until(self, pattern, timeout=60):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                chunk = os.read(self.proc.stdout.fileno(), 65536)
            except BlockingIOError:
                chunk = b""
            if chunk:
                text = chunk.decode("utf-8", "replace").replace("\r", "")
                self.log.write(text)
                self.log.flush()
                self.data += text
            pending = self.data[self.offset:]
            if "panic:" in pending or "KSTEST FAIL:" in pending:
                raise RuntimeError(pending[-3000:])
            match = re.search(pattern, pending, re.MULTILINE)
            if match:
                self.offset += match.end()
                return match
            if self.proc.poll() is not None:
                raise RuntimeError(f"QEMU stopped ({self.proc.returncode}): {pending[-3000:]}")
            time.sleep(0.02)
        raise TimeoutError(f"Missing {pattern!r}: {self.data[-3000:]}")

    def command(self, command, expected):
        self.proc.stdin.write((command + "\n").encode())
        self.proc.stdin.flush()
        match = self.until(expected)
        self.until(r"^\$ ")
        return match

    def close(self):
        if self.proc.poll() is None:
            self.proc.stdin.write(b"\x01x")
            self.proc.stdin.flush()
            self.proc.wait(timeout=10)
        if self.proc.returncode != 0:
            raise RuntimeError(f"QEMU make exit {self.proc.returncode}")

    def cleanup(self):
        try:
            os.killpg(self.proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        self.proc.wait()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cpus", type=int, choices=range(1, 9), default=3)
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args()
    args.log.parent.mkdir(parents=True, exist_ok=True)
    with args.log.open("w") as log:
        q = Console(args.cpus, log)
        try:
            q.until(r"init: starting sh")
            q.until(r"^\$ ")
            q.command("kstats", r"Valid syscalls=\d+ unknown=\d+ failures=\d+")
            boot = q.command("kstats boot", r"Measured stages: (\d+)\nBoot window: (\d+)")
            assert 0 < int(boot[1]) <= int(boot[2])
            q.command("kstats faults", r"User load/store faults: \d+ loads=\d+ stores=\d+")
            q.command("kstats cpu", r"HART 0: \d+ interrupts")
            q.command("psx", r"State summary \(displayed processes\):")
            q.command("psx -p 1", r"1 0 init SLEEPING[\s\S]*State summary \(displayed processes\): USED=0 SLEEPING=1 RUNNABLE=0 RUNNING=0 ZOMBIE=0")
            q.command("kstats calls 1", r"Most-used syscall: \w+")
            q.command("klog", r"\d+ \d+ 1 CREATE")
            q.command("psx -p abc", r"usage: psx")
            q.command("psx -p 2147483647", r"psx: PID not found")
            q.command("kstats calls -1", r"kstats: PID unavailable or invalid")
            result = q.command(f"kstest {args.cpus}", r"KSTEST: ALL TESTS PASSED \((\d+) checks\)")
            print(f"PASS: {args.cpus} HARTs, shell commands, {result[1]} kstest checks", flush=True)
            q.command("kstats", r"unknown=1 failures=\d+")
            q.close()
        finally:
            q.cleanup()
        # A new kernel must start new lifetime counters and boot measurements.
        q = Console(args.cpus, log)
        try:
            q.until(r"^\$ ")
            q.command("kstats", r"Processes created=3 exited=0 reaped=0")
            q.command("kstats", r"unknown=0 failures=0")
            boot = q.command("kstats boot", r"Measured stages: (\d+)\nBoot window: (\d+)")
            assert 0 < int(boot[1]) <= int(boot[2])
            q.close()
            print(f"PASS: {args.cpus} HARTs reboot resets lifetime counters and remeasures boot", flush=True)
        finally:
            q.cleanup()


if __name__ == "__main__":
    main()
