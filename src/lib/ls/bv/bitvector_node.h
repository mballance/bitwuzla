/***
 * Bitwuzla: Satisfiability Modulo Theories (SMT) solver.
 *
 * Copyright (C) 2021 by the authors listed in the AUTHORS file at
 * https://github.com/bitwuzla/bitwuzla/blob/main/AUTHORS
 *
 * This file is part of Bitwuzla under the MIT license. See COPYING for more
 * information at https://github.com/bitwuzla/bitwuzla/blob/main/COPYING
 */

#ifndef BZLA__LS_BITVECTOR_NODE_H
#define BZLA__LS_BITVECTOR_NODE_H

#include <vector>

#include "bv/bitvector.h"
#include "bv/bounds/bitvector_bounds.h"
#include "bv/domain/bitvector_domain.h"
#include "ls/ls.h"
#include "ls/node/node.h"

namespace bzla {

class RNG;

namespace ls {

/* -------------------------------------------------------------------------- */

class BitVectorExtract;

/* -------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------- */

class BitVectorNode : public Node<BitVector>
{
 public:
  /**
   * Constructor.
   * @param rng  The associated random number generator.
   * @param size The bit-vector size.
   */
  BitVectorNode(RNG* rng, uint64_t size);
  /**
   * Constructor.
   * @param rng        The associated random number generator.
   * @param assignment The current bit-vector assignment.
   * @param size       The bit-vector size.
   */
  BitVectorNode(RNG* rng,
                const BitVector& assignment,
                const BitVectorDomain& domain);
  /** Destructor. */
  virtual ~BitVectorNode() {}

  bool is_inequality() const override;
  bool is_not() const override;
  bool is_value_false() const override;
  void set_assignment(const BitVector& assignment) override;
  std::string str() const override;
  std::vector<std::string> log() const override;

  /**
   * Get child at given index.
   * @param pos The index of the child to get.
   * @return The child at the given index.
   */
  BitVectorNode* child(uint64_t pos) const;

  /**
   * Get the domain of this node.
   * @return A reference to the domain of this node.
   */
  const BitVectorDomain& domain() const { return d_domain; }

  /**
   * Get the bit-vector size of the node.
   * @return The size of this node.
   */
  uint64_t size() const { return d_assignment.size(); }

  /**
   * Register parent extract for normalization.
   * @param node The parent extract node to register.
   */
  void register_extract(BitVectorNode* node);
  /**
   * Get the set of parent extracts for normalization.
   * @return The set of registered extracts.
   */
  const std::vector<BitVectorExtract*>& get_extracts() { return d_extracts; }

  /**
   * Tighten signed and/or unsigned bounds of this node wrt. to the given
   * signed and unsigned bounds. If the given signed and unsigned ranges don't
   * have any intersection with the bounds of this node, all return parameters
   * will be null nodes
   * @param bounds_u The unsigned bounds.
   * @param bounds_s The lower signed bound.
   * @return A tuple [bounds_u, bounds_s] of resulting unsigned and signed
   *         bounds.
   */
  std::tuple<BitVectorRange, BitVectorRange> tighten_bounds(
      const BitVectorRange& bounds_u, const BitVectorRange& bounds_s);

  /**
   * Get the unsigned bounds for inverse value computation.
   * @return The unsigned bounds.
   */
  const BitVectorRange& bounds_u() { return d_bounds_u; }
  /**
   * Get the signed bounds for inverse value computation.
   * @return The signed bounds.
   */
  const BitVectorRange& bounds_s() { return d_bounds_s; }

  /**
   * Fix domain bit at index `idx` to `value`.
   * @param idx The index of the bit to fix to the given value.
   * @param value  The value to fix the bit to.
   */
  void fix_bit(uint64_t idx, bool value);

  /**
   * Update bounds.
   * @param min The minimum value.
   * @param max The maximum value.
   * @param min_is_exclusive True if the given min bound is exclusive, in which
   * case `min + 1` will be used.
   * @param max_is_exclusive True if the given max bound is exclusive, in which
   * case `min - 1` will be used.
   * @param is_signed True if the given bounds are signed bounds.
   */
  void update_bounds(const BitVector& min,
                     const BitVector& max,
                     bool min_is_exclusive,
                     bool max_is_exclusive,
                     bool is_signed);
  /**
   * Reset signed and unsigned bounds.
   */
  void reset_bounds();

 protected:
  /**
   * Tighten signed and/or unsigned bounds of this node wrt. to the given
   * signed and unsigned bounds. If the given signed and unsigned ranges don't
   * have any intersection with the bounds of this node, all return parameters
   * will be null nodes
   * @param cur_bounds_u The current unsigned bound (to be tightened).
   * @param cur_bounds_s The current signed bound (to be tightened).
   * @param bounds_u     The unsigned bound to tighten the current
   *                     lower unsigned bound with.
   * @param bounds_s     The lower signed bound to tighten the current lower
   *                     signed bound with.
   *
   * @return A tuple [bounds_u, bounds_s] of resulting unsigned and signed
   *         bounds.
   */
  static std::tuple<BitVectorRange, BitVectorRange> tighten_bounds(
      const BitVectorRange& cur_bounds_u,
      const BitVectorRange& cur_bounds_s,
      const BitVectorRange& bounds_u,
      const BitVectorRange& bounds_s);

  BitVectorNode(RNG* rng, uint64_t size, BitVectorNode* child0);
  BitVectorNode(RNG* rng,
                uint64_t size,
                BitVectorNode* child0,
                BitVectorNode* child1);
  BitVectorNode(RNG* rng,
                uint64_t size,
                BitVectorNode* child0,
                BitVectorNode* child1,
                BitVectorNode* child2);
  BitVectorNode(RNG* rng, const BitVectorDomain& domain, BitVectorNode* child0);
  BitVectorNode(RNG* rng,
                const BitVectorDomain& domain,
                BitVectorNode* child0,
                BitVectorNode* child1);
  BitVectorNode(RNG* rng,
                const BitVectorDomain& domain,
                BitVectorNode* child0,
                BitVectorNode* child1,
                BitVectorNode* child2);
  /**
   * Normalize given signed and unsigned bounds into a lower (from min_signed
   * to ones) and upper (from zero to max_signed) ranges. If the given signed
   * and unsigned ranges don't have any intersection, all return parameters
   * will be null nodes.
   *
   * @param bounds_u The unsigned bounds range.
   * @param bounds_s The signed bounds range.
   *
   * @return The resulting lower and upper range bounds, null BitVectors for
   *         both min/max in the lo and/or hi range if no values in that range
   *         are covered.
   */
  virtual BitVectorBounds normalize_bounds(const BitVectorRange& bounds_u,
                                           const BitVectorRange& bounds_s);
  /**
   * Helper to compute the normalized min and max bounds for `x` with respect
   * to the current signed and unsigned min/max bounds of `x` and a target
   * value. The resulting bounds are split into a lower (from min_signed to
   * ones) and upper (from zero to max_signed) ranges. If the resulting ranges
   * are empty (no inverse value exists with respect to `t`, the assignment
   * of the other operands and the current bounds on `x`), the returned bounds
   * will be empty.
   *
   * @param t The target value of this node.
   * @param pos_x The index of operand `x`.
   * @return The resulting lower and upper range bounds, empty if no values in
   *         that range are covered.
   */
  BitVectorBounds compute_normalized_bounds(const BitVector& t, uint64_t pos_x);

