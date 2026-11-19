#!/usr/bin/env bash
#
# mayhem/test.sh — RUN this repo's OWN functional test suite (already built by mayhem/build.sh).
# exit 0 = pass. EDIT per repo. PATCH-grade oracle: after an agent patches the source, the grader
# rebuilds (build.sh) then runs this. DELETE this file if the repo has no meaningful tests.
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

# Upstream's Catch2 suite (test/test_main.cpp: string/file utils, JWT, StreamUri, Metadata, Properties,
# Librespot log parsing, Error/ErrorOr, wildcard matching, AuthInfo), built by mayhem/build.sh with the
# project's normal flags into bin/snapcast_test. Every test case asserts values (REQUIREs).
runner="$SRC/bin/snapcast_test"
if [ ! -x "$runner" ]; then
  echo "test.sh: $runner missing — mayhem/build.sh must build it" >&2
  emit_ctrf "catch2" 0 1 0
  exit 1
fi

out="$("$runner" 2>&1)"; rc=$?
printf '%s\n' "$out" | tail -n 20
# Catch2 v3 summary: "All tests passed (N assertions in T test cases)" or
# "test cases: T | P passed | F failed [| S skipped]"
passed=0; failed=0; skipped=0
if line="$(printf '%s\n' "$out" | grep -E '^All tests passed \(.* in [0-9]+ test cases?\)')"; then
  passed="$(printf '%s\n' "$line" | sed -E 's/.* in ([0-9]+) test cases?\).*/\1/')"
elif line="$(printf '%s\n' "$out" | grep -E '^test cases: ')"; then
  passed="$(printf '%s\n' "$line" | grep -oE '[0-9]+ passed' | grep -oE '[0-9]+' || echo 0)"
  failed="$(printf '%s\n' "$line" | grep -oE '[0-9]+ failed' | grep -oE '[0-9]+' || echo 0)"
  skipped="$(printf '%s\n' "$line" | grep -oE '[0-9]+ skipped' | grep -oE '[0-9]+' || echo 0)"
else
  echo "test.sh: no Catch2 summary in the output" >&2
  failed=1
fi
# A non-zero exit with nothing counted as failed is still a failure.
if [ "$rc" -ne 0 ] && [ "$failed" -eq 0 ]; then failed=1; fi

# Upstream CI's "Stream removal regression" (.github/workflows/ci.yml unit-test job): starts the real
# snapserver on 127.0.0.1 only, adds/removes streams over JSON-RPC and asserts every result and the
# final stream list. Counted as one test: passed iff it exits 0.
server="$SRC/bin/snapserver"
if [ -x "$server" ] && ASAN_OPTIONS=detect_leaks=0 python3 "$SRC/test/stream_removal_test.py" "$server"; then
  passed=$(( passed + 1 ))
else
  echo "test.sh: test/stream_removal_test.py failed (or $server missing)" >&2
  failed=$(( failed + 1 ))
fi
emit_ctrf "catch2+stream_removal_test" "$passed" "$failed" "$skipped"
