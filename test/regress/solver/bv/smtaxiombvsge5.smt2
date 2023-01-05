(set-logic QF_BV)
(set-info :status unsat)
(declare-fun s () (_ BitVec 5))
(declare-fun t () (_ BitVec 5))
(assert (not (= (bvsge s t) (bvsle t s))))
(check-sat)
(exit)
