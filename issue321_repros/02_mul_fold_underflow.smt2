; dreal/dreal4#321 -- Layer B (Drake constant-fold, construction time), mul site.
; 2^-1000 * 2^-100 = 2^-1100 underflows to 0.0 when the constant product is folded,
; so (> ... 0) collapses to False before solving -> false `unsat`.
; Both inner powers are normal doubles; only the outer multiply underflows.
; Correct answer: delta-sat.
(set-logic QF_NRA)
(assert (> (* (^ 0.5 1000) (^ 0.5 100)) 0))
(check-sat)
(exit)