  /**
   * Helper to compute the signed or unsigned min and max bounds for `x` with
   * respect to the semantics of the operator, the current signed or unsigned
   * min/max bounds of `x`, and the current target value. If the resulting
   * ranges are empty, the returned ranges will be empty.
   *
   * @note Computes signed bounds for signed operators, and unsigned bounds
   *       for unsigned operators. Thus, either of the resulting bounds will
   *       be empty, depending on the signedness of the operator.
   *
   * @param t     The target value.
   * @param pos_x The index of operand `x`.
   * @return A tuple [bounds_u, bounds_s] of resulting unsigned and signed
   *         bounds.
   */
  virtual std::tuple<BitVectorRange, BitVectorRange> compute_min_max_bounds(
      const BitVector& t, uint64_t pos_x);

  /** The underlying bit-vector domain representing constant bits. */
  BitVectorDomain d_domain;

  /** Unsigned bounds for inverse value computation. */
  BitVectorRange d_bounds_u;
  /** Signed bounds for inverse value computation. */
  BitVectorRange d_bounds_s;

  std::vector<BitVectorExtract*> d_extracts;
};

std::ostream& operator<<(std::ostream& out, const BitVectorNode& node);

/* -------------------------------------------------------------------------- */

class BitVectorAdd : public BitVectorNode
{
 public:
  /** Constructors. */
  BitVectorAdd(RNG* rng,
               uint64_t size,
               BitVectorNode* child0,
               BitVectorNode* child1);
  BitVectorAdd(RNG* rng,
               const BitVectorDomain& domain,
               BitVectorNode* child0,
               BitVectorNode* child1);

  NodeKind kind() const override { return NodeKind::BV_ADD; }

  void evaluate() override;

  /**
   * IC:
   *   w/o  const bits: true
   *   with const bits: mfb(x, t - s)
   */
  bool is_invertible(const BitVector& t,
                     uint64_t pos_x,
                     bool is_essential_check = false) override;

  /**
   * CC:
   *   w/o  const bits: true
   *   with const bits: true
   */
  bool is_consistent(const BitVector& t, uint64_t pos_x) override;

  const BitVector& inverse_value(const BitVector& t, uint64_t pos_x) override;

  const BitVector& consistent_value(const BitVector& t,
                                    uint64_t pos_x) override;

 private:
  /**
   * Evaluate the assignment of this node.
   *
   * Helper to allow evaluating the assignment on construction (evaluate() is
   * virtual and cannot be called from the constructor).
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate();
  /**
   * Wrapper for evaluating the assignment and fixing the domain on construction
   * when all operands are constant.
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate_and_set_domain();
};

/* -------------------------------------------------------------------------- */

class BitVectorAnd : public BitVectorNode
{
 public:
  /** Constructors. */
  BitVectorAnd(RNG* rng,
               uint64_t size,
               BitVectorNode* child0,
               BitVectorNode* child1);
  BitVectorAnd(RNG* rng,
               const BitVectorDomain& domain,
               BitVectorNode* child0,
               BitVectorNode* child1);

  NodeKind kind() const override { return NodeKind::BV_AND; }

  void evaluate() override;

  std::tuple<BitVectorRange, BitVectorRange> compute_min_max_bounds(
      const BitVector& t, uint64_t pos_x) override;

  /**
   * IC:
   *   w/o const bits (IC_wo): (t & s) = t
   *   with const bits       : IC_wo && ((s & x_hi) & m) = (t & m)
   *                         with m = ~(x_lo ^ x_hi)
   *                              ... mask out all non-const bits
   * Intuition:
   * 1) x & s = t on all const bits of x
   * 2) s & t = t on all non-const bits of x
   */
  bool is_invertible(const BitVector& t,
                     uint64_t pos_x,
                     bool is_essential_check = false) override;

  /**
   * CC:
   *   w/o  const bits: true
   *   with const bits: t & x_hi = t
   */
  bool is_consistent(const BitVector& t, uint64_t pos_x) override;

  const BitVector& inverse_value(const BitVector& t, uint64_t pos_x) override;

  const BitVector& consistent_value(const BitVector& t,
                                    uint64_t pos_x) override;

 private:
  /**
   * Evaluate the assignment of this node.
   *
   * Helper to allow evaluating the assignment on construction (evaluate() is
   * virtual and cannot be called from the constructor).
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate();
  /**
   * Wrapper for evaluating the assignment and fixing the domain on construction
   * when all operands are constant.
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate_and_set_domain();
  /** Cache for current bound wrt. s and t and fixed bits in x. */
  BitVectorRange d_bounds;
};

/* -------------------------------------------------------------------------- */

class BitVectorConcat : public BitVectorNode
{
 public:
  /** Constructors. */
  BitVectorConcat(RNG* rng,
                  uint64_t size,
                  BitVectorNode* child0,
                  BitVectorNode* child1);
  BitVectorConcat(RNG* rng,
                  const BitVectorDomain& domain,
                  BitVectorNode* child0,
                  BitVectorNode* child1);

  NodeKind kind() const override { return NodeKind::BV_CONCAT; }

  void evaluate() override;

  /**
   * x o s = tx o ts
   * s o x = ts o tx
   *
   * IC:
   *   w/o  const bits: s = ts
   *     pos_x = 0: ts = t[bw(s) - 1 : 0]
   *     pos_x = 1: ts = t[bw(t) - 1 : bw(t) - bw(s)]
   *
   * with const bits: mfb(x, tx) && s = ts
   */
  bool is_invertible(const BitVector& t,
                     uint64_t pos_x,
                     bool is_essential_check = false) override;

