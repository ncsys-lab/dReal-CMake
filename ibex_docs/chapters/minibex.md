# Chapter: Minibex (modeling language)

Source: [`minibex.rst.txt`](../../../ibex-docs/_sources/minibex.rst.txt) ·
`minibex.html`. The Minibex `.mbx`/`.bch` language and its parser — a textual way
to declare variables, constants, functions, constraints and a system, instead of
building them in C++ (see [function](function.md)/[system](system.md)).

> **dReal status:** **not used** (correctly). dReal has its own front-ends:
> `src/dreal/smt2/` (SMT-LIB2) and `src/dreal/dr/` (the dReal3 `.dr` syntax,
> incl. ODE constructs). Backward `.dr` compatibility is *intentional*
> (project `CLAUDE.md`), so adopting IBEX's parser would be a regression, not a
> gain. The parser is also where fork patch #3 (`9a23379c`, parser.yc namespace
> fix) lives — a build-only fix, not a feature dReal calls.

Listed in [`../AUDIT.md`](../AUDIT.md) E (correctly ignored).
