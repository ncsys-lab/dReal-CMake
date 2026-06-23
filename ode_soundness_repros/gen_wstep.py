# Faithful single-step water flow_0 reproducer, gammas pinned to DRAIN mode (_1=1).
# Tanks drain from x=5 => x decreases, stays <=5 => SAT under gate x_t in [0,5], tau_t~1.
G = "9.8066499999999994"
state = ["A1","A2","A3","q1","q2","q3","tau","x1","x2","x3",
         "gamma_tank2_1","gamma_tank2_2","gamma_tank1_1","gamma_tank1_2","gamma_tank0_1","gamma_tank0_2"]
# RHS verbatim from the benchmark flow_0
rhs = {
 "A1":"(+ (* 0 gamma_tank0_1) (* 0 gamma_tank0_2))",
 "A2":"(+ (* 0 gamma_tank1_1) (* 0 gamma_tank1_2))",
 "A3":"(+ (* 0 gamma_tank2_1) (* 0 gamma_tank2_2))",
 "q1":"(+ (* 0 gamma_tank0_1) (* 0 gamma_tank0_2))",
 "q2":"(+ (* 0 gamma_tank1_1) (* 0 gamma_tank1_2))",
 "q3":"(+ (* 0 gamma_tank2_1) (* 0 gamma_tank2_2))",
 "tau":f"(+ (* (/ 1 3) gamma_tank2_1) (* (/ 1 3) gamma_tank2_2) (* (/ 1 3) gamma_tank1_1) (* (/ 1 3) gamma_tank1_2) (* (/ 1 3) gamma_tank0_1) (* (/ 1 3) gamma_tank0_2))",
 "x1":f"(+ (* 0 gamma_tank1_1) (* 0 gamma_tank1_2) (* (/ (* (* -0.5 (^ (* 2 {G}) 0.5)) (^ x1 0.5)) A1) gamma_tank0_1) (* (/ (- q1 (* (* 0.5 (^ (* 2 {G}) 0.5)) (^ x1 0.5))) A1) gamma_tank0_2))",
 "x2":f"(+ (* 0 gamma_tank2_1) (* 0 gamma_tank2_2) (* (/ (* (* 0.5 (^ (* 2 {G}) 0.5)) (- (^ x1 0.5) (^ x2 0.5))) A2) gamma_tank1_1) (* (/ (+ q2 (* (* 0.5 (^ (* 2 {G}) 0.5)) (- (^ x1 0.5) (^ x2 0.5)))) A2) gamma_tank1_2))",
 "x3":f"(+ (* (/ (* (* 0.5 (^ (* 2 {G}) 0.5)) (- (^ x2 0.5) (^ x3 0.5))) A3) gamma_tank2_1) (* (/ (+ q3 (* (* 0.5 (^ (* 2 {G}) 0.5)) (- (^ x2 0.5) (^ x3 0.5)))) A3) gamma_tank2_2))",
 "gamma_tank2_1":"0","gamma_tank2_2":"0","gamma_tank1_1":"0","gamma_tank1_2":"0","gamma_tank0_1":"0","gamma_tank0_2":"0",
}
# initial values (drain mode pinned: _1 gammas=1, _2=0)
init = {"A1":"2.0","A2":"4.0","A3":"3.0","q1":"5.0","q2":"3.0","q3":"4.0","tau":"0.0",
        "x1":"5.0","x2":"5.0","x3":"5.0",
        "gamma_tank2_1":"1","gamma_tank2_2":"0","gamma_tank1_1":"1","gamma_tank1_2":"0","gamma_tank0_1":"1","gamma_tank0_2":"0"}
L=[]
L.append("(set-logic QF_NRA_ODE)")
for v in state: L.append(f"(declare-fun {v} () Real [-1000.0, 1000.0])")
for v in state:
    L.append(f"(declare-fun {v}_0 () Real [-1000.0, 1000.0])")
    L.append(f"(declare-fun {v}_t () Real [-1000.0, 1000.0])")
L.append("(declare-fun time () Real [0.0, 2.0])")
odes=" ".join(f"(= d/dt[{v}] {rhs[v]})" for v in state)
L.append(f"(define-ode flow_0 ({odes}))")
vec_t="[" + " ".join(f"{v}_t" for v in state) + "]"
vec_0="[" + " ".join(f"{v}_0" for v in state) + "]"
asserts=[f"(= {v}_0 {init[v]})" for v in state]
asserts.append(f"(= {vec_t} (integral 0. time {vec_0} flow_0))")
# gate: clock reaches ~1, tank levels stay <=5 (drain => true)
asserts.append("(>= tau_t 0.9)"); asserts.append("(<= tau_t 1.1)")
for xv in ["x1","x2","x3"]:
    asserts.append(f"(>= {xv}_t 0.0)"); asserts.append(f"(<= {xv}_t 5.0)")
L.append("(assert (and " + " ".join(asserts) + "))")
L.append("(check-sat)"); L.append("(exit)")
open("/tmp/wstep_drain.smt2","w").write("\n".join(L)+"\n")
print("wrote /tmp/wstep_drain.smt2")
