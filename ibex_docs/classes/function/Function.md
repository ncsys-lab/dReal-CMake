# `Function` — symbolic function (DAG) + numerical operations

Header: [`ibex_Function.h`](../../../../ibex-fork/src/function/ibex_Function.h)
(`function/`). `x ↦ f(x)` as a DAG over an arithmetic expression. The object dReal
builds per theory atom and contracts.

> **dReal status:** central, and the **most-patched** surface (6 of 12 fork
> patches). `generic_contractor_generator.cc` builds it from `ExprNode`/
> `ExprSymbol`/`ExprConstant`; the contraction hot loop is `Function::backward`.

## The operations that matter

| Call | dReal | Note |
|---|---|---|
| `eval / eval_vector / eval_matrix(box)` | indirect | forward interval evaluation |
| **`backward(y, box, callback)`** | **the hot loop** | HC4Revise; the **callback** `(var_idx, before, after)` is fork #2/#5/#6/#7, used for theory lemmas |
| `gradient(box[, g])` | **cold** | the eager alloc fork #1 made lazy (dReal never used it) |
| `jacobian(box[, J])`, Hansen matrix | no | needed by [`CtcNewton`](../contractors/CtcNewton.md) — reintroduces the gradient cost |
| copy with `Function::DIFF` | no | symbolic differentiation |

## DAG sharing (the expression-level dependency-problem fix)

Build with shared `const ExprNode&` subexpressions → a DAG, not a tree ("a gain in
performance"). This is the *symbolic* analogue of the interval-level dependency
problem: evaluating a shared subexpression once avoids re-widening it. Allowed
symbols: `sign min max sqr sqrt exp log pow cos sin tan acos asin atan cosh sinh
tanh acosh asinh atanh atan2` (no `^`).

## Fork-patch interactions to respect

- The backward **callback contract** (#2,#5,#6,#7) must survive any new contractor
  dReal layers on (else lemma precision drops).
- "Domain emptied" is a **`bool` return** now (#11), not a thrown
  `EmptyBoxException` — borrowed contractors must use `is_empty()`.
- `gradient` is **lazy/cold** (#1) — Jacobian/Newton ideas pay that build back.

Related: [function chapter](../../chapters/function.md),
[`../contractors/CtcFwdBwd.md`](../contractors/CtcFwdBwd.md).
