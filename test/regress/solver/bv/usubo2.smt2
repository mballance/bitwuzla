(set-logic QF_BV)
(set-info :status unsat) 
(declare-const v (_ BitVec 6))
(assert (and (= (bvsub v v) (_ bv53 6)) (not (bvusubo v v))))
(check-sat)
