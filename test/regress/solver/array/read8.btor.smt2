(set-logic QF_ABV)
(set-info :status unsat)
(declare-const a0 (Array (_ BitVec 4) (_ BitVec 7) ))
(declare-const v0 (_ BitVec 4))
(declare-const v1 (_ BitVec 4))
(declare-const v2 (_ BitVec 4))
(declare-const v3 (_ BitVec 2))
(declare-const v4 (_ BitVec 2))
(assert (= #b1 (bvnot (bvor (bvnot (bvand (bvand (ite (= v0 v1) #b1 #b0) (ite (= v2 (concat v3 v4)) #b1 #b0)) (ite (= v1 v2) #b1 #b0))) (bvand (bvand (ite (= (select a0 v0) (select a0 v1)) #b1 #b0) (ite (= (select a0 v2) (select a0 (concat v3 v4))) #b1 #b0)) (ite (= (select a0 v1) (select a0 v2)) #b1 #b0))))))
(check-sat)
