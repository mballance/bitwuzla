(set-logic QF_BV)
(set-info :status unsat)
(declare-const v0 (_ BitVec 32))
(declare-const v1 (_ BitVec 2))
(assert (= #b1 (bvnot (ite (= ((_ extract 31 3) (bvurem v0 (concat (concat (_ bv0 29) v1) (_ bv1 1)))) (_ bv0 29)) #b1 #b0))))
(check-sat)
