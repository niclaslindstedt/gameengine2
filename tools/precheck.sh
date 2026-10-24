#!/usr/bin/env bash
# CI's cheap gates on this tree, before the push (`make precheck`): the checks that cost the most
# CI rounds, each a minute or less here, cheapest first. A red gate does not stop the ones after
# it, so one round shows everything to fix; only a failed build stops (nothing after it can run).
# Each red gate's whole log is printed, up to 400 lines (every lint finding in every file).
#
#   1. fmt-check     clang-format over every source
#   2. agents-check  the links, the router, the line cap, the ge: tags, the lessons
#   3. libm-check    no <cmath> transcendental in simulation code
#   4. release       the changeset fragments valid
#   5. build         the preset's build (incremental)
#   6. lint          clang-tidy over what the change since BASE (default origin/main) reaches
#   7. tests         cli_diagnostics_test (every message held to the diagnostics convention),
#                    each changed test, and the test every changed file's ge:test tag names
#   8. digests       each lab the change reaches (`ge digest --affected --list`) at its smoke flags,
#                    against tests/labs_digests.txt: a digest the change moved is caught here,
#                    not by CI's labs_smoke a round later
#
# It is not the whole of CI: the other compilers, the labs it does not reach and the GPU rows
# still run there. A game repo has `ge precheck` instead (engine/cli/ci.hpp).
#
#   tools/precheck.sh BUILD_DIR PRESET [BASE_REF]
#
# ge:summary CI's cheap gates before a push: format, agents, libm, lint, tests, the reached labs
# ge:keywords ci precheck gates fmt-check agents-check libm-check lint changed-files affected-tests
# ge:keywords reached-labs digests
# ge:skill commit
set -euo pipefail
cd "$(dirname "$0")/.."

build="${1:?usage: tools/precheck.sh BUILD_DIR PRESET [BASE_REF]}"
preset="${2:?usage: tools/precheck.sh BUILD_DIR PRESET [BASE_REF]}"
base="${3:-origin/main}"
start=$SECONDS

log="$build/precheck.log"
mkdir -p "$build"
: >"$log"
failed=()

# One gate: its output appended to $log, and shown only when it fails.
gate() {
  local name="$1"
  shift
  local t=$SECONDS one="$build/precheck-gate.log"
  if ! "$@" >"$one" 2>&1; then
    local lines
    lines=$(wc -l <"$one")
    head -n 400 "$one" >&2
    ((lines > 400)) && echo "... $((lines - 400)) more lines in $log" >&2
    echo "precheck: $name failed (above)." >&2
    failed+=("$name")
    cat "$one" >>"$log"
    return 1
  fi
  cat "$one" >>"$log"
  echo "precheck: $name ok ($((SECONDS - t)) s)"
}

gate fmt-check make -s fmt-check || true
gate agents-check make -s agents-check || true
gate libm-check make -s libm-check || true
gate release python3 tools/release.py check || true
# A union merge can keep two rows of one lab in the digests' baseline; CI's labs-smoke refuses it.
doubled_digests() {
  local d
  d=$(grep -v '^#' tests/labs_digests.txt | awk '{print $1}' | sort | uniq -d)
  [[ -z "$d" ]] && return 0
  echo "tests/labs_digests.txt holds more than one row for: $d. Keep the row this build writes" \
    "(ge digest <lab>) and delete the other."
  return 1
}
gate "digest rows" doubled_digests || true
if ! gate build make -s build PRESET="$preset"; then
  echo "precheck: the build failed, so the lint, the tests and the digests could not run;" \
    "red: ${failed[*]}. Fix them, then make precheck again." >&2
  exit 1
fi
gate lint tools/lint.sh "$build" "$base" || true

# The tests the change reaches: changed tests, the ge:test of every changed file, and every test
# whose target the change reaches through the include graph (`ge test --affected --list`: a plugin
# change reaches the game's tests through the game library including its headers). The labs'
# smoke slices are left to the digests below.
merge_base="$(git merge-base "$base" HEAD 2>/dev/null || echo HEAD)"
mapfile -t changed < <(git diff --name-only --diff-filter=d "$merge_base" --; git ls-files --others --exclude-standard)
tests=(cli_diagnostics_test)
for f in "${changed[@]}"; do
  [[ -f "$f" ]] || continue
  if [[ "$f" == tests/*_test.cpp ]]; then tests+=("$(basename "$f" .cpp)"); fi
  while read -r t; do
    [[ "$t" == tests/*_test.cpp ]] && tests+=("$(basename "$t" .cpp)")
  done < <(sed -n 's/^.*ge:test \(tests\/[^ ]*_test\.cpp\).*$/\1/p' "$f" 2>/dev/null | head -5)
done
while read -r t; do
  [[ "$t" =~ ^[a-z0-9_]+_test$ ]] && tests+=("$t")
done < <("$build/ge" test --affected --list --preset="$preset" --base="$base" 2>/dev/null || true)
mapfile -t tests < <(printf '%s\n' "${tests[@]}" | sort -u)
regex="^($(IFS='|'; echo "${tests[*]}"))\$"
gate "tests (${#tests[@]})" ctest --test-dir "$build" -R "$regex" -j "$(nproc 2>/dev/null || sysctl -n hw.ncpu)" \
  --output-on-failure || true

# The labs the change reaches, each against its committed digest, side by side. As in CTest's
# labs_smoke (tests/CMakeLists.txt), the leak check is off: the software GPU driver's worker
# threads keep allocations past exit, which LeakSanitizer would report as the lab's.
labs_digests() {
  printf '%s\n' "$@" | ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_leaks=0" \
    xargs -P "$(nproc 2>/dev/null || sysctl -n hw.ncpu)" -I{} \
    "$build/labs" {} --smoke --quiet --check-digests=tests/labs_digests.txt
}
mapfile -t labs < <("$build/ge" digest --affected --list --preset="$preset" --base="$base" 2>/dev/null |
  grep -E '^[a-z0-9-]+$' || true)
if ((${#labs[@]})); then
  gate "digests (${labs[*]})" labs_digests "${labs[@]}" || true
fi
if ((${#failed[@]})); then
  echo "precheck: red: ${failed[*]} (each above; every gate's output in $log). Fix them, then" \
    "make precheck again." >&2
  exit 1
fi
echo "precheck: CI's cheap gates pass ($((SECONDS - start)) s); push, and CI runs the rest."