  /**
   * CC:
   *   w/o  const bits: true
   *
   *   with const bits: mfb(x, tx)
   *     pos_x = 0: tx = t[bw(t) - 1 : bw(t) - bw(x)]
   *     pos_x = 1: tx = t[bw(x) - 1 : 0]
   */
  bool is_consistent(const BitVector& t, uint64_t pos_x) override;

  const BitVector& inverse_value(const BitVector& t, uint64_t pos_x) override;

  const BitVector& consistent_value(const BitVector& t,
                                    uint64_t pos_x) override;

 private:
  /**
   * Evaluate the assignment of this node.
   *
   * Helper to allow evaluating the assignment on construction (evaluate() is
   * virtual and cannot be called from the constructor).
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate();
  /**
   * Wrapper for evaluating the assignment and fixing the domain on construction
   * when all operands are constant.
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate_and_set_domain();
};

/* -------------------------------------------------------------------------- */

class BitVectorEq : public BitVectorNode
{
 public:
  /** Constructors. */
  BitVectorEq(RNG* rng,
              uint64_t size,
              BitVectorNode* child0,
              BitVectorNode* child1);
  BitVectorEq(RNG* rng,
              const BitVectorDomain& domain,
              BitVectorNode* child0,
              BitVectorNode* child1);

  NodeKind kind() const override { return NodeKind::EQ; }

  void evaluate() override;

  /**
   * IC:
   *   w/o  const bits: true
   *   with const bits:
   *    t = 0: (x_hi != x_lo) || (x_hi != s)
   *    t = 1: mfb(x, s)
   */
  bool is_invertible(const BitVector& t,
                     uint64_t pos_x,
                     bool is_essential_check = false) override;

  /**
   * CC:
   *   w/o  const bits: true
   *   with const bits: true
   */
  bool is_consistent(const BitVector& t, uint64_t pos_x) override;

  const BitVector& inverse_value(const BitVector& t, uint64_t pos_x) override;

  const BitVector& consistent_value(const BitVector& t,
                                    uint64_t pos_x) override;

 private:
  /**
   * Evaluate the assignment of this node.
   *
   * Helper to allow evaluating the assignment on construction (evaluate() is
   * virtual and cannot be called from the constructor).
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate();
  /**
   * Wrapper for evaluating the assignment and fixing the domain on construction
   * when all operands are constant.
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate_and_set_domain();
};

/* -------------------------------------------------------------------------- */

class BitVectorMul : public BitVectorNode
{
 public:
  /** Constructors. */
  BitVectorMul(RNG* rng,
               uint64_t size,
               BitVectorNode* child0,
               BitVectorNode* child1);
  BitVectorMul(RNG* rng,
               const BitVectorDomain& domain,
               BitVectorNode* child0,
               BitVectorNode* child1);

  NodeKind kind() const override { return NodeKind::BV_MUL; }

  void evaluate() override;

  /**
   * IC:
   *   w/o const bits (IC_wo): ((-s | s) & t) = t
   *   with const bits       : IC_wo &&
   *                           (s = 0 ||
   *                            ((odd(s) => mfb(x, t * s^-1)) &&
   *                             (!odd(s) => mfb (x << c, y << c))))
   *                    with c = ctz(s) and y = (t >> c) * (s >> c)^-1
   */
  bool is_invertible(const BitVector& t,
                     uint64_t pos_x,
                     bool is_essential_check = false) override;

  /**
   * CC:
   *   w/o  const bits: true
   *   with const bits: (t != 0 => x_hi != 0) &&
   *                    (odd(t) => x_hi[lsb] != 0) &&
   *                    (!odd(t) => \exists y. (mfb(x, y) && ctz(t) >= ctz(y))
   */
  bool is_consistent(const BitVector& t, uint64_t pos_x) override;

  const BitVector& inverse_value(const BitVector& t, uint64_t pos_x) override;

  const BitVector& consistent_value(const BitVector& t,
                                    uint64_t pos_x) override;

  std::tuple<BitVectorRange, BitVectorRange> compute_min_max_bounds(
      const BitVector& t, uint64_t pos_x) override;

 private:
  /**
   * Evaluate the assignment of this node.
   *
   * Helper to allow evaluating the assignment on construction (evaluate() is
   * virtual and cannot be called from the constructor).
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate();
  /**
   * Wrapper for evaluating the assignment and fixing the domain on construction
   * when all operands are constant.
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate_and_set_domain();
};

/* -------------------------------------------------------------------------- */

class BitVectorShl : public BitVectorNode
{
 public:
  /** Constructors. */
  BitVectorShl(RNG* rng,
               uint64_t size,
               BitVectorNode* child0,
               BitVectorNode* child1);
  BitVectorShl(RNG* rng,
               const BitVectorDomain& domain,
               BitVectorNode* child0,
               BitVectorNode* child1);

  NodeKind kind() const override { return NodeKind::BV_SHL; }

  void evaluate() override;

  /**
   * IC:
   *   w/o const bits (IC_wo):
   *       pos_x = 0: (t >> s) << s = t
   *       pos_x = 1: ctz(s) <= ctz(t) &&
   *                  ((t = 0) || (s << (ctz(t) - ctz(s))) = t)
   *
   *   with const bits:
   *       pos_x = 0: IC_wo && mfb(x << s, t)
   *       pos_x = 1: IC_wo &&
   *                  ((t = 0) => (x_hi >= ctz(t) - ctz(s) || (s = 0))) &&
   *                  ((t != 0) => mfb(x, ctz(t) - ctz(s)))
   */
  bool is_invertible(const BitVector& t,
                     uint64_t pos_x,
                     bool is_essential_check = false) override;

  /**
   * CC:
   *   w/o  const bits: true
   *   with const bits:
   *     pos_x = 0: \exists y. (y <= ctz(t) && mfb(x << y, t))
   *     pos_x = 1: t = 0 || \exists y. (y <= ctz(t) && mfb(x, y))
   */
  bool is_consistent(const BitVector& t, uint64_t pos_x) override;

  const BitVector& inverse_value(const BitVector& t, uint64_t pos_x) override;

  const BitVector& consistent_value(const BitVector& t,
                                    uint64_t pos_x) override;

