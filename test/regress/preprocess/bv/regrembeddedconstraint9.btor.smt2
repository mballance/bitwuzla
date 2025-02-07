(set-logic QF_BV)
(set-info :status sat)
(declare-const v0 (_ BitVec 4))
(declare-const v1 (_ BitVec 1))
(declare-const v2 (_ BitVec 1))
(assert (= #b1 (bvand ((_ extract 0 0) v0) (bvnot (bvand v1 (bvand ((_ extract 0 0) v0) v2))))))
(check-sat)
