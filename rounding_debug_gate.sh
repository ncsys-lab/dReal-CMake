#!/usr/bin/env bash
# rounding_debug_gate.sh — Debug-build rounding-mode assertion gate.
#
# WHY: DREAL_ASSERT_ROUNDING / DREAL_ASSERT_ROUNDING_CONSISTENT compile out
# under NDEBUG (release), so the FPU-mode invariants are only checked in a Debug
# build. This script builds the test suite in Debug and runs it; a violated
# rounding assertion calls abort(), crashing the test binary. The gate fails if
# that happens. It is the automated form of "compile Debug + run any benchmark
# quickly reveals leaky rounding modes" — run it in CI / before merges.
#
# Usage: ./rounding_debug_gate.sh [build_dir]
#   build_dir defaults to cmake-build-debug (must be a CMAKE_BUILD_TYPE=Debug
#   configuration). The suite's known-flaky trio (IfThenElseEliminatorTest.*,
#   Timer.Test1 — see CLAUDE.md) are EXPECT failures, not assertion aborts, and
#   do not fail this gate; only a crash / abort (rounding assertion) does.
set -euo pipefail

BUILD_DIR="${1:-cmake-build-debug}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_ROOT/$BUILD_DIR"

build_type="$(grep -E '^CMAKE_BUILD_TYPE:' CMakeCache.txt | cut -d= -f2)"
if [[ "$build_type" != "Debug" ]]; then
  echo "FAIL: $BUILD_DIR is CMAKE_BUILD_TYPE=$build_type, expected Debug." >&2
  echo "      Rounding assertions are compiled out unless this is a Debug build." >&2
  exit 2
fi

echo "[rounding-gate] building Debug test target in $BUILD_DIR ..."
cmake --build . --target dreal4_cmake_test -j8

echo "[rounding-gate] running suite (a rounding-assertion abort fails the gate) ..."
# A DREAL_ASSERT_ROUNDING* failure abort()s the binary -> non-zero exit / signal.
# The known-flaky EXPECT trio returns 1 without crashing; we distinguish a crash
# (signal -> exit >128) from ordinary EXPECT failures.
set +e
./dreal4_cmake_test >"$REPO_ROOT/$BUILD_DIR/rounding_gate.log" 2>&1
rc=$?
set -e

if [[ $rc -gt 128 ]]; then
  echo "FAIL: test binary crashed (exit $rc) — likely a rounding-mode assertion." >&2
  grep -iE "rounding|fegetround|Assertion" "$REPO_ROOT/$BUILD_DIR/rounding_gate.log" | tail -20 >&2 || true
  exit 1
fi

# Any FAILED test that is NOT in the known-flaky trio is also a gate failure.
unexpected="$(grep -E '^\[  FAILED  \]' "$REPO_ROOT/$BUILD_DIR/rounding_gate.log" \
  | grep -vE 'IfThenElseEliminatorTest\.(NestedITEs|ITEsInForall)|Timer\.Test1' || true)"
if [[ -n "$unexpected" ]]; then
  echo "FAIL: unexpected test failures (outside the known-flaky trio):" >&2
  echo "$unexpected" >&2
  exit 1
fi

echo "[rounding-gate] PASS: no rounding-mode assertion fired; only known-flaky trio (if any)."
