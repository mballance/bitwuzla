(set-logic QF_BV)
(set-info :status unsat)
(declare-const v0 (_ BitVec 3))
(assert (= #b1 (bvand (bvnot (ite (= (concat (bvnot ((_ extract 0 0) v0)) (bvnot ((_ extract 0 0) v0))) (_ bv0 2)) #b1 #b0)) ((_ extract 0 0) (bvmul (bvadd (bvnot ((_ extract 1 0) v0)) (bvnot (_ bv0 2))) (bvadd (bvnot ((_ extract 1 0) v0)) (bvnot (_ bv0 2))))))))
(check-sat)
