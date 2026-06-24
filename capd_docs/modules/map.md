# Map (group `map`)

**What it is.** The vector-field / map representation layer. `capd::map::Map<MatrixT>`
(typedef `IMap = Map<IMatrix>`) parses a human-readable RHS string (or a C-routine of
`autodiff::Node`s) into an automatic-differentiation DAG, and evaluates the map's value,
Jacobian (`derivative`), Hessian, jet, and — crucially for ODEs — the iterative Taylor
coefficients of the *solution* (`computeODECoefficients`). `Function<VectorT>` is the
scalar-valued sibling.

**Key contents.**
- `IMap` — dReal's vector field. Built once per flow (parse + AD-DAG), cached.
- Parse syntax: `"[par:...;][time:t;]var:x1,...;fun:expr1,...;"`. No scientific-notation
  constants (use parameters). Forward `f(x)` and backward `−f(x)` are separate `IMap`s in dReal.
- `setParameter(d, value)` / `setParameters(...)` rebind parameters **without** re-parsing.
- `computeODECoefficients(...)` overloads optionally also produce first/second variational
  (derivative/Hessian) coefficients — the hook the C1/C2 ODE solvers use.

**dReal status.** Used. `IMap` is the parsed cached vector field; parameters are rebound
per ICP call. Jacobian/Hessian/jet evaluation and the variational `computeODECoefficients`
overloads are unused. See `dreal-capd-usage.md`.

**Why it might matter.** The parse + `createEvalPath` DAG build is the per-flow setup cost
(already amortized by caching). The C-routine (`autodiff::Node`) parse path is documented as
producing identical-performance evaluation but avoids string parsing at construction.

**Source.** [group__map.html](../../../CAPD/docs/html/group__map.html)
