(set-logic QF_ABV)
(set-info :status unsat)
(declare-const v0 (_ BitVec 1))
(declare-const a0 (Array (_ BitVec 32) (_ BitVec 8) ))
(declare-const a1 (Array (_ BitVec 32) (_ BitVec 8) ))
(assert (= #b1 (bvor (bvnot (ite (= (ite (= (ite (= v0 #b1) a0 a1) a1) #b1 #b0) (bvor (bvnot v0) (ite (= a0 a1) #b1 #b0))) #b1 #b0)) (bvnot (ite (= (bvor (bvnot v0) (ite (= a0 a1) #b1 #b0)) (ite (= a1 (ite (= v0 #b1) a0 a1)) #b1 #b0)) #b1 #b0)))))
(check-sat)
