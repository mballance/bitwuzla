(set-logic QF_BV)
(set-info :status sat)
(declare-const v0 (_ BitVec 33))
(declare-const v1 (_ BitVec 65))
(assert (= #b1 (bvand (bvand (ite (= (_ bv18446744073709551617 65) (bvmul ((_ zero_extend 32) v0) v1)) #b1 #b0) (bvnot (bvumulo ((_ zero_extend 32) v0) v1))) (bvnot (ite (= v0 (_ bv1 33)) #b1 #b0)))))
(check-sat)
