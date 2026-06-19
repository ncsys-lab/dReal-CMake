#!/usr/bin/env python3
"""Rounding-mode routing lint for dReal (src/dreal only).

Syntactic enforcement of the FE_UPWARD routing discipline. This is the AST-level
intent of the planned clang-tidy check, realized dependency-free: a real
clang-tidy custom check must be compiled into clang-tidy (or built as a
libTooling tool against the clang libs), and clang-query against the project's
compile DB hits a toolchain header mismatch. A scoped regex lint enforces the
same *routing* rules (it does not, and clang-tidy could not either, verify
rounding *correctness*) and runs anywhere with no build.

Rules:
  1. No raw ibex::Function::backward — route through ibex_hc4_backward
     (util/ibex_guarded.h), which requires the UpwardRounding token.
  2. No raw .mid()/.diam() gaol getters — route through safe_mid/safe_diam
     (util/rounded_double.h), which assert FE_UPWARD.
  3. Smell: an interval built from hand-written scalar +/- arithmetic (the
     mis-rounded `Interval(mid - half, mid + half)` pattern) — use
     make_sound_interval / interval ops so gaol rounds outward.

Legitimate exceptions carry an inline `// rounding-lint: allow <reason>` marker
on the same line. Wrapper-definition files are fully allow-listed.
"""
import re
import sys
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent
SRC = ROOT / "src" / "dreal"

# Files that DEFINE the sanctioned wrappers/helpers.
ALLOW_FILES = {
    "util/rounded_double.h",  # safe_mid / safe_diam definitions
    "util/ibex_guarded.h",    # ibex_hc4_backward definition
}
ALLOW_MARK = "rounding-lint: allow"

RULES = [
    ("raw ibex backward (use ibex_hc4_backward)",
     re.compile(r'\.backward\s*\(')),
    ("raw .mid()/.diam() getter (use safe_mid/safe_diam)",
     re.compile(r'\.(?:mid|diam)\s*\(\s*\)')),
    ("interval built from hand scalar arithmetic (use make_sound_interval)",
     re.compile(r'(?:Box|ibex)::Interval\s*\([^;)]*\s[-+]\s[^;]*,')),
]


def is_generated(p: pathlib.Path) -> bool:
    # Skip flex/bison sources (parse-time, not solve-time) and generated output.
    return p.suffix in {".yy", ".ll"} or "parser" in p.name or "scanner" in p.name


def main() -> int:
    violations = []
    for p in sorted(SRC.rglob("*")):
        if p.suffix not in {".h", ".cc"} or is_generated(p):
            continue
        rel = str(p.relative_to(SRC))
        if rel in ALLOW_FILES:
            continue
        for n, line in enumerate(p.read_text().splitlines(), 1):
            if ALLOW_MARK in line:
                continue
            # Match on code only — strip line comments so pattern text mentioned
            # in a comment (e.g. documenting the forbidden form) is not flagged.
            code = line.split("//", 1)[0]
            for desc, rx in RULES:
                if rx.search(code):
                    violations.append((rel, n, desc, line.strip()))
    if violations:
        sys.stderr.write("Rounding-mode routing lint FAILED:\n\n")
        for rel, n, desc, line in violations:
            sys.stderr.write(f"  src/dreal/{rel}:{n}: {desc}\n        {line}\n")
        sys.stderr.write(f"\n{len(violations)} violation(s). Add a "
                         f"`// {ALLOW_MARK} <reason>` marker if intentional.\n")
        return 1
    print("Rounding-mode routing lint: clean.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
