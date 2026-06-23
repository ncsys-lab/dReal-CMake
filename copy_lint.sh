#!/usr/bin/env bash
# copy_lint.sh — incremental clang-tidy gate for copies, perf, and UB.
#
# WHY: this solver is memory-bound; an accidental by-value copy of a heavy type
# (Box, capd::IMap, Environment, IntervalVector, ...) where a const-ref / move
# belongs is a silent performance regression. A copy is *semantic* (type-
# dependent), so the regex lint.py structurally cannot see it. The same is true
# of undefined behavior (use-after-move, dangling handles, undefined memory
# manipulation, ptr/array mismatches, ...). clang-tidy's built-in checks are
# type-aware and catch exactly these; the gated set (all performance-* minus
# enum-size, plus an explicit UB/memory-safety bugprone-* allow-list) + header
# filter live in .clang-tidy. This script wires them into a gate: brew clang-tidy
# + the macOS SDK isysroot fixup + --warnings-as-errors. The tree is currently
# clean on every gated check, so the UB checks are zero-noise future protection.
#
# It is INCREMENTAL by default: only .cc files changed vs. the merge-base (plus
# staged/unstaged) are analyzed, so it flags copies in new/changed code without
# demanding a full-tree cleanup of pre-existing findings, and without a slow
# 230-TU sweep every run. Pass --all for a whole-tree audit.
#
# Caveat: only .cc are translation units in the compile DB. A changed .h is
# checked only when an analyzed .cc includes it (via HeaderFilterRegex); a header
# with no changed includer is not analyzed this run — accepted for an incremental
# gate; use --all before a merge to cover everything.
#
# A flagged finding is resolved by fixing it (const& / move / drop a no-op
# std::move / the real bug) or, when genuinely needed, justified inline with
#   // NOLINT(<check>)  <reason>
# the native twin of lint.py's `// lint: allow`.
#
# Usage: ./copy_lint.sh [--all] [build_dir]
#   build_dir defaults to cmake-build-debug (must contain compile_commands.json).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_ROOT"

ALL=0
BUILD_DIR="cmake-build-debug"
for arg in "$@"; do
  case "$arg" in
    --all) ALL=1 ;;
    *) BUILD_DIR="$arg" ;;
  esac
done

# Locate clang-tidy (PATH, else brew llvm). Fail loud — never auto-install.
if command -v clang-tidy >/dev/null 2>&1; then
  TIDY="$(command -v clang-tidy)"
elif [[ -x /opt/homebrew/opt/llvm/bin/clang-tidy ]]; then
  TIDY=/opt/homebrew/opt/llvm/bin/clang-tidy
else
  echo "FAIL: clang-tidy not found. Install with: brew install llvm" >&2
  exit 2
fi

if [[ ! -f "$REPO_ROOT/$BUILD_DIR/compile_commands.json" ]]; then
  echo "FAIL: $BUILD_DIR/compile_commands.json missing — clang-tidy needs the" >&2
  echo "      compile DB. Configure with CMAKE_EXPORT_COMPILE_COMMANDS=ON." >&2
  exit 2
fi

SDK="$(xcrun --show-sdk-path)"  # set -e fails loud if xcrun is unavailable

# Collect the .cc files to analyze (while-read for bash 3.2 — no mapfile).
FILES=()
if [[ $ALL -eq 1 ]]; then
  while IFS= read -r line; do FILES+=("$line"); done \
    < <(find src/dreal -name '*.cc' | sort)
  echo "[copy-lint] full sweep: ${#FILES[@]} translation units in src/dreal"
else
  base="$(git merge-base HEAD main)"
  while IFS= read -r line; do FILES+=("$line"); done < <(
    {
      git diff --name-only --diff-filter=ACMR "$base"...HEAD
      git diff --name-only HEAD
      git diff --name-only --cached
    } | grep -E '^src/dreal/.*\.cc$' | sort -u
  )
  echo "[copy-lint] incremental: ${#FILES[@]} changed .cc vs. merge-base ($base)"
fi

if [[ ${#FILES[@]} -eq 0 ]]; then
  echo "[copy-lint] PASS: no src/dreal/*.cc to check."
  exit 0
fi

echo "[copy-lint] using $TIDY (-p $BUILD_DIR, isysroot $SDK)"
failed=()
for f in "${FILES[@]}"; do
  [[ -f "$f" ]] || continue  # skip deletes
  if ! "$TIDY" -p "$BUILD_DIR" --quiet \
        --header-filter='src/dreal/.*' --warnings-as-errors='*' \
        --extra-arg=-isysroot --extra-arg="$SDK" \
        "$f"; then
    failed+=("$f")
  fi
done

if [[ ${#failed[@]} -gt 0 ]]; then
  echo >&2
  echo "FAIL: findings in ${#failed[@]} file(s):" >&2
  printf '  %s\n' "${failed[@]}" >&2
  echo "Fix (const& / move / the real bug) or justify with // NOLINT(<check>) <reason>." >&2
  exit 1
fi

echo "[copy-lint] PASS: no findings."
