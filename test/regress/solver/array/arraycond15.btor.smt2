(set-logic QF_ABV)
(set-info :status sat)
(declare-const a0 (Array (_ BitVec 1) (_ BitVec 1) ))
(declare-const a1 (Array (_ BitVec 1) (_ BitVec 1) ))
(declare-const v0 (_ BitVec 1))
(assert (= #b1 (select (ite (= v0 #b1) a0 a1) v0)))
(check-sat)
