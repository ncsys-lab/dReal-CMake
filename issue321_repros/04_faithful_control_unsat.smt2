; dreal/dreal4#321 -- CONTROL. A faithful constant fold (2^3 = 8, exact) must stay
; `unsat` on both the broken and fixed solver: 8 < 0 is genuinely false. Guards
; against a fix that over-corrects into spurious `delta-sat`.
(set-logic QF_NRA)
(assert (< (^ 2 3) 0))
(check-sat)
(exit)
