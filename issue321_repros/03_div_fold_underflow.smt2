; dreal/dreal4#321 -- Layer B (Drake constant-fold, construction time), div site.
; 2^-1000 / 2^100 = 2^-1100 underflows to 0.0 when the constant quotient is folded,
; so (> ... 0) collapses to False before solving -> false `unsat`.
; Correct answer: delta-sat.
(set-logic QF_NRA)
(assert (> (/ (^ 0.5 1000) (^ 2 100)) 0))
(check-sat)
(exit)
