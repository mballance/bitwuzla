(set-logic QF_ABV)
(set-info :status unsat)
(declare-const a0 (Array (_ BitVec 1) (_ BitVec 1) ))
(assert (= #b1 (select (store a0 #b0 #b0) (bvnot ((_ extract 1 1) (bvadd (bvnot #b00) (bvnot #b00)))))))
(check-sat)
