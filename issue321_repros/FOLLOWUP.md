# Follow-up on #321: `2^-1075` underflow is one instance of a broader false-`unsat` category

While turning the `pow(0.5, 1075)` (= `2^-1075`) underflow from this issue into a regression
test, we found it is **one instance of a category**:

> A subexpression whose *true* value underflows to `±0` (or overflows to `±inf`) is silently
> replaced by a value that lies about its sign/magnitude. Downstream, an inequality that is
> really **sat** then collapses to **false** → the solver reports a **false `unsat`**.

It surfaces in **two independent layers**, with different triggers and different fixes. Below are
minimal, runnable repros (attached `.smt2` files), the root cause of each layer, and the fix we
landed. All "before/after" verdicts are from actually running the files (see *Verification*).

---

## Manifestation 1 — Drake symbolic constant-fold (construction time)

This is the one that bites on real `.smt2` input. dReal's symbolic layer eagerly constant-folds
`pow`/`mul`/`div` of constant operands. The exactly-FP-representable literals the parser builds
(`0.5`, integers) fold via `std::pow` / `*` / `/`, and **the rounded result lies when it
underflows**: `std::pow(0.5, 1075)` = `2^-1075` = exactly `½·DBL_TRUE_MIN`, which ties-to-even
rounds to `0.0`. So `pow(0.5,1075) > 0` is folded to `0.0 > 0` → `False` **before any interval
reasoning runs**.

```smt2
; 01_pow_fold_underflow.smt2   — true value 2^-1075 > 0, so the answer is sat
(set-logic QF_NRA)
(assert (> (^ 0.5 1075) 0))
(check-sat)
```

The same lie occurs through the `mul` and `div` fold sites — here built from
exactly-representable `(^ 0.5 N)` blocks (each inner power is a normal double; only the outer
op underflows):

```smt2
; 02_mul_fold_underflow.smt2   — 2^-1000 * 2^-100 = 2^-1100  underflows to 0.0
(assert (> (* (^ 0.5 1000) (^ 0.5 100)) 0))

; 03_div_fold_underflow.smt2   — 2^-1000 / 2^100  = 2^-1100  underflows to 0.0
(assert (> (/ (^ 0.5 1000) (^ 2 100)) 0))
```

**Verified before/after** (`--precision 0.001`):

| repro | before (buggy) | after (fixed) |
|---|---|---|
| `01_pow_fold_underflow.smt2` | `unsat` ❌ | `delta-sat` ✓ |
| `02_mul_fold_underflow.smt2` | `unsat` ❌ | `delta-sat` ✓ |
| `03_div_fold_underflow.smt2` | `unsat` ❌ | `delta-sat` ✓ |
| `04_faithful_control_unsat.smt2` (`(< (^ 2 3) 0)`) | `unsat` ✓ | `unsat` ✓ |

**Fix.** At the three eager fold sites, detect an *unfaithful* fold — a nonzero true value that
rounded to `±0` (or a finite one that overflowed to `±inf`, sign read from `std::signbit`) — and
fold instead to a **sound `RealConstant` interval** that brackets the true value:
`[0, DBL_TRUE_MIN]`, `[-DBL_TRUE_MIN, 0]`, `[DBL_MAX, +inf]`, `[-inf, -DBL_MAX]`. A faithful fold
still produces the scalar (`2^3` stays `8`), so nothing else changes.

> One non-obvious constraint worth flagging for anyone fixing this upstream: the result must stay
> a **constant**, not a symbolic `Pow`/`Mul`. A symbolic product/power with all-constant operands
> violates a Drake AST invariant (`ExpressionMulFactory::AddTerm` asserts it) and aborts a debug
> build. Folding to a `RealConstant` interval (still a constant) is what keeps it both sound and
> legal. Note also that the parser only builds *exactly-representable* literals as foldable
> `Constant`s; an inexact decimal like `0.1` is already a `RealConstant` interval and never folds.

---

## Manifestation 2 — ibex HC4 backward (contraction time)

This is the layer the original DISABLED test (`PowSubnormalUnderflowEndToEnd`) captured, and it
lives **at the interval level**, not in `.smt2` input — once a base/exponent is a *variable* so
nothing folds, the *forward* op already over-approximates soundly to `[0, DBL_TRUE_MIN]`, so a
`> 0` query is satisfiable from the forward box and never needs the tight backward. The bug only
bites when contraction pins a tight-inverting backward op's *output* to a non-zero subnormal-band
target. Minimal ibex reproducer (HC4 backward of `x − pow(0.5, 1075) = 0` over `x ∈ [DBL_TRUE_MIN, +∞)`):

