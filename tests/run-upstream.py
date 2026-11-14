#!/usr/bin/env python3
"""Run the unchanged upstream runner, retain output, and reap QEMU processes."""
import argparse
import os
from pathlib import Path
import re
import signal
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('target', choices=('usertests', 'crash'))
parser.add_argument('--log', type=Path, required=True)
args = parser.parse_args()
args.log.parent.mkdir(parents=True, exist_ok=True)
started = time.monotonic()
with args.log.open('w') as log:
    proc = subprocess.Popen(['./test-xv6.py', args.target], stdout=log,
                            stderr=subprocess.STDOUT, start_new_session=True)
    try:
        code = proc.wait(timeout=720)
    finally:
        # Upstream stop() terminates make; also clean up any surviving QEMU
        # children belonging to this invocation before another image is used.
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        proc.wait()
if code:
    print(args.log.read_text())
    raise SystemExit(code)
text = args.log.read_text()
if args.target == 'usertests':
    source = Path('user/usertests.c').read_text()
    sections = re.findall(r'(?:} quicktests|struct test slowtests)\[\] = \{(.*?)\n\};', source, re.S)
    expected = []
    for section in sections:
        expected.extend(re.findall(r'^\s*\{\w+,\s*"([^"\n]+)"\}', section, re.M))
    actual = re.findall(r'^test (\w+):', text, re.M)
    if len(sections) != 2 or len(actual) != len(expected) or set(actual) != set(expected) or 'ALL TESTS PASSED' not in text:
        raise RuntimeError('Incomplete upstream usertests run; inspect ' + str(args.log))
    print(f'PASS: {len(actual)} enabled upstream usertests; ALL TESTS PASSED; exit 0')
else:
    if len(re.findall(r'^OK$', text, re.M)) != 3:
        raise RuntimeError('Incomplete crash-recovery run; inspect ' + str(args.log))
    print('PASS: 3 upstream crash-recovery checks; exit 0')
print(f'{args.target} duration: {time.monotonic()-started:.1f}s; log: {args.log}')
