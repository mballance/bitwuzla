(set-logic QF_BV)
(set-info :status unsat)
(declare-const v0 (_ BitVec 32))
(declare-const v1 (_ BitVec 32))
(assert (= #b1 (bvor (bvor (bvnot (ite (= (ite (bvslt v0 v1) #b1 #b0) (ite (bvult (bvadd v0 #b10000000000000000000000000000000) (bvadd v1 #b10000000000000000000000000000000)) #b1 #b0)) #b1 #b0)) (bvnot (ite (= (ite (bvslt v0 v1) #b1 #b0) (bvxor (bvxor (ite (bvult v0 v1) #b1 #b0) ((_ extract 31 31) v0)) ((_ extract 31 31) v1))) #b1 #b0))) (bvor (bvnot (ite (= (ite (bvult v0 v1) #b1 #b0) (ite (bvslt (bvsub v0 #b10000000000000000000000000000000) (bvsub v1 #b10000000000000000000000000000000)) #b1 #b0)) #b1 #b0)) (bvnot (ite (= (ite (bvult v0 v1) #b1 #b0) (bvxor (bvxor (ite (bvslt v0 v1) #b1 #b0) ((_ extract 31 31) v0)) ((_ extract 31 31) v1))) #b1 #b0))))))
(check-sat)
