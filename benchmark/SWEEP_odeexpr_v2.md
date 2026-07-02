# odeexpr_v2 flag/parameter sweep — best config + optimization avenues

**Date:** 2026-07-01 · **Binary:** `gcc_build/dreal4` @ HEAD `a077ab36f` (Release, rebuilt
19:37) · **Set:** regenerated odeexpr_v2, **25 `exists_forall/` + 25 `forall/`** · **Cap:** 120 s
CPU, δ baked 0.01 unless overridden · **Metric:** CPU time (user+sys), 12-way pool.

Focus per the user: the **`exists_forall/` ∃∀ queries** (`∃c (MLP params). ∀x. descent(c,x) ≤
−1e-9`). The result we care about is **UNSAT** = "no witness c exists" = a separation/impossibility
proof. delta-SAT is *inconclusive* (a δ-relaxed candidate witness) and not a goal.

Everything below is a **COMPLETENESS** story (missed refutation / non-termination). No config
produced or could produce a false `unsat` — dReal stays sound throughout; the failure mode is
"never reaches the UNSAT it might be entitled to," never "asserts a wrong verdict."

> **Update 2026-07-02:** the *encoding*-side lever (worklog avenue #1, symbolic rewrite of the descent
> body) was also tested — SymPy-verified `sig2tanh`/`factor`/`expand`/`horner`/`simplify` forms — and is
> **null for UNSAT** (0/set) with no robust speedup (factor net-reduces solved count; simplify hangs on
> N2). Full record: `exists_forall_perf.md` §2026-07-02.

---

## TL;DR

- **∃∀ best config: there isn't one.** Across **17 configurations** — every precision point
  0.005→1.0, `--forall-pre-prune` (±`-prec`), `--forall-polytope`, `--polytope`, `--acid`,
  `--3bcid`, `--local-optimization`, `--smear smearsum`, `--jobs 4` (±pre-prune), and combined
  "kitchen-sink" (pre-prune+polytope+acid ±finer-δ) — **0 of the 21 ∃∀ targets ever returned
  UNSAT** in 120 s. Baseline is as good as anything; the levers are equivalent at zero.
- **The wall is algorithmic, not compute.** `--jobs 4` (≈4× compute in the same wall) cracked
  **nothing** — it didn't even match single-thread. A verbose trace shows the inner existential ICP
  doing **~600 K branches in 20 s** on the (J, ch) box while completing **~1 CEGIS refinement**: it
  cannot contract the box because the transcendental descent body (tanh/exp/pow, with ch·x
  multiplicity) has interval enclosures too loose to prune. More of the same compute never
  converges.
- **`forall/` best config = baseline δ=0.01** (3 UNSAT / 21 delta-sat / 1 timeout) — the only config
  that keeps all 3 UNSAT. This set is essentially already solved; 1 file (n3 `average_descends`)
  stays open.
- **Avenue that would move the needle: tighter enclosures of the transcendental descent body** — not
  any runtime knob. Cheapest first spike is a **symbolic dependency-mitigation pass** (Horner/CSE +
  tanh-monotonicity via Drake `Expand`), which dReal lacks entirely today. Note IBEX's one
  linear-relaxation lever (`--polytope` = X-Taylor `CtcPolytopeHull`) is **already wired and only
  partially effective** (it contracts — 2× on one file — but never empties the hard boxes), and
  affine arithmetic was stripped from the fork while true Taylor models don't exist in the stack.
  See the ranked avenues for the difficulty grades.

---

## Step 0 — baseline classification (δ=0.01, default flags, 120 s)

| set | UNSAT | delta-sat (inconclusive) | TIMEOUT |
|---|---|---|---|
| **exists_forall (25)** | **0** | 9 (4 `sign_agreement` true-SAT via zero witness + 5 strict-margin) | **16** |
| forall (25) | 3 | 21 | 1 |

**∃∀ target set = 16 timeouts + 5 strict-margin delta-sats = 21 files** (excludes the 4
`sign_agreement` delta-sats, which are genuinely SAT via the zero-MLP witness — `≥ 0` admits it).
Since baseline ∃∀ UNSAT = 0, the "no UNSAT-losing flip" guard is *vacuous on ∃∀* — the whole game
is converting a timeout into UNSAT, and nothing does.

