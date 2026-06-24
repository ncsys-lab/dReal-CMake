; dreal/dreal4#321 -- Layer B (Drake constant-fold, construction time).
; (^ 0.5 1075) = 2^-1075 underflows to 0.0 when folded as a scalar via std::pow,
; so (> ... 0) collapses to False before any solving -> false `unsat`.
; Correct answer: delta-sat (the true value 2^-1075 > 0).
(set-logic QF_NRA)
(assert (> (^ 0.5 1075) 0))
(check-sat)
(exit)
