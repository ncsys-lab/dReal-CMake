import sys
G = "9.8066499999999994"
state = ["A1","A2","A3","q1","q2","q3","tau","x1","x2","x3",
         "gamma_tank2_1","gamma_tank2_2","gamma_tank1_1","gamma_tank1_2","gamma_tank0_1","gamma_tank0_2"]
rhs = {
 "A1":"(+ (* 0 gamma_tank0_1) (* 0 gamma_tank0_2))","A2":"(+ (* 0 gamma_tank1_1) (* 0 gamma_tank1_2))",
 "A3":"(+ (* 0 gamma_tank2_1) (* 0 gamma_tank2_2))","q1":"(+ (* 0 gamma_tank0_1) (* 0 gamma_tank0_2))",
 "q2":"(+ (* 0 gamma_tank1_1) (* 0 gamma_tank1_2))","q3":"(+ (* 0 gamma_tank2_1) (* 0 gamma_tank2_2))",
 "tau":"(+ (* (/ 1 3) gamma_tank2_1) (* (/ 1 3) gamma_tank2_2) (* (/ 1 3) gamma_tank1_1) (* (/ 1 3) gamma_tank1_2) (* (/ 1 3) gamma_tank0_1) (* (/ 1 3) gamma_tank0_2))",
 "x1":f"(+ (* 0 gamma_tank1_1) (* 0 gamma_tank1_2) (* (/ (* (* -0.5 (^ (* 2 {G}) 0.5)) (^ x1 0.5)) A1) gamma_tank0_1) (* (/ (- q1 (* (* 0.5 (^ (* 2 {G}) 0.5)) (^ x1 0.5))) A1) gamma_tank0_2))",
 "x2":f"(+ (* 0 gamma_tank2_1) (* 0 gamma_tank2_2) (* (/ (* (* 0.5 (^ (* 2 {G}) 0.5)) (- (^ x1 0.5) (^ x2 0.5))) A2) gamma_tank1_1) (* (/ (+ q2 (* (* 0.5 (^ (* 2 {G}) 0.5)) (- (^ x1 0.5) (^ x2 0.5)))) A2) gamma_tank1_2))",
 "x3":f"(+ (* (/ (* (* 0.5 (^ (* 2 {G}) 0.5)) (- (^ x2 0.5) (^ x3 0.5))) A3) gamma_tank2_1) (* (/ (+ q3 (* (* 0.5 (^ (* 2 {G}) 0.5)) (- (^ x2 0.5) (^ x3 0.5)))) A3) gamma_tank2_2))",
}
for g in ["gamma_tank2_1","gamma_tank2_2","gamma_tank1_1","gamma_tank1_2","gamma_tank0_1","gamma_tank0_2"]: rhs[g]="0"
init = {"A1":"2.0","A2":"4.0","A3":"3.0","q1":"5.0","q2":"3.0","q3":"4.0","tau":"0.0","x1":"5.0","x2":"5.0","x3":"5.0",
        "gamma_tank2_1":"1","gamma_tank2_2":"0","gamma_tank1_1":"1","gamma_tank1_2":"0","gamma_tank0_1":"1","gamma_tank0_2":"0"}
def emit(path, tmax, taulo, tauhi):
    L=["(set-logic QF_NRA_ODE)"]
    for v in state: L.append(f"(declare-fun {v} () Real [-1000.0, 1000.0])")
    for v in state:
        L.append(f"(declare-fun {v}_0 () Real [-1000.0, 1000.0])"); L.append(f"(declare-fun {v}_t () Real [-1000.0, 1000.0])")
    L.append(f"(declare-fun time () Real [0.0, {tmax}])")
    L.append("(define-ode flow_0 ("+" ".join(f"(= d/dt[{v}] {rhs[v]})" for v in state)+"))")
    vt="["+" ".join(f"{v}_t" for v in state)+"]"; v0="["+" ".join(f"{v}_0" for v in state)+"]"
    A=[f"(= {v}_0 {init[v]})" for v in state]
    A.append(f"(= {vt} (integral 0. time {v0} flow_0))")
    A.append(f"(>= tau_t {taulo})"); A.append(f"(<= tau_t {tauhi})")
    for xv in ["x1","x2","x3"]: A.append(f"(>= {xv}_t 0.0)"); A.append(f"(<= {xv}_t 5.0)")
    L.append("(assert (and "+" ".join(A)+"))"); L.append("(check-sat)"); L.append("(exit)")
    open(path,"w").write("\n".join(L)+"\n")
emit("/tmp/ws_interior.smt2","2.0","0.9","1.1")   # terminal tau=1 at t=1 INTERIOR of [0,2]
emit("/tmp/ws_boundary.smt2","1.0","0.999","1.0") # terminal tau=1 at t=1 = win_ub (matches non-sat B)
emit("/tmp/ws_taupin.smt2","1.0","1.0","1.0")     # tau pinned EXACTLY 1 at win_ub
print("emitted")
