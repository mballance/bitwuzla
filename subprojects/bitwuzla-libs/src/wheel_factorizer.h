#ifndef BZLALS__WHEEL_FACTORIZER_H
#define BZLALS__WHEEL_FACTORIZER_H

#include <cstdint>

#include "bitvector.h"

namespace bzlals {

/* Wheel factorization for s % x = t with base {2, 3, 5}. */
class WheelFactorizer
{
 public:
  /**
   * Constructor.
   * n    : The bit-vector value to factorize.
   * limit: The limit for max number of iterations.
   */
  WheelFactorizer(const ::bzlabv::BitVector& n, uint64_t limit);
  /**
   * Get next factor.
   * Returns nullptr when no next factor exists.
   */
  const ::bzlabv::BitVector* next();

 private:
  /**
   * The value to factorize.
   * Updated at each call to next() to d_num / d_fact if d_fact is a factor
   * of d_num, i.e., if d_num % d_fact = 0. */
  ::bzlabv::BitVector d_num;
  /** The current factor. */
  ::bzlabv::BitVector d_fact;
  /** Bit-vector value one. */
  ::bzlabv::BitVector d_one;
  /** Bit-vector value two. */
  ::bzlabv::BitVector d_two;
  /** Bit-vector value four. */
  ::bzlabv::BitVector d_four;
  /** Bit-vector value six. */
  ::bzlabv::BitVector d_six;

  /** The increments applied to d_fact for a {2, 3, 5} wheel. */
  ::bzlabv::BitVector* d_inc[11];

  bool d_done  = false;
  size_t d_pos = 0;
  uint64_t d_limit;
};
}  // namespace bzlals
#endif
