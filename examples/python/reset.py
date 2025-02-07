###
# Bitwuzla: Satisfiability Modulo Theories (SMT) solver.
#
# Copyright (C) 2023 by the authors listed in the AUTHORS file at
# https:#github.com/bitwuzla/bitwuzla/blob/main/AUTHORS
#
# This file is part of Bitwuzla under the MIT license. See COPYING for more
# information at https:#github.com/bitwuzla/bitwuzla/blob/main/COPYING
##

from bitwuzla import *

if __name__ == '__main__':

    # First, create a term manager instance.
    tm = TermManager()
    # Create a Bitwuzla options instance.
    options = Options()
    # (set-option :produce-models true)
    options.set(Option.PRODUCE_MODELS, False)

    # Then, create a Bitwuzla instance.
    bitwuzla = Bitwuzla(tm, options)

    # Create a bit-vector sort of size 3.
    sortbv3 = tm.mk_bv_sort(3)

    # (declare-const x (_ BitVec 3))
    x = tm.mk_const(sortbv3, 'x')

    # (assert (= x #b010))
    bitwuzla.assert_formula(
        tm.mk_term(Kind.EQUAL, [x, tm.mk_bv_value(sortbv3, 2)]))
    # (check-sat)
    result = bitwuzla.check_sat()
    print('Expect: sat')
    print(f'Bitwuzla: {result}')

    # (set-option :produce-models true)
    options.set(Option.PRODUCE_MODELS, True)

    # (reset)
    # Note: Bitwuzla does not provide an explicit API function for reset since
    #       this is achieved by simply discarding the current Bitwuzla instance
    #       and creating a new one.
    bitwuzla = Bitwuzla(tm, options)

    # (declare-const a ( Array (_ BitVec 2) (_ BitVec 3)))
    sortbv2 = tm.mk_bv_sort(2)
    a       = tm.mk_const(tm.mk_array_sort(sortbv2, sortbv3), 'a')

    # (assert (= x #b011))
    bitwuzla.assert_formula(
        tm.mk_term(Kind.EQUAL, [x, tm.mk_bv_value(sortbv3, 3)]))
    # (assert (= x (select a #b01)))
    bitwuzla.assert_formula(tm.mk_term(
        Kind.EQUAL,
        [x, tm.mk_term(Kind.ARRAY_SELECT, [a, tm.mk_bv_value(sortbv2, 1)])]))
    # (check-sat)
    result = bitwuzla.check_sat()
    print('Expect: sat')
    print(f'Bitwuzla: {result}')
    # (get-model)
    print('(')
    print(f'  (define-fun {x.symbol()} () {x.sort()} {bitwuzla.get_value(x)} )')
    print(f'  (define-fun {a.symbol()} () {a.sort()} {bitwuzla.get_value(a)} )')
    print(')')