 private:
  /**
   * Evaluate the assignment of this node.
   *
   * Helper to allow evaluating the assignment on construction (evaluate() is
   * virtual and cannot be called from the constructor).
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate();
  /**
   * Wrapper for evaluating the assignment and fixing the domain on construction
   * when all operands are constant.
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate_and_set_domain();
};

/* -------------------------------------------------------------------------- */

class BitVectorShr : public BitVectorNode
{
 public:
  /**
   * Additional interface / helper for is_invertible.
   * Cached result is stored in 'inverse_value'.
   */
  static bool is_invertible(RNG* rng,
                            const BitVector& t,
                            const BitVector& s,
                            const BitVectorDomain& x,
                            uint64_t pos_x,
                            std::unique_ptr<BitVector>* inverse_value);
  /**
   * Additional interface / helper for inverse_value.
   * Cached result is stored in `inverse`.
   */
  static void inverse_value(RNG* rng,
                            const BitVector& t,
                            const BitVector& s,
                            const BitVectorDomain& x,
                            uint64_t pos_x,
                            std::unique_ptr<BitVector>& inverse);
  /** Constructors. */
  BitVectorShr(RNG* rng,
               uint64_t size,
               BitVectorNode* child0,
               BitVectorNode* child1);
  BitVectorShr(RNG* rng,
               const BitVectorDomain& domain,
               BitVectorNode* child0,
               BitVectorNode* child1);

  NodeKind kind() const override { return NodeKind::BV_SHR; }

  void evaluate() override;

  /**
   * IC:
   *   w/o const bits (IC_wo):
   *       pos_x = 0: (t << s) >> s = t
   *       pos_x = 1: clz(s) <= clz(t) &&
   *                  ((t = 0) || (s >> (clz(t) - clz(s))) = t)
   *
   *   with const bits:
   *       pos_x = 0: IC_wo && mfb(x >> s, t)
   *       pos_x = 1: IC_wo &&
   *                  ((t = 0) => (x_hi >= clz(t) - clz(s) || (s = 0))) &&
   *                  ((t != 0) => mfb(x, clz(t) - clz(s)))
   */
  bool is_invertible(const BitVector& t,
                     uint64_t pos_x,
                     bool is_essential_check = false) override;

  /**
   * CC:
   *   w/o  const bits: true
   *   with const bits:
   *     pos_x = 0: \exists y. (y <= clz(t) && mfb(x >> y, t))
   *     pos_x = 1: t = 0 || \exists y. (y <= clz(t) && mfb(x, y))
   */
  bool is_consistent(const BitVector& t, uint64_t pos_x) override;

  const BitVector& inverse_value(const BitVector& t, uint64_t pos_x) override;

  const BitVector& consistent_value(const BitVector& t,
                                    uint64_t pos_x) override;

 private:
  /**
   * Evaluate the assignment of this node.
   *
   * Helper to allow evaluating the assignment on construction (evaluate() is
   * virtual and cannot be called from the constructor).
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate();
  /**
   * Wrapper for evaluating the assignment and fixing the domain on construction
   * when all operands are constant.
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate_and_set_domain();
};

/* -------------------------------------------------------------------------- */

class BitVectorAshr : public BitVectorNode
{
 public:
  /** Constructors. */
  BitVectorAshr(RNG* rng,
                uint64_t size,
                BitVectorNode* child0,
                BitVectorNode* child1);
  BitVectorAshr(RNG* rng,
                const BitVectorDomain& domain,
                BitVectorNode* child0,
                BitVectorNode* child1);

  NodeKind kind() const override { return NodeKind::BV_ASHR; }

  void evaluate() override;

  /**
   * IC:
   *   w/o const bits (IC_wo):
   *       pos_x = 0: (s < bw(s) => (t << s) >>a s = t) &&
   *                  (s >= bw(s) => (t = ones || t = 0))
   *       pos_x = 1: (s[msb] = 0 => IC_shr(s >> x = t) &&
   *                  (s[msb] = 1 => IC_shr(~s >> x = ~t))
   *
   *   with const bits:
   *       pos_x = 0: IC_wo && mfb(x >>a s, t)
   *       pos_x = 1: IC_wo &&
   *                  (s[msb ] = 0 => IC_shr) &&
   *                  (s[msb] = 1 => IC_shr(~s >> x = ~t))
   */
  bool is_invertible(const BitVector& t,
                     uint64_t pos_x,
                     bool is_essential_check = false) override;

  /**
   * CC:
   *   w/o  const bits: true
   *   with const bits:
   *     pos_x = 0:
   *     ((t = 0 || t = ones) => \exists y. (y[msb] = t[msb] && mfb(x, y))) &&
   *     ((t != 0 && t != ones) => \exists y. (
   *        c => y <= clo(t) && ~c => y <= clz(t) && mfb(x, y))
   *     with c = ((t << y)[msb] = 1)
   *
   *     pos_x = 1:
   *     t = 0 || t = ones ||
   *     \exists y. (c => y < clo(t) && ~c => y < clz(t) && mfb(x, y)
   *     with c = (t[msb] = 1)
   */
  bool is_consistent(const BitVector& t, uint64_t pos_x) override;

  const BitVector& inverse_value(const BitVector& t, uint64_t pos_x) override;

  const BitVector& consistent_value(const BitVector& t,
                                    uint64_t pos_x) override;

 private:
  /**
   * Evaluate the assignment of this node.
   *
   * Helper to allow evaluating the assignment on construction (evaluate() is
   * virtual and cannot be called from the constructor).
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate();
  /**
   * Wrapper for evaluating the assignment and fixing the domain on construction
   * when all operands are constant.
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate_and_set_domain();
};

/* -------------------------------------------------------------------------- */

class BitVectorUdiv : public BitVectorNode
{
 public:
  /** Constructors. */
  BitVectorUdiv(RNG* rng,
                uint64_t size,
                BitVectorNode* child0,
                BitVectorNode* child1);
  BitVectorUdiv(RNG* rng,
                const BitVectorDomain& domain,
                BitVectorNode* child0,
                BitVectorNode* child1);

  NodeKind kind() const override { return NodeKind::BV_UDIV; }

  void evaluate() override;

