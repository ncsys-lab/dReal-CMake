# `CtcInverse` — inverse-image contraction `f⁻¹(C)` (audit D)

Header:
[`ibex_CtcInverse.h`](../../../../ibex-fork/src/contractor/ibex_CtcInverse.h)
(`contractor/`). `class CtcInverse : public Ctc`. Given a function `f` and a
contractor `C` defined on the *image* space, contracts `[x]` to the inverse image
of what `C` keeps: `[x] ↦ [x] ∩ f⁻¹(C(f([x])))`.

> **dReal status:** **unused.** Niche — no obvious dReal use shape.
> [`../../AUDIT.md`](../../AUDIT.md) D4.

## Constructor (verbatim)

```cpp
CtcInverse(Ctc& c, Function& f);
```

Nothing tunable: just the inner contractor `c` (acting on the image) and the map
`f`. `nb_var` is taken from `f.nb_var()`; the image dimension from `f.image_dim()`.

## Algorithm (from `ibex_CtcInverse.cpp`)

1. `fx = f.eval_domain(box)` — forward-evaluate `f` over `[x]`.
2. project `fx` onto a fresh image box `y` via an identity function `id->backward`.
3. `c.contract(y, …)` — run the **inner** contractor on the image box.
4. if `y` empties → `box` empties (`FIXPOINT`); if `c` reports `INACTIVE`, stop;
   otherwise `f.backward(id->eval_domain(y), box)` pulls the contracted image back
   onto `[x]`.

So it is HC4-style forward/backward *wrapped around an arbitrary image-space
contractor* — the inverse-image generalization of [`CtcFwdBwd`](./CtcFwdBwd.md)
(which is the special case where the image constraint is `f(x) ∈ [y]`).

## Why dReal skips it

dReal's theory atoms are already `f(x) ∈ [y]` constraints handled directly by the
callback fwd-bwd path; it never needs to compose an *arbitrary* contractor through
an outer function. No instance shape calls for it. Recorded so future sessions
don't re-investigate.

Related: [`CtcFwdBwd.md`](./CtcFwdBwd.md), [function chapter](../../chapters/function.md),
[contractor chapter](../../chapters/contractor.md).
