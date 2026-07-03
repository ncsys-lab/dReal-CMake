# Rect2 / QR Policy (the coordinate-frame re-orthogonalization)

## What it is
The "Rect2" in `C0Rect2Set` is not a separate class — it is a **policy bundle** plugged into the generic doubleton/tripleton via the `Policies` template parameter. It controls how the error frame `B` is **re-orthogonalized (QR)** each step and **reorganized** periodically. This is the actual machinery that makes a doubleton fight wrapping.

dReal's bundle (ground truth, `dynset/typedefs.h:24`):
```
typedef FactorReorganization< FullQRWithPivoting<> > C0Rect2Policies;
```
i.e. **full QR with column pivoting** per step + **factor-triggered reorganization**.

## The QR policies (`dynset/QRPolicy.h`)
- **`FullQRWithPivoting<BasePolicy>`** — full Gram–Schmidt/QR with column pivoting on the frame `B`; dReal's choice. Most thorough, most cost.
- **`PartialQRWithPivoting<N, BasePolicy>`** — QR on only the first `N` columns.
- **`SelectiveQRWithPivoting<BasePolicy>`** — "vectors are orthogonalized only if they are close to be parallel" (confirmed class detail) — skips work when the frame is already well-conditioned. Cheaper, can match Full on benign flows.
- **`InverseQRPolicy<BasePolicy>`** — used by the Pped facade (`C0PpedPolicies = InverseQRPolicy<>`).
- `IdQRPolicy`, `DefaultPolicy` — identity / base.

## dReal status
**Used implicitly, not configurable.** Every wired C0 set (`rect2`, `horect2`, `tripleton`) shares `C0Rect2Policies`. There is **no `--ode-*` flag** to change the QR policy or the reorganization factor — they are compiled in.

## Why it might matter
`FullQRWithPivoting` is the **most expensive** QR option; `SelectiveQRWithPivoting` does the same orthogonalization only when vectors are near-parallel, which is the only time it changes the enclosure. On benchmark families where flows stay well-conditioned, switching to Selective could cut per-step cost with **no tightness loss** (same enclosure when nothing is near-parallel) — a pure speed win and a low-risk experiment, but it requires exposing the policy (a new typedef + flag), since it is currently hard-wired.

## Source
[classcapd_1_1dynset_1_1FullQRWithPivoting.html](../../../../CAPD/docs/html/classcapd_1_1dynset_1_1FullQRWithPivoting.html) · [classcapd_1_1dynset_1_1SelectiveQRWithPivoting.html](../../../../CAPD/docs/html/classcapd_1_1dynset_1_1SelectiveQRWithPivoting.html) · [classcapd_1_1dynset_1_1QRReorganization.html](../../../../CAPD/docs/html/classcapd_1_1dynset_1_1QRReorganization.html)