  /**
   * IC:
   *   w/o const bits (IC_wo):
   *       pos_x = 0: (s * t) / s = t
   *       pos_x = 1: s / (s / t) = t
   *
   *   with const bits:
   *       pos_x = 0: IC_wo &&
   *                  (t = 0 => x_lo < s) &&
   *                  ((t != 0 && s != 0 ) => \exists y. (
   *                    mfb(x, y) && (~c => y < s * t + 1) && (c => y <= ones)))
   *                  with c = umulo(s, t + 1) && uaddo(t, 1)
   *       pos_x = 1: IC_wo &&
   *                  (t != ones => x_hi > 0) &&
   *                  ((s != 0 || t != 0) => (s / x_hi <= t) && \exists y. (
   *                      mfb(x, y) &&
   *                      (t = ones => y <= s / t) &&
   *                      (t != ones => y > t + 1 && y <= s / t)))
   */
  bool is_invertible(const BitVector& t,
                     uint64_t pos_x,
                     bool is_essential_check = false) override;

  /**
   * CC:
   *   w/o  const bits: true
   *
   *   with const bits:
   *     pos_x = 0:
   *       (t != ones => x_hi >= t) && (t = 0 => x_lo != ones) &&
   *       ((t != 0 && t != ones && t != 1 && !mfb(x, t)) =>
   *        (!mulo(2, t) && \exists y,o.(mfb(x, y*t + o) && y >= 1 && o <= c
   *         && !mulo(y, t) && !addo(y * t, o))))
   *     with c = min(y − 1, x_hi − y * t)
   *
   *     pos_x = 1:
   *       (t = ones => (mfb(x, 0) || mfb(x, 1))) &&
   *       (t != ones => (!mulo(x_lo, t) &&
   *                  \exists y. (y > 0 && mfb(x, y) && !mulo(y, t))))
   */
  bool is_consistent(const BitVector& t, uint64_t pos_x) override;

  const BitVector& inverse_value(const BitVector& t, uint64_t pos_x) override;

  const BitVector& consistent_value(const BitVector& t,
                                    uint64_t pos_x) override;

 private:
  /**
   * Evaluate the assignment of this node.
   *
   * Helper to allow evaluating the assignment on construction (evaluate() is
   * virtual and cannot be called from the constructor).
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate();
  /**
   * Wrapper for evaluating the assignment and fixing the domain on construction
   * when all operands are constant.
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate_and_set_domain();

  /**
   * Try to find a consistent value for pos_x = 0 other than x = t.
   * Returns a null bit-vector if no such value can be found.
   */
  BitVector consistent_value_pos0_aux(const BitVector& t);
};

/* -------------------------------------------------------------------------- */

class BitVectorUlt : public BitVectorNode
{
 public:
  /**
   * Constructor.
   * @param rng The associated random number generator.
   * @param size The bit-width of this node.
   * @param child0 The operand at index 0.
   * @param child1 The operand at index 1.
   * @param opt_concat_sext True to enable optimization for inverse_value
   *                        computation of concat and sign extension operands.
   */
  BitVectorUlt(RNG* rng,
               uint64_t size,
               BitVectorNode* child0,
               BitVectorNode* child1,
               bool opt_concat_sext = false);
  /**
   * Constructor.
   * @param rng The associated random number generator.
   * @param domain The underlying bit-vector domain.
   * @param child0 The operand at index 0.
   * @param child1 The operand at index 1.
   * @param opt_concat_sext True to enable optimization for inverse_value
   *                        computation of concat and sign extension operands.
   */
  BitVectorUlt(RNG* rng,
               const BitVectorDomain& domain,
               BitVectorNode* child0,
               BitVectorNode* child1,
               bool opt_concat_sext = false);

  NodeKind kind() const override { return NodeKind::BV_ULT; }

  void evaluate() override;

  /**
   * IC:
   *   w/o const bits (IC_wo):
   *       pos_x = 0: t = 0 || s != 0
   *       pos_x = 1: t = 0 || s != ones
   *
   *   with const bits:
   *       pos_x = 0: t = 1 => (s != 0 && x_lo < s) && t = 0 => (x_hi >= s)
   *       pos_x = 1: t = 1 => (s != ones && x_hi > s) && t = 0 => (x_lo <= s)
   */
  bool is_invertible(const BitVector& t,
                     uint64_t pos_x,
                     bool is_essential_check = false) override;

  /**
   * CC:
   *   w/o  const bits: true
   *   with const bits: pos_x = 0: t = false || x_lo != ones
   *                    pos_x = 1: t = false || x_hi != 0
   */
  bool is_consistent(const BitVector& t, uint64_t pos_x) override;

  const BitVector& inverse_value(const BitVector& t, uint64_t pos_x) override;

  const BitVector& consistent_value(const BitVector& t,
                                    uint64_t pos_x) override;

  std::tuple<BitVectorRange, BitVectorRange> compute_min_max_bounds(
      const BitVector& t, uint64_t pos_x) override;

 private:
  /**
   * Evaluate the assignment of this node.
   *
   * Helper to allow evaluating the assignment on construction (evaluate() is
   * virtual and cannot be called from the constructor).
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate();
  /**
   * Wrapper for evaluating the assignment and fixing the domain on construction
   * when all operands are constant.
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate_and_set_domain();
  /**
   * Helper for is_invertible().
   *
   * @note Does not and (must not) reset cached inverse.
   *
   * @param d The domain representing operand 'x'.
   * @param t The target value of this node.
   * @param pos_x The index of operand `x`.
   * @param is_essential_check True if called to determine is_essential(). For
   *                           is_essential() checks, we don't consider bounds
   *                           derived from top-level inequalities since this
   *                           may trap us in a cycle (see is_essential()).
   * @param opt_concat         True to enable optimization for inverse_value
   *                           computation of concat operand.
   */
  bool _is_invertible(const BitVectorDomain* d,
                      const BitVector& t,
                      uint64_t pos_x,
                      bool is_essential_check,
                      bool opt_concat);

  /**
   * Helper for concat-specific (when x is a concat) inverse value computation.
   * Attempts to find an inverse value by only changing the value of one of
   * the children of the concat.
   * @param t The target value of this node.
   * @param pos_x The index of operand `x`, which is a concat node.
   * @return The inverse value.
   */
  BitVector inverse_value_concat(bool t, uint64_t pos_x);
  /**
   * Helper for inverse_value_concat()  to generate a new random value with
   * respect to the given domain and within given min/max range.
   * @param d The domain.
   * @param min The lower bound of the range.
   * @param max The upper bound of the range.
   * @return A random value within the given range, if there is one, else
   *         a null BitVector.
   */
  BitVector inverse_value_concat_new_random(const BitVectorDomain& d,
                                            const BitVector& min,
                                            const BitVector& max);
  /**
   * True to enable optimization for inverse_value computation of concat
   * and sign extension operands.
   */
  bool d_opt_concat_sext = false;
};

/* -------------------------------------------------------------------------- */

class BitVectorSlt : public BitVectorNode
{
 public:
  /** Constructors. */
  BitVectorSlt(RNG* rng,
               uint64_t size,
               BitVectorNode* child0,
               BitVectorNode* child1,
               bool opt_concat_sext = false);
  BitVectorSlt(RNG* rng,
               const BitVectorDomain& domain,
               BitVectorNode* child0,
               BitVectorNode* child1,
               bool opt_concat_sext = false);

