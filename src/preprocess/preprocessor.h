#ifndef BZLA_PREPROCESS_PREPROCESSOR_H_INCLUDED
#define BZLA_PREPROCESS_PREPROCESSOR_H_INCLUDED

#include "backtrack/assertion_stack.h"
#include "backtrack/pop_callback.h"
#include "preprocess/pass/contradicting_ands.h"
#include "preprocess/pass/elim_lambda.h"
#include "preprocess/pass/elim_uninterpreted.h"
#include "preprocess/pass/embedded_constraints.h"
#include "preprocess/pass/flatten_and.h"
#include "preprocess/pass/normalize.h"
#include "preprocess/pass/rewrite.h"
#include "preprocess/pass/skeleton_preproc.h"
#include "preprocess/pass/variable_substitution.h"
#include "solver/result.h"

namespace bzla {

class SolvingContext;
class Env;

namespace util {
class Logger;
}

namespace preprocess {

class Preprocessor
{
  friend class BacktrackCallback;

 public:
  Preprocessor(SolvingContext& context);

  /** Preprocess current set of assertions. */
  Result preprocess();

  /** Preprocess given term based on last preprocess() call. */
  Node process(const Node& term);

 private:
  /** Apply all preprocessing passes to assertions until fixed-point. */
  void apply(AssertionVector& assertions);

  /** Synchronize d_backtrack_mgr up to given level. */
  void sync_scope(size_t level);

  Env& d_env;
  util::Logger& d_logger;

  /** Current set of assertions. */
  backtrack::AssertionView& d_assertions;

  /** Preprocessor backtrack manager. */
  backtrack::BacktrackManager d_backtrack_mgr;

  /** Global backtrack manager of solving context. */
  const backtrack::BacktrackManager& d_global_backtrack_mgr;

  /** Callback to sync d_backtrack_mgr with d_global_backtrack_mgr on pop(). */
  backtrack::PopCallback d_pop_callback;

  /** Preprocessing passes */
  pass::PassRewrite d_pass_rewrite;
  pass::PassContradictingAnds d_pass_contr_ands;
  pass::PassElimLambda d_pass_elim_lambda;
  pass::PassElimUninterpreted d_pass_elim_uninterpreted;
  pass::PassEmbeddedConstraints d_pass_embedded_constraints;
  pass::PassVariableSubstitution d_pass_variable_substitution;
  pass::PassFlattenAnd d_pass_flatten_and;
  pass::PassSkeletonPreproc d_pass_skeleton_preproc;
  pass::PassNormalize d_pass_normalize;

  struct Statistics
  {
    Statistics(util::Statistics& stats);
    util::TimerStatistic& time_preprocess;
    util::TimerStatistic& time_process;
    uint64_t& num_iterations;
  } d_stats;
};

}  // namespace preprocess
}  // namespace bzla

#endif
