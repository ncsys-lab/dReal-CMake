# Step control: `MpStepControl.h` — `MpLastTermsStepControl`

## What it is
The **multiprecision** analog of `ILastTermsStepControl`: `MpLastTermsStepControl` (+ its
`StepControlInterface<MpLastTermsStepControl, Scalar>` specialization), for solvers whose
scalar type is `capd::multiPrec::MpReal` rather than `double`. Same last-terms prediction
logic, MpReal arithmetic. (`MpStepControl.h` page.)

## dReal status
**Unused and inapplicable.** dReal builds CAPD with `CAPD_INTERVAL_TYPE=NATIVE` (double-based
intervals — see CLAUDE.md / dreal-capd-usage.md), so the MpReal step-control path is never
instantiated.

## Why it might matter
Only relevant if dReal ever needed multiprecision intervals (e.g. to push enclosure precision
far below double `1e-10` for an extremely ill-conditioned flow). That would be a large build
change (`CAPD_INTERVAL_TYPE`, the `Mp*` solver typedefs) and is orthogonal to the C0→C1
narrowing opportunities. Not a near-term lever.

## Source
[MpStepControl.h](../../../../CAPD/docs/html/MpStepControl_8h.html)
