#!/bin/bash
# Usage: tools/run_mutants.sh <dir made by tools/mutate.py>
# Builds and tests every mutant copy. A mutant is KILLED when make fails (a test failed or
# ThreadSanitizer reported a race) and SURVIVED when make passes.
base=$1
[ -d "$base" ] || { echo "no such folder: $base"; exit 2; }
survived=0
killed=0
for d in "$base"/*/; do
  name=$(basename "$d")
  cd "$d" || continue
  if [ "$name" = "m5_release_without_lock" ]; then
    make tsan > run.log 2>&1
  else
    make test > run.log 2>&1
  fi
  code=$?
  if [ $code -ne 0 ]; then
    killed=$((killed + 1))
    echo "KILLED   $name: $(grep -m1 -E 'FAIL |FAILED|WARNING: ThreadSanitizer|terminate called' run.log)"
  else
    survived=$((survived + 1))
    echo "SURVIVED $name"
  fi
  cd - > /dev/null || exit 2
done
echo "killed=$killed survived=$survived"
[ $survived -eq 0 ]
