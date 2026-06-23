#!/usr/bin/env python3
"""Source-hygiene regex lint for dReal (src/dreal only).

Two rule families, one mechanism (file walk + per-line regex + an inline
`// lint: allow <reason>` escape marker). This is the AST-level intent of a
clang-tidy check, realized dependency-free: a real clang-tidy custom check must
be compiled into clang-tidy (or built as a libTooling tool against the clang
libs), and clang-query against the project's compile DB hits a toolchain header
mismatch. A scoped regex lint enforces the same *routing/shape* rules (it does
not, and clang-tidy could not either, verify rounding *correctness*) and runs
anywhere with no build.

== Rounding-mode routing (FE_UPWARD / FE_TONEAREST regimes) ==
  1. No raw ibex::Function::backward — route through ibex_hc4_backward
     (util/rounded_interval.h), which requires the UpwardRounding token.
  2. No raw .mid()/.diam() gaol getters — route through safe_mid/safe_diam
     (util/rounded_interval.h), which require the UpwardRounding token.
  3. Smell: an interval built from hand-written scalar +/- arithmetic (the
     mis-rounded `Interval(mid - half, mid + half)` pattern) — use
     make_sound_interval / interval ops so gaol rounds outward.
  4. No raw json `.dump(` — route through dump_json (util/json_guarded.h),
     which requires the NearestRounding token (nlohmann serializes its doubles
     to decimal in dump(), correct only under FE_TONEAREST).
  5. No raw std::to_string on a value feeding a parser — it renders doubles with
     only 6 fractional digits (sprintf %f), so a non-exact coefficient (1/3 ->
     "0.333333") feeds an *unfaithful* literal to the downstream parser. The one
     solver-feed path (CAPD's IMap) routes through to_capd_string (max_digits10,
     + a NearestRounding token); display goes through format_double. Mark the
     legitimate integer uses (variable/file-name counters) // lint: allow int.

Note on the nearest regime: scalar double->decimal formatting (`os << v`) is not
syntactically distinctive enough for a regex to catch reliably, so that routing
is enforced at *compile time* by format_double / to_capd_string requiring the
NearestRounding token, not by this lint. The lint covers the two regex-detectable
forms: json `.dump(` (rule 4) and the std::to_string solver-feed trap (rule 5).

== Iteration shape ==
  6. No subscript by a side-effecting counter (`arr[i++]`, `arr[++i]`). A manual
     index advancing in parallel with a range-`for` loop variable silently
     drifts out of sync with it once the two containers' orders differ — the
     dreal/dreal4 BUG-005 scrambled-model class (commit 5774191f2, fixed
     0296a8e19). Use one shared index, or index the same container both places.

Legitimate exceptions carry an inline `// lint: allow <reason>` marker on the
same line. Wrapper-definition files are fully allow-listed.
"""
import re
import sys
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent
SRC = ROOT / "src" / "dreal"

# Files that DEFINE the sanctioned wrappers/helpers.
ALLOW_FILES = {
    "util/rounded_interval.h",  # safe_mid / safe_diam / ibex_hc4_backward defs
    "util/rounded_format.h",    # format_double definition
    "util/json_guarded.h",      # dump_json definition
}
ALLOW_MARK = "lint: allow"

RULES = [
    ("raw ibex backward (use ibex_hc4_backward)",
     re.compile(r'\.backward\s*\(')),
    ("raw .mid()/.diam() getter (use safe_mid/safe_diam)",
     re.compile(r'\.(?:mid|diam)\s*\(\s*\)')),
    ("interval built from hand scalar arithmetic (use make_sound_interval)",
     re.compile(r'(?:Box|ibex)::Interval\s*\([^;)]*\s[-+]\s[^;]*,')),
    ("raw json .dump( (use dump_json under a NearestRounding token)",
     re.compile(r'\.dump\s*\(')),
    ("raw std::to_string feeding a parser truncates doubles to 6 fractional "
     "digits (%f) — use to_capd_string (CAPD feed) or format_double (display); "
     "mark integer uses // lint: allow int",
     re.compile(r'(?<![.\w])to_string\s*\(')),
    ("subscript by a side-effecting counter (parallel index drifts out of "
     "sync with a range-for loop variable; use one shared index)",
     re.compile(r'\[\s*(?:(?:\+\+|--)\s*[A-Za-z_]\w*|[A-Za-z_]\w*\s*(?:\+\+|--))\s*\]')),
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
        sys.stderr.write("Source-hygiene lint FAILED:\n\n")
        for rel, n, desc, line in violations:
            sys.stderr.write(f"  src/dreal/{rel}:{n}: {desc}\n        {line}\n")
        sys.stderr.write(f"\n{len(violations)} violation(s). Add a "
                         f"`// {ALLOW_MARK} <reason>` marker if intentional.\n")
        return 1
    print("Source-hygiene lint: clean.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
