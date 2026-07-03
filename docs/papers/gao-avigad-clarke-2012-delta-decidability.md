# Delta-Decidability over the Reals

**File:** `papers/gao-avigad-clarke-2012-delta-decidability.pdf`
**Authors:** Sicun Gao, Jeremy Avigad, Edmund M. Clarke (Carnegie Mellon University)
**Venue:** 27th ACM/IEEE Symposium on Logic in Computer Science (LICS 2012), pp. 305–314,
DOI 10.1109/LICS.2012.41.

> Lineage position: **theory** (this paper) $\to$ procedure ([gao-avigad-clarke-2012-delta-complete](gao-avigad-clarke-2012-delta-complete.md))
> $\to$ tool ([gao-kong-clarke-2013-dreal](gao-kong-clarke-2013-dreal.md)) $\to$ $\exists\forall$ extension
> ([kong-solar-lezama-gao-2018-exists-forall](kong-solar-lezama-gao-2018-exists-forall.md)). This is the decidability/complexity
> foundation the whole stack rests on.

## Core Claim

The classical first-order theory of the reals with $\sin$ (or any signature extending real
arithmetic with the sine function) is **undecidable** — deciding formulas *symbolically and
precisely* is impossible. This paper shows that a slight shift of perspective makes the
picture "completely different, and much more positive": if one only asks to decide formulas
up to a numerical perturbation $\delta$, then for *any* collection $F$ of **Type-2 computable**
real functions and any **bounded** sentence, the **$\delta$-decision problem** is decidable —
and lands in surprisingly low complexity classes. This is the theoretical justification for
building numerically-driven decision procedures (interval arithmetic, validated ODE solvers)
and trusting their answers in correctness-critical settings.

## Key Results

- **$\delta$-strengthening / $\delta$-weakening** (Def. 17). For a bounded $\mathcal{L}_F$-sentence
  $\varphi$ in normal form, $\varphi^{+\delta}$ replaces each atom $t_i > 0$ by $t_i > \delta$
  and $t_j \geq 0$ by $t_j \geq \delta$ (harder to be true); $\varphi^{-\delta}$ relaxes them to
  $t_i > -\delta$, $t_j \geq -\delta$ (easier). **Quantifier bounds are not changed.**
- **Main theorem** (Thm. 22). There is an algorithm that, given any bounded
  $\mathcal{L}_F$-sentence $\varphi$ and any $\delta \in \mathbb{Q}^+$, correctly returns one of:
  "**True**: $\varphi$ is true" or "**$\delta$-False**: $\varphi^{+\delta}$ is false." The two
  cases can overlap, and then **either answer is permitted** — this overlap is the designed
  $\delta$-slack.
- **$\delta$-robustness** (Def. 19) and **robustness $\Rightarrow$ decidability** (Cor. 24). A
  sentence is $\delta$-robust if $\varphi^{-\delta} \to \varphi$; robust sentences are decided
  exactly. Both **boundedness and robustness are necessary** (§VIII): drop either and
  $\mathcal{L}_F$-sentences over arbitrary Type-2 functions are undecidable (Props. 40, 41).
- **Complexity** (§VII). For $\{+, \times, \exp, \sin\}$, the $\delta$-decision problem for
  bounded $\Sigma_1$-sentences is **NP-complete** (Cor. 38); for bounded $\Sigma_n$ it is
  $(\Sigma_n^P)^{\mathsf{C}}$ (Thm. 36); for Lipschitz-continuous **ODEs** over compact domains
  it is **PSPACE-complete** (Cor. 39). Striking: arbitrarily-quantified bounded sentences with
  Lipschitz ODEs are no harder than deciding quantified Boolean formulas.
- **Machinery** (§II): computable analysis / Type-2 computability — names (Cauchy sequences of
  dyadics), computable uniform modulus of continuity (Thm. 6), Type-2 complexity classes
  $\mathsf{P}_{\mathsf{C}[0,1]}$, $\mathsf{PSPACE}_{\mathsf{C}[0,1]}$. Strictification (Def. 27)
  lets the $\delta$-band absorb the strict/non-strict distinction.

## Systems / Functions This Paper Motivates

| Paper concept | Where it lives in this project |
|---|---|
| The $\delta$-decision contract ("True" vs "$\delta$-False", overlap $\Rightarrow$ either answer) | `docs/soundness-vs-completeness.md` "What dReal's two answers guarantee" — the `unsat` / `delta-sat` split and the designed $\delta$-slack |
| $\delta$-strengthening / $\delta$-weakening of atoms | `DeltaStrengthen` in `src/dreal/symbolic/symbolic.cc` (the operator the $\forall$-contractor strengthens $\neg\varphi$ with); the four T-relations in `soundness-vs-completeness.md` |
| Boundedness requirement on quantifiers | `docs/forall-semantics.md` §2.2 / §6.4 — bounded `forall` domains required in practice |
| PSPACE-completeness of Lipschitz-ODE decision | `docs/ode-integration.md` — theoretical reason ODE handling is "hard but possible," motivating the CAPD interval-enclosure backend |
| Model-theory grounding | `smt-model-theory` skill — `references/02-satisfaction-and-validity.md` |

## Open Proof Targets

- Sanity check on a project benchmark: pick a robustly-true $\exists$ instance, confirm the
  reported `delta-sat` witness $a$ satisfies $\varphi^{-\delta}(a)$ exactly (rational
  evaluation) — the operational content of "$\delta$-False is correct."
- For an ODE benchmark, relate the PSPACE-completeness bound to the observed CAPD cost growth
  in $\dim$ and integration time (`docs/ode-integration.md` §Performance).

## Notes

- **This is pure theory — no direct code maps to it.** It is the *why* behind the framework,
  not an algorithm in the solver. The algorithm is the IJCAR'12 companion
  ([gao-avigad-clarke-2012-delta-complete](gao-avigad-clarke-2012-delta-complete.md)).
- **Misalignment (scope).** The decidability result covers the **full alternating-quantifier**
  bounded hierarchy ($\Sigma_n$, $\Pi_n$). dReal implements only the bottom of it: bounded
  $\exists$ (`QF_NRA`) and one alternation $\exists\forall$ (`forall` /
  [kong-solar-lezama-gao-2018-exists-forall](kong-solar-lezama-gao-2018-exists-forall.md)). Deeper alternations are decidable *in theory*
  here but **not implemented** — they need external transformation (`forall-semantics.md` §8).
- **Citation-hygiene flag (fixed in the docs).** This LICS'12 paper *Delta-Decidability over the
  Reals* and the IJCAR'12 paper *$\delta$-Complete Decision Procedures for Satisfiability over
  the Reals* are **two distinct 2012 Gao–Avigad–Clarke papers**.
  `docs/soundness-vs-completeness.md` previously cited the IJCAR title against the LICS venue;
  corrected when these summaries were added.
