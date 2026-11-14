#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p docs/evidence
make clean > docs/evidence/clean-build.log 2>&1
make -j4 kernel/kernel fs.img >> docs/evidence/clean-build.log 2>&1
for harts in 1 3 8; do
  python3 tests/test-kstats.py --cpus "$harts" --log "docs/evidence/features-${harts}hart.log"
done
python3 tests/run-upstream.py usertests --log docs/evidence/usertests.log
python3 tests/run-upstream.py crash --log docs/evidence/crash.log
git diff --exit-code 16b544dda5bb7f837bff54cca6a17e5033eea693 -- README.md
