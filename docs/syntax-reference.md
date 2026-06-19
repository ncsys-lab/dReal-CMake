# SMT2 Syntax Reference

Complete reference for all sorts, operators, numeric literals, and commands accepted by the
dReal4 SMT2 parser. Every entry is grounded in `src/dreal/smt2/scanner.ll`,
`src/dreal/smt2/parser.yy`, and `src/dreal/smt2/sort.cc`.

---

## Sorts

Declared with the `SYMBOL` token and dispatched by `ParseSort` in `sort.cc`:

| SMT2 sort | `Variable::Type` | Notes |
|---|---|---|
| `Real` | `CONTINUOUS` | Default for arithmetic; used for ODE state variables |
| `Int` | `INTEGER` | Integer-valued; rounded to integer bounds by `ContractorInteger` |
| `Bool` | `BOOLEAN` | Boolean; handled separately in the SAT layer |

---

## Declare / Define Commands

```smt2
(declare-fun x () Real)                   ; unbounded Real
(declare-fun x () Real [lb ub])           ; Real with domain [lb, ub]
(declare-const x Real)                    ; same as declare-fun with no params
(declare-const x Real [lb ub])

(define-fun f ((a Real) (b Real)) Real e) ; function macro (inlined at call site)
(define-fun c () Real e)                  ; zero-param: treated as `x = e` constraint
```

`define-fun` with parameters creates a `FunctionDefinition` entry. Each call site substitutes
the arguments and returns the instantiated term. There is no recursive function support
(`define-fun-rec` token is recognized but produces a parse error if used).

`define-fun` with zero parameters is treated as a variable declaration plus an equality assertion:
`(declare-fun c () Real)` + `(assert (= c e))`.

---

## Numeric Literals

| Form | Token | Example | Notes |
|---|---|---|---|
| Integer | `INT` | `42`, `-7` | Parsed as `int64_t`; out-of-range throws |
| Decimal | `DOUBLE` | `3.14`, `-1e-5`, `2.` | Returned as `std::string`; converted with `stod` under `FE_TONEAREST` |
| Hex float | `HEXFLOAT` | `0x1.8p+1` | Parsed directly to `double` |
| Interval | — | `3.14` (with rounding) | `StringToInterval` converts the decimal string to a tight `ibex::Interval`; if `diam > 0` (rounding artefact), `real_constant(lb, ub, ...)` is used instead of a point |

Integer literals are converted to `double` in expression context (`convert_int64_to_double`).

---

## Expression Operators

All take `Expression` arguments and return `Expression`.

### Arithmetic

| Token | Syntax | Notes |
|---|---|---|
| `+` | `(+ e)` or `(+ e e*)` | Unary `+` is identity; n-ary left-fold |
| `-` | `(- e)` or `(- e e*)` | Unary negation; n-ary left-fold subtraction |
| `*` | `(* e e*)` | n-ary left-fold |
| `/` | `(/ e e*)` | n-ary left-fold |
| `pow` / `^` | `(pow e e)` | Both spellings map to `TK_POW`; `^` is also accepted |
| `sqrt` | `(sqrt e)` | |
| `abs` | `(abs e)` | |
| `min` | `(min e e)` | Binary only |
| `max` | `(max e e)` | Binary only |
| `exp` | `(exp e)` | |
| `log` | `(log e)` | Natural log |

### Trigonometry

| Token | Syntax | Aliases |
|---|---|---|
| `sin` | `(sin e)` | — |
| `cos` | `(cos e)` | — |
| `tan` | `(tan e)` | — |
| `asin` | `(asin e)` | `arcsin` |
| `acos` | `(acos e)` | `arccos` |
| `atan` | `(atan e)` | `arctan` |
| `atan2` | `(atan2 e e)` | `arctan2` |
| `sinh` | `(sinh e)` | — |
| `cosh` | `(cosh e)` | — |
| `tanh` | `(tanh e)` | — |

The `arc*` aliases are handled in the lexer: `"asin"|"arcsin"` both emit `TK_ASIN`, etc.

**CAPD translator support**: the ODE RHS translator `to_capd_string`
(`src/dreal/contractor/odes/to_capd_string.h`) covers essentially the full QF_NRA operator set —
including `log`, `asin`/`acos`/`atan`/`atan2`, `sinh`/`cosh`/`tanh`, `min`/`max`, and `abs` (the
ones `capd::IMap` lacks are emulated, e.g. `abs` → `sqrt(sqr(·))`, `tan` → `sin/cos`). Only
`IfThenElse`, `UninterpretedFunction`, and `NaN` are unsupported; encountering one makes the
per-flow cache build (`make_capd_ode_cache`) **raise** at contractor-build time (it does not
silently disable integration).

---

## Formula Connectives

All take `Formula` arguments and return `Formula` (except comparisons, which take `Expression`).

