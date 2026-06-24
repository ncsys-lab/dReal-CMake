# Autodiff (group `autodiff`)

**What it is.** The automatic-differentiation engine under `capd::map`. A parsed RHS
expression becomes a DAG of `autodiff::Node`s; the `group__autodiff` namespace list is one
namespace *per elementary-operation/operand-kind combination* (e.g. `Add`, `ConstPlusVar`,
`DivVarByConst`, `Exp`, `ExpTime`, `Sin`, `HalfIntPow`, …). Each encodes the recurrence that
propagates Taylor coefficients (and their space-derivatives) through that operation. This is
how `IMap` computes the order-N solution coefficients an `IOdeSolver` step needs.

**Key contents.**
- `capd::autodiff::Node` — the expression-graph node type also used to define a map from a
  C-routine: `void vf(Node t, Node in[], int dimIn, Node out[], int dimOut, Node par[], int noPar)`.
- `DagIndexer` — the coefficient store / indexer (`Map::DAG`).
- The per-operation namespaces are the AD recurrence rules; not called directly by users.

**dReal status.** Used transitively — every `IMap` evaluation and Taylor step runs this AD
machinery. dReal builds `IMap` from a *string* (not the `Node` C-routine path). See
`dreal-capd-usage.md`.

**Why it might matter.** This is the inner loop of every ODE step. The `Node` C-routine
construction is an alternative front-end (no string parse) with documented identical
evaluation speed; relevant only if `IMap` *construction* (not evaluation) is a bottleneck.

**Source.** [group__autodiff.html](../../../CAPD/docs/html/group__autodiff.html)
