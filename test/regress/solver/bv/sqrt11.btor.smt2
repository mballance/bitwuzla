(set-logic QF_BV)
(set-info :status unsat)
(declare-const v0 (_ BitVec 4))
(assert (= #b1 (ite (= (bvmul v0 v0) (_ bv11 4)) #b1 #b0)))
(check-sat)