  NodeKind kind() const override { return NodeKind::BV_SLT; }

  void evaluate() override;

  /**
   * IC:
   *   w/o const bits (IC_wo):
   *       pos_x = 0: t = 0 || s != min_signed_value
   *       pos_x = 1: t = 0 || s != max_signed_value
   *
   *   with const bits:
   *       pos_x = 0: t = 1 => (s != min_signed_value &&
   *                   ((MSB(x) = 0 && x_lo < s) ||
   *                    (MSB(x) != 0 && 1 o x_lo[bw-2:0] < s))) &&
   *                  t = 0 => ((MSB(x) = 1 && x_hi >= s) ||
   *                            (MSB(x) != 1 && 0 o x_hi[bw-2:0] >= s))))
   *       pos_x = 1: t = 1 => (s != max_signed_value &&
   *                            ((MSB(x) = 1 && s < x_hi) ||
   *                             (MSB(x) != 1 && s < 0 o x_hi[bw-2:0])))
   *                  t = 0 => ((MSB(x) = 0 && s >= x_lo) ||
   *                            (MSB(x) != 0 && s >= 1 o x_lo[bw-2:0])))
   */
  bool is_invertible(const BitVector& t,
                     uint64_t pos_x,
                     bool is_essential_check = false) override;

  /**
   * CC:
   *   w/o  const bits: true
   *   with const bits: pos_x = 0: t = false || (const(x) => x_lo != smax)
   *                    pos_x = 1: t = false || (const(x) => x_lo != smin)
   */
  bool is_consistent(const BitVector& t, uint64_t pos_x) override;

  const BitVector& inverse_value(const BitVector& t, uint64_t pos_x) override;

  const BitVector& consistent_value(const BitVector& t,
                                    uint64_t pos_x) override;

  std::tuple<BitVectorRange, BitVectorRange> compute_min_max_bounds(
      const BitVector& t, uint64_t pos_x) override;

 private:
  /**
   * Evaluate the assignment of this node.
   *
   * Helper to allow evaluating the assignment on construction (evaluate() is
   * virtual and cannot be called from the constructor).
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate();
  /**
   * Wrapper for evaluating the assignment and fixing the domain on construction
   * when all operands are constant.
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate_and_set_domain();
  /**
   * Helper for is_invertible().
   *
   * @note Does not and (must not) reset cached inverse.
   *
   * @param d The domain representing operand 'x'.
   * @param t The target value of this node.
   * @param pos_x The index of operand `x`.
   * @param is_essential_check True if called to determine is_essential(). For
   *                           is_essential() checks, we don't consider bounds
   *                           derived from top-level inequalities since this
   *                           may trap us in a cycle (see is_essential()).
   */
  bool _is_invertible(const BitVectorDomain* d,
                      const BitVector& t,
                      uint64_t pos_x,
                      bool is_essential_check);
  /**
   * Helper for concat-specific (when x is a concat) inverse value computation.
   * Attempts to find an inverse value by only changing the value of one of
   * the children of the concat.
   * @param s The value of the other operand.
   * @param t The target value of this node.
   * @param pos_x The index of operand `x`, which is a concat node.
   * @return The inverse value.
   */
  BitVector* inverse_value_concat(bool t, uint64_t pos_x, uint64_t pos_s);
  /**
   * Helper for inverse_value_concat()  to generate a new random value with
   * respect to the given domain and within given min/max range.
   * @param d The domain.
   * @param min The lower bound of the range.
   * @param max The upper bound of the range.
   * @return A random value within the given range, if there is one, else
   *         a null BitVector.
   */
  BitVector inverse_value_concat_new_random(const BitVectorDomain& d,
                                            const BitVector& min,
                                            const BitVector& max);
  /**
   * True to enable optimization for inverse_value computation of concat
   * and sign extension operands.
   */
  bool d_opt_concat_sext = false;
};

/* -------------------------------------------------------------------------- */

class BitVectorUrem : public BitVectorNode
{
 public:
  /** Constructors. */
  BitVectorUrem(RNG* rng,
                uint64_t size,
                BitVectorNode* child0,
                BitVectorNode* child1);
  BitVectorUrem(RNG* rng,
                const BitVectorDomain& domain,
                BitVectorNode* child0,
                BitVectorNode* child1);

  NodeKind kind() const override { return NodeKind::BV_UREM; }

  void evaluate() override;

  /**
   * IC:
   *   w/o const bits (IC_wo):
   *       pos_x = 0: ~(-s) >= t
   *       pos_x = 1: (t + t - s) & s >= t
   *
   *   with const bits:
   *       pos_x = 0: IC_wo &&
   *                  ((s = 0 || t = ones) => mfb(x, t)) &&
   *                  ((s != 0 && t != ones) => \exists y. (
   *                      mfb(x, s * y + t) && !umulo(s, y) && !uaddo(s *y, t)))
   *       pos_x = 1: IC_wo &&
   *                  (s = t => (x_lo = 0 || x_hi > t)) &&
   *                  (s != t => \exists y. (
   *                      mfb(x, y) && y > t && (s - t) mod y = 0)
   */
  bool is_invertible(const BitVector& t,
                     uint64_t pos_x,
                     bool is_essential_check = false) override;

  /**
   * CC:
   *  w/o  const bits: true
   *
   *  with const bits:
   *     pos_x = 0: (t = ones => mfb(x, ones)) &&
   *                (t != ones =>
   *                  (t > (ones - t) => mfb (x, t)) &&
   *                  (t < (ones - t) => mfb(x, t) ||
   *                   \exists y. (mfb(x, y) && y> 2*t))
   *
   *     pos_x = 1: mfb(x, 0) ||
   *                ((t = ones => mfb(x, 0)) &&
   *                 (t != ones => \exists y. (mfb(x, y) && y > t)))
   */
  bool is_consistent(const BitVector& t, uint64_t pos_x) override;

