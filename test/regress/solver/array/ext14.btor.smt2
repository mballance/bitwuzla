(set-logic QF_ABV)
(set-info :status sat)
(declare-const a0 (Array (_ BitVec 3) (_ BitVec 1) ))
(declare-const v0 (_ BitVec 3))
(assert (= #b1 (ite (= (store a0 #b000 (bvnot #b0)) (store (store a0 v0 (bvnot #b0)) (bvnot #b000) (bvnot #b0))) #b1 #b0)))
(check-sat)
