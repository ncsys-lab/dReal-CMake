# C0BallSet

## What it is
A C0 set stored as a **norm ball**: `x + Ball(r)` — a center `x` and a radius `r` measured
in a supplied norm, with **no coordinate frame at all** (confirmed `C0BallSet.h` /
"the set is represented as: x + Ball(r)"). For flows the radius grows as
`r = exp(Lip(vectfield)·h) + errors` (confirmed `C0BallSet.h`) — a Lipschitz blow-up bound.
The minimal set representation; predates the doubleton machinery.

## Key API
- Constructors `<MatrixT>`: `C0BallSet(x, NormType& n[, t])` and `C0BallSet(x, r, NormType& n[, t])` — a **`NormType` is mandatory** (the ball needs a norm).
- `move(DynSys&, C0BallSet& result)` — one step; radius updated by the Lipschitz bound.
- `operator VectorType()` — cast to the enclosing `IVector`.

## dReal status
**Not used.** dReal wires only doubleton/tripleton/HO C0 sets. `C0BallSet` /
`C0FlowballSet` are not exposed by any `--ode-c0-set` value.

## Why it might matter
Mostly a non-candidate. With no frame, the ball cannot track the flow's deformation, so it
suffers the **full wrapping effect** — the module page lists Ball/FlowBall among methods
"implemented in the past but now **not recommended**" (`dynset_module.html`). Worth knowing
only to recognize the name and to understand the baseline the doubleton improves on: a
single growing ball is exactly what the `C`/`B` split was invented to beat.

## Source
[classcapd_1_1dynset_1_1C0BallSet.html](../../../../CAPD/docs/html/classcapd_1_1dynset_1_1C0BallSet.html)
