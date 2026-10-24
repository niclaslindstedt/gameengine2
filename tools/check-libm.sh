#!/usr/bin/env bash
# Holds BITS, NOT CLOSENESS for transcendentals (CI's `agents` job, `make libm-check`):
# simulation code never calls the platform libm's sin/cos/exp/pow/..., whose bits differ
# between glibc, Apple and MSVC. It calls ge::core::libm (engine/core/libm/libm.hpp), whose
# bits are the same everywhere. Exact functions (sqrt, fabs, floor, ceil, trunc, fmod) are fine.
#
# Simulation code: engine/core (except engine/core/libm itself), engine/loop, engine/ai, engine/wind, engine/water, engine/move, engine/camera
# (its poses feed lab digests and goldens), every plugin
# (except a plugin's app/ directory, its presentation half), games/*/sim, and engine/lab and
# every labs/ directory, whose pictures and digests must match on every CI row, and engine/audio,
# whose rendered buffers are goldens and lab digests, and engine/net, whose simulated sessions are
# goldens too, and engine/console, whose cvars set simulation values. Renderer and UI code may use <cmath>: they
# feed no digest.
#
# ge:summary Fails any <cmath> transcendental in simulation code (make libm-check)
# ge:keywords ci check libm cmath transcendental determinism
# ge:skill determinism
set -euo pipefail
cd "$(dirname "$0")/.."

FNS='sin|cos|tan|asin|acos|atan|atan2|sinh|cosh|tanh|asinh|acosh|atanh|exp|exp2|expm1|log|log2|log10|log1p|pow|cbrt|erf|erfc|tgamma|lgamma'
FNS="($FNS)[fl]?"
# A call not qualified by another namespace (libm::sin is fine), std::/:: calls, std::hypot,
# compiler builtins, and `using std::sin`.
BAD="(^|[^A-Za-z0-9_:.>])(std::|::)?$FNS[[:space:]]*\(|std::hypot[fl]?[[:space:]]*\(|__builtin_$FNS[[:space:]]*\(|using[[:space:]]+(std)?::$FNS\b"

scan() {  # reads "path:line:text" lines, strips // comments, prints offenders
  sed -E 's#^([^:]*:[0-9]+:)(([^"/]|"[^"]*"|/[^/])*)//.*#\1\2#' | grep -E "^[^:]*:[0-9]+:.*($BAD)" || true
}

# Self-test, so a regex edit cannot silently turn the check off.
want_bad=$'x.cpp:1:  double a = std::sin(t);\nx.cpp:2:  y = pow(b, 2);\nx.cpp:3:using std::exp;\nx.cpp:4:  q = std::hypot(a, b);'
want_ok=$'x.cpp:5:  double a = libm::sin(t);\nx.cpp:6:  // std::sin(x) would differ\nx.cpp:7:  h = core::hypot(a, b) + num::round(c) + std::sqrt(d);\nx.cpp:8:  s.log(1);'
[[ "$(scan <<<"$want_bad" | wc -l)" -eq 4 ]] || { echo "check-libm: self-test failed to flag a banned call; fix BAD in $0" >&2; exit 2; }
[[ -z "$(scan <<<"$want_ok")" ]] || { echo "check-libm: self-test flagged an allowed call; fix BAD in $0" >&2; exit 2; }

dirs=()
for d in engine/core engine/loop engine/ai engine/wind engine/water engine/move engine/camera engine/audio engine/net engine/console engine/lab plugins games/*/sim games/*/labs; do [[ -d "$d" ]] && dirs+=("$d"); done
hits="$(git ls-files -- "${dirs[@]}" | grep -E '\.(cpp|hpp|h)$' | grep -v '^engine/core/libm/' | grep -v '^plugins/[^/]*/app/' \
  | xargs -r grep -nE "$FNS|hypot|__builtin_" /dev/null | scan)"
if [[ -n "$hits" ]]; then
  echo "$hits" >&2
  echo "check-libm: simulation code calls a platform transcendental (above). Call the same" >&2
  echo "function in ge::core::libm (#include \"core/libm/libm.hpp\") so the bits are the same on" >&2
  echo "every platform; for hypot use ge::core::hypot. If libm lacks the function, vendor it there" >&2
  echo "first, with its golden table (the determinism skill)." >&2
  exit 1
fi
echo "check-libm: ok (${#dirs[@]} simulation dirs)"