  const BitVector& inverse_value(const BitVector& t, uint64_t pos_x) override;

  const BitVector& consistent_value(const BitVector& t,
                                    uint64_t pos_x) override;

 private:
  /**
   * Evaluate the assignment of this node.
   *
   * Helper to allow evaluating the assignment on construction (evaluate() is
   * virtual and cannot be called from the constructor).
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate();
  /**
   * Wrapper for evaluating the assignment and fixing the domain on construction
   * when all operands are constant.
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate_and_set_domain();

  /**
   * Pick a consistent value for pos_x = 0 with x > t.
   * Returns a null bit-vector if no such value can be found.
   */
  BitVector consistent_value_pos0_aux(const BitVector& t);
  /** Cached inverse_value result. */
  std::unique_ptr<BitVectorDomain> d_inverse_domain;
};

/* -------------------------------------------------------------------------- */

class BitVectorXor : public BitVectorNode
{
 public:
  /** Constructors. */
  BitVectorXor(RNG* rng,
               uint64_t size,
               BitVectorNode* child0,
               BitVectorNode* child1);
  BitVectorXor(RNG* rng,
               const BitVectorDomain& domain,
               BitVectorNode* child0,
               BitVectorNode* child1);

  NodeKind kind() const override { return NodeKind::BV_XOR; }

  void evaluate() override;

  /**
   * IC:
   *   w/o  const bits: true
   *   with const bits: mfb(x, s^t)
   */
  bool is_invertible(const BitVector& t,
                     uint64_t pos_x,
                     bool is_essential_check = false) override;

  /**
   * CC:
   *   w/o  const bits: true
   *   with const bits: true
   */
  bool is_consistent(const BitVector& t, uint64_t pos_x) override;

  const BitVector& inverse_value(const BitVector& t, uint64_t pos_x) override;

  const BitVector& consistent_value(const BitVector& t,
                                    uint64_t pos_x) override;

 private:
  /**
   * Evaluate the assignment of this node.
   *
   * Helper to allow evaluating the assignment on construction (evaluate() is
   * virtual and cannot be called from the constructor).
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate();
  /**
   * Wrapper for evaluating the assignment and fixing the domain on construction
   * when all operands are constant.
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate_and_set_domain();
};

/* -------------------------------------------------------------------------- */

class BitVectorIte : public BitVectorNode
{
 public:
  /** Constructors. */
  BitVectorIte(RNG* rng,
               uint64_t size,
               BitVectorNode* child0,
               BitVectorNode* child1,
               BitVectorNode* child2);
  BitVectorIte(RNG* rng,
               const BitVectorDomain& domain,
               BitVectorNode* child0,
               BitVectorNode* child1,
               BitVectorNode* child2);

  NodeKind kind() const override { return NodeKind::ITE; }

  void evaluate() override;

  bool is_essential(const BitVector& t, uint64_t pos_x) override;

  /**
   * ite(_c, _t, _e)
   *
   * IC:
   *   w/o const bits (IC_wo):
   *       pos_x = 0: s0 == t || s1 == t
   *                  with s0 the value for '_t' branch
   *                  and s1 the value for '_e'
   *       pos_x = 1: s0 == true
   *                  with s0 the value for '_c'
   *       pos_x = 2: s0 == false
   *                  with s0 the value for '_c'
   *
   *   with const bits:
   *       pos_x = 0: (!is_fixed(x) && (s0 = t || s1 = t)) ||
   *                  (is_fixed_true(x) && s0 = t) ||
   *                  (is_fixed_false(x) && s1 = t)
   *                  with s0 the value for '_t' and s1 the value for '_e'
   *       pos_x = 1: s0 = true && mfb(x, t)
   *                  with s0 the value for '_c'
   *       pos_x = 2: s0 == false && mfb(x, t)
   *                  with s0 the value for '_c'
   */
  bool is_invertible(const BitVector& t,
                     uint64_t pos_x,
                     bool is_essential_check = false) override;

  /**
   * CC:
   *   w/o  const bits: true
   *   with const bits: true
   */
  bool is_consistent(const BitVector& t, uint64_t pos_x) override;

  const BitVector& inverse_value(const BitVector& t, uint64_t pos_x) override;

  const BitVector& consistent_value(const BitVector& t,
                                    uint64_t pos_x) override;

  std::tuple<uint64_t, bool, bool> select_path(
      const BitVector& t, std::vector<uint64_t>& ess_inputs) override;

 private:
  uint64_t select_path_non_const(std::vector<uint64_t>& inputs) const override;
  /**
   * Evaluate the assignment of this node.
   *
   * Helper to allow evaluating the assignment on construction (evaluate() is
   * virtual and cannot be called from the constructor).
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate();
  /**
   * Wrapper for evaluating the assignment and fixing the domain on construction
   * when all operands are constant.
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate_and_set_domain();
};

/* -------------------------------------------------------------------------- */

class BitVectorNot : public BitVectorNode
{
 public:
  /** Constructors. */
  BitVectorNot(RNG* rng, uint64_t size, BitVectorNode* child0);
  BitVectorNot(RNG* rng, const BitVectorDomain& domain, BitVectorNode* child0);

  NodeKind kind() const override { return NodeKind::BV_NOT; }

  void evaluate() override;

  bool is_essential(const BitVector& t, uint64_t pos_x) override;

  /**
   * IC:
   *   w/o  const bits: true
   *   with const bits: mfb(x, ~t)
   */
  bool is_invertible(const BitVector& t,
                     uint64_t pos_x,
                     bool is_essential_check = false) override;

  /**
   * CC:
   *   w/o  const bits: true
   *   with const bits: IC
   */
  bool is_consistent(const BitVector& t, uint64_t pos_x) override;

  const BitVector& inverse_value(const BitVector& t, uint64_t pos_x) override;

  const BitVector& consistent_value(const BitVector& t,
                                    uint64_t pos_x) override;

