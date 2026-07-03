# Dynsets and the Wrapping Effect

## The wrapping effect
A rigorous ODE step takes an enclosure of the state at time `t` and produces an enclosure at `t + h`. Even when the true flow only *rotates and shears* a small set, if that set is stored as an axis-aligned interval box, each step must **re-wrap** the sheared image in a new axis-aligned box. The wrapping box is strictly larger than the image, and the excess compounds geometrically across steps — a rotating linear system can blow a tiny initial box up by orders of magnitude in a few periods. This **overestimation, not the solver's local error, is what loses a tube** (and, for dReal, what causes a `forall_t` invariant to be missed → a **COMPLETENESS** loss, never a soundness loss: a looser tube can only *fail to refute*, never wrongly refute).

CAPD's answer is to **store the set in a moving coordinate frame** that tracks the flow's deformation, so the re-enclosure box stays tight. The set-representation zoo (`dynset` module) is exactly the menu of how that frame is maintained.

> **The complete menu, side by side:** [`../classes/sets/COMPARISON.md`](../classes/sets/COMPARISON.md) — every CAPD set type (all jet orders × geometries × QR policies) in one strengths/weaknesses table, with ordered recommendations for dReal. That table is the practical entry point; the sections below explain the mechanisms it summarizes.

## Doubleton — the workhorse
A **doubleton** stores the set as `x + C*r0 + B*r` (confirmed `C0DoubletonSet.h`):
- `x` — point center,
- `C*r0` — the *initial* set, in its own frame `C` whose deformation tracks how the flow stretched the original box,
- `B*r` — *accumulated errors* (round-off, Taylor truncation) in a separate frame `B`.

Splitting initial-size from error lets each be carried in the frame that keeps it tight, instead of merging both into one ever-growing box. The **Rect method** keeps `B` orthogonal (a rotated rectangle); the **Pped method** lets it be a general parallelepiped.

## Tripleton — a tighter error term
A **tripleton** (`C0TripletonSet`) refines the error term to `x + C*r0 + intersection(B*r, Q*q)` (confirmed `C0TripletonSet.h`): the accumulated error is enclosed in **two** frames `B` and `Q` (Q kept near-orthogonal) and **intersected**. Intersecting two valid enclosures is never looser than either and usually tighter, at the cost of a second frame and its rigorous inverse. This is CAPD's own `DefaultC0Set`.

## Higher-Order (HO) — a tighter time step
A **HO set** (`C0HOSet<BaseSet>`) computes each step's image two ways — the **Taylor** method and the **Hermite–Obreshkov** method — and **intersects** them (confirmed `C0HOSet` detail; the base set can itself be a doubleton or tripleton). HO is implicit and higher-order-accurate, so its enclosure of the *time* remainder is tighter; the intersection inherits the smaller. Caveat from the docs: order ≤ 64 for `C0HOSet`, ≤ 32 for `C0HODoubletonSet` (binomial-coefficient integer capacity). The cost is roughly two integrations per step.

## Reorganization — resetting the frame before it goes singular
Over many steps the frames `C`, `B` themselves degrade: dominant expanding directions make the frame nearly singular, and `r` (errors) can grow larger than `r0` (initial size), so the `C/B` split stops helping. **Reorganization** periodically rebuilds the representation:
- `FactorReorganization` — triggers when `size(r) > factor·size(r0)` (confirmed `FactorReorganization.h`); this is dReal's policy.
- `CanonicalReorganization` — reset `C,B` to identity, fold everything into `r0`.
- `SwapReorganization`, `CoordWiseReorganization` — move the dominant error direction into the initial-size frame.
- `QRReorganization` — re-orthogonalize `B`.
- `NoReorganization` — never (for sets that don't need it).

Paired with reorganization is the **QR policy** that re-orthogonalizes the frame each step: `FullQRWithPivoting`, `PartialQRWithPivoting<N>`, `SelectiveQRWithPivoting` (orthogonalize only near-parallel vectors — cheaper), `InverseQRPolicy`.

## Which set for which flow shape
| Flow shape | Tightest wired choice | Why |
|---|---|---|
| Mild / short tube | `C0Rect2Set` (doubleton) | cheapest; orthogonalized frame already fights wrapping |
| Strong rotation / stiff, eigenvalues of different magnitude | `C0TripletonSet` | dual-frame intersection on the error term |
| Long integration where time-remainder dominates | `C0HORect2Set` | Taylor∩HO shrinks the per-step remainder |
| Near elliptic fixed point, equal-magnitude eigenvalues | *Pped method* (unwired) | parallelepiped beats orthogonalized rect; but blows up elsewhere |

## dReal mapping (ground truth, `dynset/typedefs.h`)
- `--ode-c0-set rect2` → `C0Rect2Set = C0DoubletonSet<IMatrix, C0Rect2Policies>` (**default**)
- `--ode-c0-set horect2` → `C0HORect2Set = C0HOSet<C0Rect2Set>`
- `--ode-c0-set tripleton` → `C0TripletonSet<IMatrix, C0Rect2Policies>`

All three pin `C0Rect2Policies = FactorReorganization<FullQRWithPivoting<>>` — so dReal already gets QR-with-pivoting + factor reorganization on every set, but the policy itself is **not exposed**. Notably CAPD's library default is the *tripleton*, while dReal defaults to the cheaper *doubleton*.

## Source
[dynset_module.html](../../../CAPD/docs/html/dynset_module.html) · [group__dynset.html](../../../CAPD/docs/html/group__dynset.html) · [C0DoubletonSet](../../../CAPD/docs/html/classcapd_1_1dynset_1_1C0DoubletonSet.html) · [C0TripletonSet](../../../CAPD/docs/html/classcapd_1_1dynset_1_1C0TripletonSet.html) · [C0HOSet](../../../CAPD/docs/html/classcapd_1_1dynset_1_1C0HOSet.html)