| Token | Syntax | Semantics |
|---|---|---|
| `=` | `(= e e)` | Equality: `e1 == e2` for expressions; `⟺` for formulas |
| `<` | `(< e e [prec])` | Strict less-than; optional `[prec]` ignored (dReal3 compat) |
| `<=` | `(<= e e [prec])` | Less-than-or-equal |
| `>` | `(> e e [prec])` | Strict greater-than |
| `>=` | `(>= e e [prec])` | Greater-than-or-equal |
| `and` | `(and f* [prec])` | n-ary conjunction; optional trailing `[prec]` ignored |
| `or` | `(or f*)` | n-ary disjunction |
| `xor` | `(xor f*)` | n-ary exclusive-or; folded as `(f1 ⊕ f2) ⊕ f3 ...` |
| `not` | `(not f)` | Negation |
| `=>` | `(=> f f)` | Implication; desugared to `(or (not f1) f2)` |
| `ite` | `(ite f e e)` | If-then-else over expressions; `(ite f f f)` for formulas |

**`ite` desugaring**: expression ITE becomes `if_then_else(cond, e1, e2)` in the AST and is later
eliminated by `IfThenElseEliminator` before the SAT/theory split. Formula ITE
`(ite cond f1 f2)` is desugared inline by the parser to `(¬cond ∨ f1) ∧ (cond ∨ f2)`.

**`xor` semantics**: the parser folds left:
```
(xor a b c)  →  ((a ∧ ¬b) ∨ (¬a ∧ b)) ∧ ... fold with c
```
This is n-ary odd-parity XOR.

---

## Quantifiers

### `forall` (real-variable quantification)

```smt2
(forall ((x Real) (y Real [0.0 1.0])) body)
```

Produces a `Formula::Forall` node with domain constraints `imply(lb ≤ x ≤ ub, body)`. Variables
that do not appear free in `body` are silently dropped. Boolean-typed quantified variables are
eliminated by case-splitting (`f[b↦true] ∧ f[b↦false]`) before the `forall` node is created.
Handled by `ContractorForall` in the contractor layer.

### `exists` (token only — no grammar production)

The lexer emits `TK_EXISTS` for the keyword `exists` but the parser has no grammar rule consuming
it. Writing `(exists ...)` causes a parse error:

```
syntax error, unexpected TK_EXISTS
```

Existential quantification cannot be expressed directly. The standard workaround is to declare the
witness variable as a free `declare-fun` variable and assert the body.

---

## `let` Bindings

```smt2
(let ((a e1) (b e2)) body)
```

Bindings are processed simultaneously (not sequentially): all RHSs are evaluated before any
binding takes effect. Each binding introduces a fresh internal variable with a unique mangled name
and asserts `var = rhs`. The body is then returned with the bindings in scope.

For formula-valued bindings: `(b ∧ rhs) ∨ (¬b ∧ ¬rhs)` is asserted (biconditional).
For expression-valued bindings: `var = rhs` is asserted as an equality constraint.

---

## ODE-Specific Keywords

| Keyword | Token | See |
|---|---|---|
| `define-ode` | `TK_DEFINEODE` | `docs/qf_nra_ode_semantics.md` §3 |
| `d/dt` | `TK_DDT` | `docs/qf_nra_ode_semantics.md` §3 |
| `integral` | `TK_INTEGRAL` | `docs/qf_nra_ode_semantics.md` §4 |
| `forall_t` | `TK_FORALLT` | `docs/qf_nra_ode_semantics.md` §5 |

---

## Optimization Commands

```smt2
(minimize e)   ; minimizes expression e subject to all asserted constraints
(maximize e)   ; maximizes expression e subject to all asserted constraints
```

Both delegate to `context_.Minimize` / `context_.Maximize`. Implemented via NLopt; finds a local
optimum, not necessarily global.

---

## Solver Commands

| Command | Notes |
|---|---|
| `(set-logic SYMBOL)` | Sets the logic; `QF_NRA` or `QF_NRA_ODE` are the relevant values |
| `(assert f)` | Adds formula `f` to the current assertion set |
| `(check-sat)` | Runs the solver; prints `delta-sat ...` or `unsat` |
| `(push N)` | Pushes `N` scope levels |
| `(pop N)` | Pops `N` scope levels |
| `(get-model)` | Prints the model from the last `check-sat`; requires `--produce-models` |
| `(get-value (t*))` | Evaluates a list of terms in the current model |
| `(get-option :key)` | Queries a solver option |
| `(set-option :key v)` | Sets a solver option (bool, numeric, or symbol value) |
| `(set-info :key v)` | Metadata; `:precision` sets the solver delta if recognized |
| `(exit)` | Terminates parsing immediately (`YYACCEPT`) |
| `(echo STRING)` | Token recognized but has no grammar production; causes parse error |

---

## User-Defined Functions

```smt2
(define-fun f ((x Real) (y Real)) Real (+ x y))
(assert (= z (f a b)))   ; expands to (assert (= z (+ a b)))
```

Functions are expanded inline at each call site via `Term::Substitute`. There is no memoization;
each call performs a full substitution traversal. Recursive calls are not possible (no
`define-fun-rec` production).

---

## Comments

```smt2
; everything from semicolon to end of line is a comment
```

The lexer rule `";".*[\n\r]+` consumes comments including the trailing newline.
