#!/usr/bin/env python3
"""Shared definitions for the `odeexpr` benchmark family (ode_expressivity set).

Single source of truth for: where the set lives, per-family selection weights,
family classification, and name<->path resolution. Unlike the other three
families (whose paths are flat directories), odeexpr is content-addressed: each
logical benchmark (`bench_id`) has hashed revision files and a `manifest.json`
that names the *current* revision. We key on the stable `bench_id` and resolve
through the manifest so regeneration (which mints new hashed files, never
overwrites) does not silently change which file a name points at.

A benchmark name in this family is `odeexpr_<bench_id>`, e.g.
`odeexpr_box_sweep.ising__phi_7`.

CLI: `python3 odeexpr.py --all` prints TSV (name <TAB> abspath) for all 43.
"""
import json
import os
import sys

ODEEXPR_ROOT = "/Users/kunalsheth/Documents/new_dreal/ode_expressivity/benchmarks"
MANIFEST_PATH = os.path.join(ODEEXPR_ROOT, "manifest.json")
PREFIX = "odeexpr_"

# Per-family selection/severity weights. Encodes the user's equivalence
# "1 odeexpr = 3 github = 2 saradc = 3 tacas": with odeexpr=6, github/tacas=2,
# saradc=3, one odeexpr equals 3 github (3*2), 2 saradc (2*3), 3 tacas (3*2).
FAMILY_WEIGHTS = {"odeexpr": 6, "saradc": 3, "github": 2, "tacas": 2}


def family_of(name: str) -> str | None:
    """Classify a (possibly .smt2-suffixed) benchmark name into its family."""
    if name.startswith(PREFIX):
        return "odeexpr"
    if name.startswith("1mhz_"):
        return "saradc"
    if name.startswith("github_oct5_"):
        return "github"
    if name.startswith("tacas_c2e2_"):
        return "tacas"
    return None


def weight_of(name: str) -> float:
    """Selection weight for a benchmark name (defaults to 1 for unknown families)."""
    fam = family_of(name)
    return FAMILY_WEIGHTS.get(fam, 1) if fam else 1


def _load_manifest() -> dict:
    with open(MANIFEST_PATH) as f:
        return json.load(f)


def _current_file(entry: dict) -> str | None:
    """Relative path of the entry's current revision, or None."""
    for rev in entry.get("revisions", []):
        if rev.get("state") == "current":
            return rev.get("file")
    return None


def load_odeexpr_names() -> list[str]:
    """All active odeexpr benchmark names whose current file exists on disk."""
    manifest = _load_manifest()
    names = []
    for bench_id, entry in manifest.items():
        if entry.get("status") != "active":
            continue
        rel = _current_file(entry)
        if rel and os.path.exists(os.path.join(ODEEXPR_ROOT, rel)):
            names.append(PREFIX + bench_id)
    return sorted(names)


def resolve_odeexpr(name: str) -> str | None:
    """Map `odeexpr_<bench_id>` to its current revision's absolute path."""
    if not name.startswith(PREFIX):
        return None
    bench_id = name[len(PREFIX):].removesuffix(".smt2")
    manifest = _load_manifest()
    entry = manifest.get(bench_id)
    if entry is None:
        return None
    rel = _current_file(entry)
    if not rel:
        return None
    path = os.path.join(ODEEXPR_ROOT, rel)
    return path if os.path.exists(path) else None


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "--all":
        n = 0
        for name in load_odeexpr_names():
            path = resolve_odeexpr(name)
            if path:
                print(f"{name}\t{path}")
                n += 1
            else:
                print(f"WARN: could not resolve {name}", file=sys.stderr)
        print(f"Listed {n} odeexpr benchmarks.", file=sys.stderr)
    else:
        print("Usage: odeexpr.py --all", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
