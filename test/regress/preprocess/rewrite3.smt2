(declare-const x2 Bool)
(declare-const x Bool)
(declare-fun $1 () (_ BitVec 1))
(declare-fun $51 () (_ BitVec 1))
(declare-fun $15 () (_ BitVec 1))
(declare-fun $ () (_ BitVec 1))
(declare-fun $3 () (_ BitVec 1))
(declare-fun $4 () (_ BitVec 1))
(declare-fun $47 () (_ BitVec 1))
(declare-fun $5 () (_ BitVec 1))
(assert (= (ite (= $15 (_ bv0 1)) (_ bv1 1) (_ bv0 1)) (ite (= (_ bv0 1) (ite x (_ bv1 1) (_ bv0 1))) (_ bv1 1) (_ bv0 1))))
(assert (= (_ bv0 1) (ite (and x (= $ (_ bv1 1))) (_ bv1 1) (_ bv0 1))))
(assert (= (_ bv0 1) (ite (= $1 (_ bv1 1)) (_ bv1 1) (_ bv0 1))))
(assert (= (_ bv0 1) (ite (and (= $51 (_ bv1 1)) (= $3 (_ bv1 1))) (_ bv1 1) (_ bv0 1))))
(assert (= (_ bv0 1) (ite (and x2 (= $3 (_ bv1 1))) (_ bv1 1) (_ bv0 1))))
(assert (= (_ bv0 1) (ite (and x2 (= $51 (_ bv1 1))) (_ bv1 1) (_ bv0 1))))
(assert (= (_ bv0 1) (ite (and x (= $4 (_ bv1 1))) (_ bv1 1) (_ bv0 1))))
(assert (= (_ bv0 1) (ite (= $47 (_ bv1 1)) (_ bv1 1) (_ bv0 1))))
(assert (= (_ bv0 1) (ite (and (= $1 (_ bv1 1)) (= $47 (_ bv1 1))) (_ bv1 1) (_ bv0 1))))
(assert (= (_ bv0 1) (ite (and (= $ (_ bv1 1)) (= $4 (_ bv1 1))) (_ bv1 1) (_ bv0 1))))
(assert (= $5 (ite (and x2 (= (_ bv1 1) (ite (= $51 (ite (= $15 (_ bv0 1)) (_ bv1 1) (_ bv0 1))) (_ bv1 1) (_ bv0 1)))) (_ bv1 1) (_ bv0 1))))
(assert (= $5 (_ bv1 1)))
(set-info :status sat)
(check-sat)
