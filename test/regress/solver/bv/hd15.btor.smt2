(set-logic QF_BV)
(set-info :status unsat)
(declare-const v0 (_ BitVec 8))
(declare-const v1 (_ BitVec 8))
(assert (= #b1 (bvor (bvnot (ite (= (ite (= v0 v1) #b1 #b0) (bvnot ((_ extract 7 7) (bvor (bvsub v0 v1) (bvsub v1 v0))))) #b1 #b0)) (bvnot (ite (= (bvnot (ite (= v0 v1) #b1 #b0)) ((_ extract 7 7) (bvor (bvsub v0 v1) (bvsub v1 v0)))) #b1 #b0)))))
(check-sat)
