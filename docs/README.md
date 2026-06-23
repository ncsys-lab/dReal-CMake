# dReal4 Documentation

Deeper reference documentation beyond the project-level `CLAUDE.md`.

| Document | Contents |
|---|---|
| [architecture.md](architecture.md) | Full solving pipeline — preprocessing, SAT/theory layers, ICP loop, branching, Box, ContractorStatus, UNSAT explanations |
| [contractors.md](contractors.md) | All contractor kinds, how HC4 forward-backward works, fixpoint/worklist variants, join, how TheorySolver composes them |
| [ode-integration.md](ode-integration.md) | CAPD order-20 Taylor integration (forward/backward/trace), hybrid system encoding, input format examples |
| [decisions.md](decisions.md) | Architecture & soundness ADRs — ODE backend (Codac→CAPD-only), per-slice tube, ODE feed faithfulness, denormal/underflow (#321), backward narrowing |
| [pattern-matching.md](pattern-matching.md) | CAV26 lemma reuse — De Bruijn canonicalization, substitution tree matching, bijection checking, symmetry filtering, CaDiCaL integration |
| [api-guide.md](api-guide.md) | C++ API: Variable/Expression/Formula/Box types, CheckSatisfiability, Minimize, Config options, incremental Context, linking |
| [qf_nra_ode_semantics.md](qf_nra_ode_semantics.md) | Deep QF_NRA_ODE semantics: define-ode vars vs. pars, integral grammar and invariants, forall_t linking and flattening, negated ODE constraints (semantically ignored), edge cases grounded in parser and C++ source |
| [syntax-reference.md](syntax-reference.md) | Complete SMT2 syntax reference: all sorts (Real/Int/Bool), numeric literal forms, every expression operator and trig function (including arcsin aliases, xor n-ary semantics, ite desugaring), quantifiers, let, all solver commands, edge cases |
