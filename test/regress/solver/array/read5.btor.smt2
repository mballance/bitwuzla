(set-logic QF_ABV)
(set-info :status unsat)
(declare-const a0 (Array (_ BitVec 6) (_ BitVec 8) ))
(declare-const v0 (_ BitVec 3))
(declare-const v1 (_ BitVec 3))
(assert (= #b1 (bvand (ite (= (concat v0 v1) (concat (concat v0 ((_ extract 2 1) v1)) ((_ extract 0 0) v1))) #b1 #b0) (bvnot (ite (= (select a0 (concat v0 v1)) (select a0 (concat (concat v0 ((_ extract 2 1) v1)) ((_ extract 0 0) v1)))) #b1 #b0)))))
(check-sat)