```cpp
ibex::Variable x;
ibex::Function f(x, x - ibex::pow(ibex::ExprConstant::new_scalar(0.5), 1075));
ibex::IntervalVector box(1, ibex::Interval(DBL_TRUE_MIN, POS_INFINITY));
f.backward(ibex::Interval::zero(), box);   // pre-fix: box[0] becomes EMPTY  -> false unsat
```

The forward `pow(0.5,1075)` soundly yields `[0, DBL_TRUE_MIN]`; the subtraction backward pins the
`pow` output to the singleton `{DBL_TRUE_MIN}`; pre-fix `bwd_pow` then inverts that singleton via
gaol's **tight** `nth_root` (≈ 0.50034), which *excludes the feasible base 0.5* and empties the
box. The five backward ops that invert via a tight gaol primitive are affected: `bwd_pow`,
`bwd_exp`, `bwd_sqr`, `bwd_mul`, `bwd_div`. The siblings `bwd_sqrt`/`bwd_log`/`bwd_root` invert via
a *loose forward* op instead and were already sound (we audited and pinned them).

**Fix.** Before the tight inverse, saturate a backward target that lies **entirely** in the
subnormal band to include `0` (it can only have come from an underflowed forward result):

```cpp
inline Interval underflow_saturate(const Interval& y) {
  const double tiny = std::numeric_limits<double>::min();  // smallest normal
  if (0.0 < y.lb() && y.ub() <= +tiny) return Interval(0.0, y.ub());
  if (-tiny <= y.lb() && y.ub() < 0.0) return Interval(y.lb(), 0.0);
  return y;
}
```

> Two subtleties cost us debugging time: (1) key on the endpoint **farthest** from 0 (`ub` for a
> positive interval), so a target that merely reaches *into* the subnormal range but extends into
> the normal range (e.g. `[DBL_TRUE_MIN, +inf]`) is left alone — it has normal-magnitude
> preimages and must not be widened. (2) `bwd_div` needs its **own** saturate; it is not fixed
> transitively through its internal `bwd_mul`.

`underflow_saturate` only ever *enlarges* the backward target, so it can never prune a feasible
point → it cannot introduce a false `unsat`. The trade-off is the standard one: it *weakens*
contraction at the subnormal scale, so it can also stop *legitimate* pruning of a robustly-`unsat`
subnormal infeasibility (we saw exactly one benchmark flip robust-`UNSAT` → `delta-sat`). That is
acceptable for a delta-complete solver — a false `unsat` is catastrophic, an over-permissive
`delta-sat` is within the delta-completeness guarantee.

---

## A note on the overflow twin

The fold-level fix is symmetric for overflow (`finite → ±inf` brackets to `[DBL_MAX, +inf]` /
`[-inf, -DBL_MAX]`), but we did **not** chase end-to-end overflow *comparisons*, which hit a
separate `±inf`-handling concern. For the record, on the pre-fix build
`(< (^ 10 400) (^ 10 401))` (true: `10^400 < 10^401`) **aborts with `NaN is detected while
visiting an expression`** (from `inf − inf`); post-fix it no longer crashes but still resolves to
`unsat` rather than `delta-sat`. Flagging it as a related, not-fully-resolved facet of the same
category.

---

## Verification

Repros run with `--precision 0.001` on two builds of our CMake port of dReal4:

- **before (buggy):** dreal4 `96fc11879` (ibex fork `cf3928c7`) — neither fix present.
- **after (fixed):** dreal4 `44d503d53` (ibex fork patch #12, `d9930909`).

```
$ for f in 0*.smt2; do echo "$f: $(./dreal4 --precision 0.001 $f)"; done   # before-binary
01_pow_fold_underflow.smt2: unsat
02_mul_fold_underflow.smt2: unsat
03_div_fold_underflow.smt2: unsat
04_faithful_control_unsat.smt2: unsat
                                                                           # after-binary
01_pow_fold_underflow.smt2: delta-sat with delta = 0.001
02_mul_fold_underflow.smt2: delta-sat with delta = 0.001
03_div_fold_underflow.smt2: delta-sat with delta = 0.001
04_faithful_control_unsat.smt2: unsat
```

(These are from a downstream CMake port + ibex fork, but the root causes are in shared code —
the Drake symbolic constant-fold and the ibex HC4 backward — so they should reproduce on a
mainline build with an equivalent ibex.)