---

## Step 1+2+3 — the ∃∀ sweep (the focus): every lever, zero UNSAT

Per-config outcome over the 21 ∃∀ targets (all at 120 s CPU cap):

| config | UNSAT | delta-sat | TIMEOUT | note |
|---|---|---|---|---|
| baseline δ=0.01 | **0** | 5 | 16 | reference |
| `--precision 0.005` | 0 | 3 | 18 | finer ⇒ stronger refutation, but slower ⇒ *more* timeouts |
| `--precision 0.05` | 0 | 5 | 16 | |
| `--precision 0.1` | 0 | 6 | 15 | |
| `--precision 0.5` | 0 | 9 | 12 | coarser ⇒ more (inconclusive) delta-sat |
| `--precision 1.0` | 0 | 10 | 11 | coarsest ⇒ most delta-sat, still 0 UNSAT |
| `--forall-pre-prune` | 0 | 4 | 17 | *adds* overhead — pushed one delta-sat into timeout |
| `--forall-pre-prune -prec 0.1` | 0 | 5 | 16 | finer pre-prune, no traction |
| `--forall-polytope` | 0 | 5 | 16 | same verdict set; **CPU changes 2–4×** (engages, insufficient) |
| `--polytope` | 0 | 5 | 16 | same verdict set; CPU differs |
| `--acid` | 0 | 5 | 16 | same verdict set; CPU differs |
| `--3bcid` | 0 | 5 | 16 | same verdict set; CPU differs |
| `--local-optimization` | 0 | 5 | 16 | same verdict set; CPU differs |
| `--smear smearsum` | 0 | 5 | 16 | same verdict set (matches the generator's own choice) |
| `--jobs 4` | 0 | 3 | 18 | 4× compute, cracked nothing, *fewer* than baseline |
| `--jobs 4 --forall-pre-prune` | 0 | 3 | 18 | |
| kitchen-sink (pp+poly+acid) | 0 | 5 | 16 | |
| kitchen-sink + `--precision 0.005` | 0 | 3 | 18 | |

Direction of the two knobs that *do* move outcomes (neither toward UNSAT):
- **δ coarser** → more delta-sat (easier to δ-satisfy a candidate) — *inconclusive*, the wrong
  direction for a separation proof.
- **δ finer** → fewer delta-sat, more timeout (refutation strengthens per check but each check is
  slower; net worse within 120 s).
- **contraction flags** (pre-prune/polytope/acid/3bcid/local-opt/smear) → **engage but are
  insufficient**. They change per-file CPU noticeably (e.g. `--forall-polytope` takes
  `average_descends dh3` 18.9 s → **10.0 s**, but `both_descend dh3` 1.8 s → **7.9 s**;
  `--forall-pre-prune` pushes an n2 delta-sat from 26 s clean into timeout), yet on **every one of
  the 16 hard files the verdict is unchanged** — they contract the existential box, just never to
  empty. (An earlier draft called these "byte-identical to baseline," inferred from the aggregate
  verdict-count; that was wrong — the contractors are active, they just don't crack the wall.)

Set-level verification (guarding against a hidden win the counts could mask — crack a timeout, lose
a delta-sat, count unchanged): **no config cracked any baseline timeout into a completion other than
inconclusive delta-sat, and no config produced UNSAT anywhere.** The only timeout→delta-sat
conversions are under coarse δ (0.5 and 1.0 each convert 5 timeouts → delta-sat — inconclusive).

### Why — the existential-isolation wall, made concrete
`--verbose debug` on the smallest hard case (n1 `both_descend`, 4 vars), 20 s:

- `IcpSeq::CheckSat() Loop Head` ≈ **1.2 M**, `Branch J/ch/x` ≈ **600 K**, CEGIS refinements ≈ **1**.
- `Icp::EvaluateBox() Found an interval >= precision` fires ~600 K times: ICP repeatedly finds
  sub-boxes straddling the descent constraint boundary that it **cannot prune**, so it branches — on
  a continuous (J, ch) box that never contracts to empty.
- Root cause: the descent body `(… − ch0·… + tanh(J·(ch0 + ch1·x)) …)·(…)` has **loose interval
  enclosures** — tanh/exp/pow are widened, and ch/x appear with high multiplicity (the **dependency
  problem**), so every branch leaves a feasible-looking sliver. UNSAT (empty box) is never reached.

This is exactly why **more compute (`--jobs 4`) and the pre-pruner (same loose `CtcForAll`
enclosure) do nothing** — the loop isn't compute-starved (600 K branches/20 s), it's
**non-convergent**.

---

## abs vs box (per the encoding-equivalence hint)

`__abs__` = `(>= (abs x) eps)` (non-smooth); `__box__` = `(or (>= x eps) (<= x −eps))` (disjunction
→ two smooth sub-intervals). Same exclusion set, different solver stress.

**∃∀:**

| encoding | TIMEOUT | delta-sat | **UNSAT** |
|---|---|---|---|
| abs (12) | 10 | 2 | **0** |
| box (13) | 6 | 7 | **0** |

Box terminates more often — **but only into inconclusive delta-sat, never UNSAT.** And it is
**confounded**: the generator paired box mostly with the *wider* `eps=1/10` (10/13) and abs mostly
with `eps=1/100` (7/12); wider exclusion ⇒ smaller ∀-domain ⇒ easier. The **3 truly matched pairs**
(same system/descent/dh) are **all TIM/TIM**, including the one with matched `eps=1/10`. So for the
UNSAT goal there is **no easier encoding to pivot to** — the wall is identical.

**forall/:** all 13 abs-form → delta-sat; **all 3 UNSAT and the 1 timeout are box-form.** Here the
disjunctive box form is where the interesting (UNSAT) verdicts live. Actionable takeaway:
**standardize the generator on box-form** — it's the better-behaved encoding and the one that yields
UNSAT on the tractable (`forall/`) set — but this does not, by itself, unlock ∃∀ UNSAT.

---

## Step 4 — forall/ secondary (light)

| config | UNSAT | delta-sat | TIMEOUT |
|---|---|---|---|
| **baseline δ=0.01** | **3** | 21 | 1 |
| `--precision 0.1` | 2 | 23 | 0 |
| `--precision 0.5` | 2 | 23 | 0 |
| `--forall-pre-prune` | 3 | 21 | 1 |
| `--smear smearsum` | 3 | 21 | 1 |

**Best = baseline δ=0.01** — the only config preserving all 3 UNSAT. Coarser δ (0.1/0.5) cracks the
lone timeout (n3 `average_descends` dh2 box) but only into *inconclusive* delta-sat **and loses one
n1 UNSAT** (an UNSAT→delta-sat flip — expected COMPLETENESS behaviour of a weaker refutation, not a
soundness bug). Net a bad trade. The 3 UNSAT are the small n1 box files (instant, 0.0 s); the 1 open
file shares the same transcendental wall as the ∃∀ set.

---

## Ranked optimization avenues

The binding constraint is enclosure tightness: the inner ICP cannot contract the existential
(J, ch) box to empty because the transcendental descent body's interval enclosure is too loose.
Grades below reflect a code-level survey of what IBEX (this fork) and dReal already provide.

**Key context — the one relevant lever IBEX has is already wired and only *partially* effective.**
IBEX's sole "Taylor-ish" tool is first-order **X-Taylor linear relaxation** (`LinearizerXTaylor`
+ `CtcPolytopeHull`), and dReal already wraps exactly it as `--polytope` / `--forall-polytope`
(`contractor_ibex_polytope.cc:108`). The sweep shows it **engages and contracts** (2× faster on one
completing file, slower on another) but is **insufficient** — it never empties the box on the 16
hard instances. So "add a linear relaxation" is not an untried idea; it's tried, active, and not
strong enough. That partial traction is mildly encouraging for a *stronger* relaxation, and it
rules out "the relaxation path is dead."

1. **Symbolic dependency-problem mitigation** — *cheapest, try first.* dReal does **zero** symbolic
   pre-simplification today (no `Expand`/`Horner`/`CSE` in `src/dreal/`); expressions go verbatim
   into `ibex_converter`, so every ch/x occurrence is independent — maximal dependency-problem
   width. Drake symbolic (vendored) has `Expression::Expand()` + polynomial tooling to build a
   Horner/factor/CSE + tanh-monotonicity pass before conversion. **Effort: LOW–MEDIUM** (days-scale
   spike, no engine rewrite). **Risk: real** — the hard coupling is `poly(ch)·tanh(poly(J,ch,x))`;
   polynomial rewriting doesn't break the tanh–coefficient coupling, so payoff may be modest. But
   it's the only cheap shot and dReal lacks it entirely.
2. **Affine arithmetic** — *higher payoff, higher cost, uncertain.* Tracks first-order correlations,
   directly attacking the dependency problem. **Absent from this fork** (upstream IBEX ships it; the
   `dreal-perf-patches` fork stripped it — no `Affine*` files). Path: re-vendor the module (fits the
   12-patch fork model) + wrap a contractor. **Effort: HIGH but bounded.** Caveats: affine yields a
   tighter *forward enclosure*, not native box *contraction* — IBEX feeds it back into the *same*
   linear-relaxation path that's already only-partially-effective here; and it loses tightness on
   high-degree poly×transcendental products, which is exactly the descent body. Worth it only if (1)
   underdelivers.
3. **Taylor models** (higher-order polynomial + interval remainder) — *research-scale.* Nothing to
   reuse: IBEX has none, and CAPD's TM machinery (already a dep) is ODE-flow/time-integration
   oriented, not a drop-in static-NRA expression enclosure. A new arithmetic type + TM→box backward
   contractor threaded through the engine. **Effort: VERY HIGH.** Last resort.
4. **Standardize on box-form encoding** *(the user's hint, actionable now, cheap).* Prefer the
   disjunctive box exclusion over non-smooth `abs`; box is where the `forall/` UNSAT verdicts live,
   it's smoother/more linearizable, and it composes better with (1)–(3). Regenerate the corpus
   box-only. Necessary hygiene — not sufficient for ∃∀ UNSAT on its own (matched abs/box pairs are
   both TIM).
5. **Re-measure `--forall-pre-prune` (`CtcForAll`) after (1)/(2).** It currently **engages but is
   net-negative** on this set (adds overhead, pushed an n2 delta-sat into timeout; never helped a
   verdict) — its enclosure quality is gated on the same transcendental looseness, so improve that
   first, then re-check.
6. **Do NOT invest in:** more compute (`--jobs 4` = ≈4× compute-in-wall, cracked nothing — the wall
   is *not* compute-bound; a longer single-thread timeout is correspondingly unlikely to help);
   precision tuning (coarser δ only buys *inconclusive* delta-sat, never UNSAT; finer δ only adds
   timeouts). The contraction flags (acid/3bcid/local-opt/smear/polytope) are **verdict-null**
   (they change CPU but no verdict) — not worth tuning as-is; their ceiling is set by avenues
   (1)–(3).
7. **Encoder-side escalation (if (1)–(3) stall).** If δ-complete ICP fundamentally cannot isolate
   the existential region for these transcendental separations, the impossibility proofs may need a
   different route (quantifier elimination on a polynomial over-approximation, or accepting specific
   instances as open) rather than more solver work. Owner's call — surfaced, not pre-empted.

---

## Reproduction

Job set: `python3 benchmark/select.py --family odeexpr_v2 --all` (50 rows; dict-keyed manifest,
`odeexpr.py` unchanged). ∃∀ target set: `/tmp/v2_ef_targets.tsv` (21). Raw results under
`benchmark/results/`:

| run | dir |
|---|---|
| baseline (all 50) | `sweep_20260701_194313/base` |
| ∃∀ screen (12 configs × 21) | `sweep_20260701_194940` |
| ∃∀ `--jobs 4` probe (2 × 21) | `sweep_20260701_202417` |
| ∃∀ kitchen-sink (3 × 21) | `sweep_20260701_205036` |
| forall/ secondary (5 × 25) | `sweep_20260701_210350` |
| rewrite probe (v0/sig2tanh/factor × 22) | scratchpad `odeexpr_v2_rewrite/sweep` |
