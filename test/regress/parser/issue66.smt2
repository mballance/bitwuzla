(set-option :produce-models true)
(push 1)
(declare-fun a () Bool)
(assert a)
(pop 1)
(set-info :status sat)
(check-sat)
(get-model)