 private:
  /**
   * Evaluate the assignment of this node.
   *
   * Helper to allow evaluating the assignment on construction (evaluate() is
   * virtual and cannot be called from the constructor).
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate();
  /**
   * Wrapper for evaluating the assignment and fixing the domain on construction
   * when all operands are constant.
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate_and_set_domain();
};

/* -------------------------------------------------------------------------- */

class BitVectorExtract : public BitVectorNode
{
 public:
  /**
   * Constructor.
   * @param rng       The associated random number generator.
   * @param size      The size of the extract node.
   * @param child0    The child at index 0.
   * @param hi        The upper index.
   * @param lo        The lower index.
   * @param normalize True if this extract should be registered in its child
   *                  node for normalization.
   */
  BitVectorExtract(RNG* rng,
                   uint64_t size,
                   BitVectorNode* child0,
                   uint64_t hi,
                   uint64_t lo,
                   bool normalize);
  /**
   * Constructor.
   * @param rng       The associated random number generator.
   * @param domain    The associated bit-vector domain.
   * @param child0    The child at index 0.
   * @param hi        The upper index.
   * @param lo        The lower index.
   * @param normalize True if this extract should be registered in its child
   *                  node for normalization.
   */
  BitVectorExtract(RNG* rng,
                   const BitVectorDomain& domain,
                   BitVectorNode* child0,
                   uint64_t hi,
                   uint64_t lo,
                   bool normalize);

  NodeKind kind() const override { return NodeKind::BV_EXTRACT; }

  void evaluate() override;

  bool is_essential(const BitVector& t, uint64_t pos_x) override;

  /**
   * IC:
   *   w/o  const bits: true
   *   with const bits: mfb(x[hi:lo], t)
   */
  bool is_invertible(const BitVector& t,
                     uint64_t pos_x,
                     bool is_essential_check = false) override;

  /**
   * CC:
   *   w/o  const bits: true
   *   with const bits: IC
   */
  bool is_consistent(const BitVector& t, uint64_t pos_x) override;

  const BitVector& inverse_value(const BitVector& t, uint64_t pos_x) override;

  const BitVector& consistent_value(const BitVector& t,
                                    uint64_t pos_x) override;

  std::string str() const override;

  /** @return The upper index of this extract. */
  uint64_t hi() const;
  /** @return The lower index of this extract. */
  uint64_t lo() const;

  /**
   * Normalize node.
   * This caches the original child and indices, replaces the child with
   * the normalized node and resets its indices to represent a full slice
   * (upper index is msb, lower index is lsb).
   * @param node The normalized representation of the child node.
   */
  void normalize(BitVectorNode* node);

  /** @return True if this extract is normalized. */
  bool is_normalized() const;

 private:
  /**
   * Probability for keeping the current value of don't care bits (rather than
   * of fully randomizing all of them).
   */
  static constexpr uint32_t s_prob_keep = 500;

  /**
   * Evaluate the assignment of this node.
   *
   * Helper to allow evaluating the assignment on construction (evaluate() is
   * virtual and cannot be called from the constructor).
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate();
  /**
   * Wrapper for evaluating the assignment and fixing the domain on construction
   * when all operands are constant.
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate_and_set_domain();

  /** The upper index. */
  uint64_t d_hi;
  /** The lower index. */
  uint64_t d_lo;

  /**
   * Left part of don't care bits, that is, all bits > d_hi.
   * Nullptr if d_hi = msb.
   * Cache for inverse_value.
   */
  std::unique_ptr<BitVectorDomain> d_x_slice_left;
  /**
   * Right part of don't care bits, that is, all bits < d_lo.
   * Nullptr if d_lo = 0.
   * Cache for inverse_value.
   */
  std::unique_ptr<BitVectorDomain> d_x_slice_right;

  /**
   * Cache the original child node that has been replaced with a normalized
   * node. We cache to be able to restore in incremental mode.
   */
  BitVectorNode* d_child0_original = nullptr;
  /**
   * Cache the original upper index, replaced by the msb index after
   * normalization. We cache to be able to restore in incremental mode.
   */
  uint64_t d_hi_original = 0;
  /**
   * Cache the original lower index, replaced by the msb index after
   * normalization. We cache to be able to restore in incremental mode.
   */
  uint64_t d_lo_original = 0;
};

/* -------------------------------------------------------------------------- */

class BitVectorSignExtend : public BitVectorNode
{
 public:
  /** Constructors. */
  BitVectorSignExtend(RNG* rng,
                      uint64_t size,
                      BitVectorNode* child0,
                      uint64_t n);
  BitVectorSignExtend(RNG* rng,
                      const BitVectorDomain& domain,
                      BitVectorNode* child0,
                      uint64_t n);

  /**
   * Get the number of extension bits.
   * @return The number of extension bits.
   */
  uint64_t get_n() const { return d_n; }

  NodeKind kind() const override { return NodeKind::BV_SEXT; }

  void evaluate() override;

  BitVectorBounds normalize_bounds(const BitVectorRange& bounds_u,
                                   const BitVectorRange& bounds_s) override;

  bool is_essential(const BitVector& t, uint64_t pos_x) override;

  /**
   * IC:
   *   w/o  const bits (IC_wo): t_ext == ones || t_ext == zero
   *                            and t_x   = t[t_size - 1 - n : 0]
   *                            and t_ext = t[t_size - 1, t_size - 1 - n]
   *                            (i.e., it includes MSB of t_x)
   *
   *   with const bits: IC_wo && mfb(x, t_x)
   */
  bool is_invertible(const BitVector& t,
                     uint64_t pos_x,
                     bool is_essential_check = false) override;

  /**
   * CC:
   *   w/o  const bits: true
   *   with const bits: IC
   */
  bool is_consistent(const BitVector& t, uint64_t pos_x) override;

  const BitVector& inverse_value(const BitVector& t, uint64_t pos_x) override;

  const BitVector& consistent_value(const BitVector& t,
                                    uint64_t pos_x) override;

  std::string str() const override;

 private:
  /**
   * Evaluate the assignment of this node.
   *
   * Helper to allow evaluating the assignment on construction (evaluate() is
   * virtual and cannot be called from the constructor).
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate();
  /**
   * Wrapper for evaluating the assignment and fixing the domain on construction
   * when all operands are constant.
   *
   * @note We cannot assert that the assignment propagated from the inputs
   *       to the roots on evaluation matches constant bits information if it
   *       includes constant bit information propagated down from top-level
   *       constraints (and not only up from the inputs). This can happen if
   *       constant bit information is derived via the 'fixed' interface of
   *       a SAT solver (e.g., CaDiCaL, Lingeling), where the assignment of
   *       variables that are implied by the formula can be queried.
   */
  void _evaluate_and_set_domain();

  /** The number of bits to extend with. */
  uint64_t d_n;
};

/* -------------------------------------------------------------------------- */

}  // namespace ls
}  // namespace bzla
#endif
