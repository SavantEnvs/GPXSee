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

# GPXSee ships no test suite, so the oracle is a known-answer test over committed sample files:
# mayhem/out/gpxsee_dump (built by build.sh with the project's normal flags) opens each file
# through the same public entry points the GUI and the fuzz harnesses use (Data::Data,
# MapList::loadMaps + load/zoomFit/draw — mayhem/fuzz/exercise.h) and prints what it parsed:
# track/route/waypoint/area counts, point count, distance, times, graph sizes and waypoint names
# for track files; validity, name, lat/lon bounds and the number of pixels drawn at each zoom
# level for maps. Each line of mayhem/test/expected.txt is one case:
#   <-d|-u|-m> <sample path> => <expected summary>
# and passes only if gpxsee_dump's summary for that file is exactly the expected one.
DUMP="$SRC/mayhem/out/gpxsee_dump"
CASES="$SRC/mayhem/test/expected.txt"
passed=0; failed=0
if [ ! -x "$DUMP" ] || [ ! -s "$CASES" ]; then
  echo "test.sh: missing $DUMP or $CASES (build.sh should have built it)" >&2
  emit_ctrf "gpxsee-kat" 0 1 0
  exit 1
fi
while IFS= read -r line; do
  case "$line" in ''|'#'*) continue ;; esac
  mode="${line%% *}"; rest="${line#* }"
  path="${rest%% => *}"; want="${rest#* => }"
  got="$("$DUMP" "$mode" "$SRC/$path" 2>/dev/null)"; got="${got#*: }"
  if [ "$got" = "$want" ]; then
    passed=$((passed + 1))
  else
    failed=$((failed + 1))
    printf 'FAIL %s %s\n  want: %s\n  got:  %s\n' "$mode" "$path" "$want" "$got"
  fi
done < "$CASES"
echo "gpxsee-kat: $passed passed, $failed failed"
emit_ctrf "gpxsee-kat" "$passed" "$failed" 0
