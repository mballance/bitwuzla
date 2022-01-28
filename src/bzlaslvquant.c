/***
 * Bitwuzla: Satisfiability Modulo Theories (SMT) solver.
 *
 * This file is part of Bitwuzla.
 *
 * Copyright (C) 2007-2022 by the authors listed in the AUTHORS file.
 *
 * See COPYING for more information on using this software.
 */

#include "bzlaslvquant.h"

#include "bzlabeta.h"
#include "bzlabv.h"
#include "bzlaclone.h"
#include "bzlacore.h"
#include "bzlaexp.h"
#include "bzlamodel.h"
#include "bzlaprintmodel.h"
#include "bzlaslvfun.h"
#include "bzlasynth.h"
#include "preprocess/bzlader.h"
#include "preprocess/bzlaminiscope.h"
#include "preprocess/bzlanormquant.h"
#include "preprocess/bzlaskolemize.h"
#include "utils/bzlaabort.h"
#include "utils/bzlahashint.h"
#include "utils/bzlanodeiter.h"
#include "utils/bzlautil.h"

#ifdef BZLA_HAVE_PTHREADS
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#endif

struct BzlaQuantStats
{
  struct
  {
    uint32_t refinements;
    uint32_t failed_refinements;

    /* overall synthesize statistics */
    uint32_t synthesize_const;
    uint32_t synthesize_term;
    uint32_t synthesize_none;

    /* statistics for the currently synthesized model */
    uint32_t synthesize_model_const;
    uint32_t synthesize_model_term;
    uint32_t synthesize_model_none;
  } stats;

  struct
  {
    double e_solver;
    double f_solver;
    double synth;
    double refine;
    double qinst;
    double findpm;
    double checkinst;
  } time;
};

typedef struct BzlaQuantStats BzlaQuantStats;

struct BzlaGroundSolvers
{
  Bzla *forall; /* solver for checking the model */
  BzlaNode *forall_formula;
  BzlaNodeMap *forall_evars;     /* existential vars (map to skolem
                                    constants of exists solver) */
  BzlaNodeMap *forall_uvars;     /* universal vars map to fresh bv vars */
  BzlaNodeMap *forall_evar_deps; /* existential vars map to argument nodes
                                    of universal vars */
  BzlaNodeMap *forall_uvar_deps; /* universal vars map to argument nodes
                                    of existential vars */
  BzlaNodePtrStack forall_consts;
  BzlaPtrHashTable *forall_synth_model; /* currently synthesized model for
                                         existential vars */
  BzlaPtrHashTable *forall_ces;         /* counter examples */
  BzlaBitVectorTuple *forall_last_ce;
  BzlaNodeMap *forall_skolem; /* skolem functions for evars */

  Bzla *exists;              /* solver for computing the model */
  BzlaNodeMap *exists_evars; /* skolem constants (map to existential
                                vars of forall solver) */
  BzlaNodeMap *exists_ufs;   /* UFs (non-skolem constants), map to UFs
                                of forall solver */
  BzlaNodeMap *exists_cur_qi;
  BzlaSolverResult result;

  BzlaQuantStats statistics;

#ifdef BZLA_HAVE_PTHREADS
  bool *found_result;
  pthread_mutex_t *found_result_mutex;
#endif
};

typedef struct BzlaGroundSolvers BzlaGroundSolvers;

struct BzlaQuantSolver
{
  BZLA_SOLVER_STRUCT;

  BzlaGroundSolvers *gslv;  /* two ground solver instances */
  BzlaGroundSolvers *dgslv; /* two ground solver instances for dual */
};

typedef struct BzlaQuantSolver BzlaQuantSolver;

BZLA_DECLARE_STACK(BzlaBitVectorTuplePtr, BzlaBitVectorTuple *);

/*------------------------------------------------------------------------*/

struct SynthResult
{
  bool partial;
  uint32_t limit;
  BzlaNode *value;
};

typedef struct SynthResult SynthResult;

static SynthResult *
new_synth_result(BzlaMemMgr *mm)
{
  SynthResult *res;
  BZLA_CNEW(mm, res);
  return res;
}

static void
delete_synth_result(BzlaMemMgr *mm, SynthResult *res)
{
  BzlaNode *cur;

  if (res->value)
  {
    cur = bzla_node_real_addr(res->value);
    bzla_node_release(cur->bzla, cur);
  }
  BZLA_DELETE(mm, res);
}

/*------------------------------------------------------------------------*/

struct FlatModel
{
  BzlaMemMgr *mm;
  BzlaPtrHashTable *model;
  BzlaIntHashTable *uvar_index_map;
  BzlaIntHashTable *evar_index_map;
};

typedef struct FlatModel FlatModel;

static BzlaBitVector *
flat_model_get_value(FlatModel *flat_model,
                     BzlaNode *var,
                     BzlaBitVectorTuple *ce)
{
  uint32_t i;
  BzlaBitVectorTuple *t;
  BzlaPtrHashBucket *b;
  BzlaBitVector *res;

  if (bzla_node_param_is_exists_var(var))
  {
    i = bzla_hashint_map_get(flat_model->evar_index_map, var->id)->as_int;
    if (ce)
    {
      b = bzla_hashptr_table_get(flat_model->model, ce);
      assert(b);
      t   = b->data.as_ptr;
      res = t->bv[i];
    }
    else
    {
      b = flat_model->model->first;
      assert(b);
      t   = b->data.as_ptr;
      res = t->bv[i];
      /* value of 'var' is the same for every ce (outermost var) */
#ifndef NDEBUG
      BzlaPtrHashTableIterator it;
      BzlaBitVectorTuple *tup;
      bzla_iter_hashptr_init(&it, flat_model->model);
      while (bzla_iter_hashptr_has_next(&it))
      {
        tup = it.bucket->data.as_ptr;
        (void) bzla_iter_hashptr_next(&it);
        assert(bzla_bv_compare(res, tup->bv[i]) == 0);
      }
#endif
    }
  }
  else
  {
    assert(ce);
    assert(bzla_node_param_is_forall_var(var));
    i   = bzla_hashint_map_get(flat_model->uvar_index_map, var->id)->as_int;
    res = ce->bv[i];
  }
  return res;
}

static FlatModel *
flat_model_generate(BzlaGroundSolvers *gslv)
{
  bool free_bv;
  uint32_t i, j, pos, nevars;
  Bzla *e_solver, *f_solver;
  BzlaNode *cur, *e_evar, *f_evar, *args;
  BzlaPtrHashTableIterator it;
  BzlaNodeMapIterator nit;
  BzlaBitVectorTuple *ce, *mtup, *evar_values;
  const BzlaPtrHashTable *m;
  BzlaBitVector *bv;
  BzlaArgsIterator ait;
  BzlaPtrHashBucket *b;
  BzlaMemMgr *mm;
  FlatModel *flat_model;

  e_solver = gslv->exists;
  f_solver = gslv->forall;
  mm       = e_solver->mm;
  BZLA_CNEW(mm, flat_model);
  flat_model->mm    = mm;
  flat_model->model = bzla_hashptr_table_new(
      mm, (BzlaHashPtr) bzla_bv_hash_tuple, (BzlaCmpPtr) bzla_bv_compare_tuple);
  flat_model->uvar_index_map = bzla_hashint_map_new(mm);
  flat_model->evar_index_map = bzla_hashint_map_new(mm);

  nevars = gslv->exists_evars->table->count;

  i = 0;
  bzla_iter_nodemap_init(&nit, gslv->forall_uvars);
  while (bzla_iter_nodemap_has_next(&nit))
  {
    cur = bzla_iter_nodemap_next(&nit);
    bzla_hashint_map_add(flat_model->uvar_index_map, cur->id)->as_int = i++;
  }

  i = 0;
  bzla_iter_nodemap_init(&nit, gslv->forall_evars);
  while (bzla_iter_nodemap_has_next(&nit))
  {
    cur = bzla_iter_nodemap_next(&nit);
    bzla_hashint_map_add(flat_model->evar_index_map, cur->id)->as_int = i++;
  }

  /* generate model for exists vars/ufs */
  assert(e_solver->last_sat_result == BZLA_RESULT_SAT);
  e_solver->slv->api.generate_model(e_solver->slv, false, false);

  bzla_iter_hashptr_init(&it, gslv->forall_ces);
  while (bzla_iter_hashptr_has_next(&it))
  {
    ce = bzla_iter_hashptr_next(&it);

    pos         = 0;
    evar_values = bzla_bv_new_tuple(mm, nevars);
    bzla_iter_nodemap_init(&nit, gslv->forall_evars);
    while (bzla_iter_nodemap_has_next(&nit))
    {
      e_evar = nit.it.bucket->data.as_ptr;
      f_evar = bzla_iter_nodemap_next(&nit);

      free_bv = false;
      if ((args = bzla_nodemap_mapped(gslv->forall_evar_deps, f_evar)))
      {
        bv = 0;
        m  = bzla_model_get_fun(e_solver, e_evar);
        if (m)
        {
          mtup =
              bzla_bv_new_tuple(mm, bzla_node_args_get_arity(f_solver, args));
          j = 0;
          bzla_iter_args_init(&ait, args);
          while (bzla_iter_args_has_next(&ait))
          {
            cur = bzla_iter_args_next(&ait);
            i   = bzla_hashint_map_get(flat_model->uvar_index_map, cur->id)
                    ->as_int;
            bzla_bv_add_to_tuple(mm, mtup, ce->bv[i], j++);
          }
          b = bzla_hashptr_table_get((BzlaPtrHashTable *) m, mtup);
          bzla_bv_free_tuple(mm, mtup);
          if (b) bv = b->data.as_ptr;
        }
        if (!bv)
        {
          free_bv = true;
          bv      = bzla_bv_new(mm, bzla_node_bv_get_width(f_solver, f_evar));
        }
      }
      else
      {
        assert(bzla_node_param_is_exists_var(f_evar));
        bv = (BzlaBitVector *) bzla_model_get_bv(
            e_solver, bzla_simplify_exp(e_solver, e_evar));
      }
      bzla_bv_add_to_tuple(mm, evar_values, bv, pos++);
      if (free_bv) bzla_bv_free(mm, bv);
    }
    bzla_hashptr_table_add(flat_model->model, ce)->data.as_ptr = evar_values;
  }
  return flat_model;
}

static void
flat_model_free(FlatModel *flat_model)
{
  BzlaPtrHashTableIterator it;
  BzlaBitVectorTuple *t;
  BzlaMemMgr *mm;

  mm = flat_model->mm;

  bzla_iter_hashptr_init(&it, flat_model->model);
  while (bzla_iter_hashptr_has_next(&it))
  {
    t = it.bucket->data.as_ptr;
    /* not need to free ce in gslv->forall_ces */
    (void) bzla_iter_hashptr_next(&it);
    bzla_bv_free_tuple(mm, t);
  }
  bzla_hashptr_table_delete(flat_model->model);
  bzla_hashint_map_delete(flat_model->uvar_index_map);
  bzla_hashint_map_delete(flat_model->evar_index_map);
  BZLA_DELETE(mm, flat_model);
}

/*------------------------------------------------------------------------*/

static bool g_measure_thread_time = false;

static double
time_stamp(void)
{
  if (g_measure_thread_time) return bzla_util_process_time_thread();
  return bzla_util_time_stamp();
}

/*------------------------------------------------------------------------*/

static void
delete_model(BzlaGroundSolvers *gslv)
{
  BzlaNode *cur;
  BzlaPtrHashTableIterator it;
  SynthResult *synth_res;

  if (!gslv->forall_synth_model) return;

  bzla_iter_hashptr_init(&it, gslv->forall_synth_model);
  while (bzla_iter_hashptr_has_next(&it))
  {
    synth_res = it.bucket->data.as_ptr;
    cur       = bzla_iter_hashptr_next(&it);
    assert(bzla_node_is_uf(cur) || bzla_node_param_is_exists_var(cur));
    (void) cur;
    delete_synth_result(gslv->forall->mm, synth_res);
  }
  bzla_hashptr_table_delete(gslv->forall_synth_model);
  gslv->forall_synth_model = 0;
}

/* compute dependencies between existential variables and universal variables.
 * 'deps' maps existential variables to a list of universal variables by means
 * of an argument node.
 */
static void
compute_var_deps(Bzla *bzla,
                 BzlaNode *root,
                 BzlaNodeMap *edeps,
                 BzlaNodeMap *udeps)
{
  uint32_t i;
  BzlaNode *cur, *real_cur, *q, *args;
  BzlaNodePtrStack visit, fquants, equants, vars;
  BzlaMemMgr *mm;
  BzlaIntHashTable *map;
  BzlaHashTableData *d;

  mm = bzla->mm;

  BZLA_INIT_STACK(mm, vars);
  BZLA_INIT_STACK(mm, fquants);
  BZLA_INIT_STACK(mm, equants);
  BZLA_INIT_STACK(mm, visit);
  BZLA_PUSH_STACK(visit, root);
  map = bzla_hashint_map_new(mm);

  while (!BZLA_EMPTY_STACK(visit))
  {
    cur      = BZLA_POP_STACK(visit);
    real_cur = bzla_node_real_addr(cur);

    d = bzla_hashint_map_get(map, real_cur->id);
    if (!d)
    {
      bzla_hashint_map_add(map, real_cur->id);

      if (bzla_node_is_forall(real_cur)) BZLA_PUSH_STACK(fquants, real_cur);
      if (bzla_node_is_exists(real_cur)) BZLA_PUSH_STACK(equants, real_cur);

      BZLA_PUSH_STACK(visit, cur);
      for (i = 0; i < real_cur->arity; i++)
        BZLA_PUSH_STACK(visit, real_cur->e[i]);
    }
    else if (d->as_int == 0)
    {
      d->as_int = 1;
      if (bzla_node_is_exists(real_cur))
      {
        /* create dependency of 'real_cur' with all universal vars of
         * 'fquants' */
        if (BZLA_COUNT_STACK(fquants))
        {
          for (i = 0; i < BZLA_COUNT_STACK(fquants); i++)
          {
            q = BZLA_PEEK_STACK(fquants, i);
            BZLA_PUSH_STACK(vars, bzla_node_real_addr(q)->e[0]);
          }
          args = bzla_exp_args(bzla, vars.start, BZLA_COUNT_STACK(vars));
          bzla_nodemap_map(edeps, real_cur->e[0], args);
          bzla_node_release(bzla, args);
          BZLA_RESET_STACK(vars);
        }
        q = BZLA_POP_STACK(equants);
        assert(q == real_cur);
      }
      else if (bzla_node_is_forall(real_cur))
      {
        /* create dependency of 'real_cur' with all universal vars of
         * 'equants' */
        if (BZLA_COUNT_STACK(equants))
        {
          for (i = 0; i < BZLA_COUNT_STACK(equants); i++)
          {
            q = BZLA_PEEK_STACK(equants, i);
            BZLA_PUSH_STACK(vars, bzla_node_real_addr(q)->e[0]);
          }
          args = bzla_exp_args(bzla, vars.start, BZLA_COUNT_STACK(vars));
          bzla_nodemap_map(udeps, real_cur->e[0], args);
          bzla_node_release(bzla, args);
          BZLA_RESET_STACK(vars);
        }
        q = BZLA_POP_STACK(fquants);
        assert(q == real_cur);
      }
    }
  }
  bzla_hashint_map_delete(map);
  BZLA_RELEASE_STACK(visit);
  BZLA_RELEASE_STACK(fquants);
  BZLA_RELEASE_STACK(equants);
  BZLA_RELEASE_STACK(vars);
}

static BzlaNode *
mk_dual_formula(Bzla *bzla, Bzla *dual_bzla, BzlaNode *root)
{
  char *sym;
  size_t j;
  int32_t i;
  BzlaMemMgr *mm;
  BzlaNode *cur, *real_cur, *result, **e;
  BzlaNodePtrStack stack, args;
  BzlaIntHashTable *map;
  BzlaHashTableData *d;
  BzlaSortId sortid;

  mm  = bzla->mm;
  map = bzla_hashint_map_new(mm);

  BZLA_INIT_STACK(mm, stack);
  BZLA_INIT_STACK(mm, args);
  BZLA_PUSH_STACK(stack, root);
  while (!BZLA_EMPTY_STACK(stack))
  {
    cur      = BZLA_POP_STACK(stack);
    real_cur = bzla_node_real_addr(cur);
    d        = bzla_hashint_map_get(map, real_cur->id);

    if (!d)
    {
      bzla_hashint_table_add(map, real_cur->id);
      BZLA_PUSH_STACK(stack, cur);
      for (i = real_cur->arity - 1; i >= 0; i--)
        BZLA_PUSH_STACK(stack, real_cur->e[i]);
    }
    else if (!d->as_ptr)
    {
      /* bit vector variables should be existentially quantified */
      assert(!bzla_node_is_bv_var(real_cur));
      assert(BZLA_COUNT_STACK(args) >= real_cur->arity);
      args.top -= real_cur->arity;
      e = args.top;

      if (real_cur->arity == 0)
      {
        if (bzla_node_is_param(real_cur))
        {
          sym = bzla_node_get_symbol(bzla, real_cur);
          sortid =
              bzla_sort_bv(dual_bzla, bzla_node_bv_get_width(bzla, real_cur));
          result = bzla_exp_param(dual_bzla, sortid, sym);
          bzla_sort_release(dual_bzla, sortid);
        }
        else if (bzla_node_is_bv_const(real_cur))
        {
          result = bzla_exp_bv_const(dual_bzla,
                                     bzla_node_bv_const_get_bits(real_cur));
        }
        else
        {
          assert(bzla_node_is_uf(real_cur));
          sortid = bzla_clone_recursively_rebuild_sort(
              bzla, dual_bzla, real_cur->sort_id);
          result = bzla_exp_uf(dual_bzla, sortid, 0);
          bzla_sort_release(dual_bzla, sortid);
        }
      }
      else if (bzla_node_is_bv_slice(real_cur))
      {
        result = bzla_exp_bv_slice(dual_bzla,
                                   e[0],
                                   bzla_node_bv_slice_get_upper(real_cur),
                                   bzla_node_bv_slice_get_lower(real_cur));
      }
      /* invert quantifiers */
      else if (bzla_node_is_forall(real_cur))
        result = bzla_exp_exists(dual_bzla, e[0], e[1]);
      else if (bzla_node_is_exists(real_cur))
        result = bzla_exp_forall(dual_bzla, e[0], e[1]);
      else
      {
        result = bzla_exp_create(dual_bzla, real_cur->kind, e, real_cur->arity);
      }

      d->as_ptr = bzla_node_copy(dual_bzla, result);

      for (i = 0; i < real_cur->arity; i++) bzla_node_release(dual_bzla, e[i]);
    PUSH_RESULT:
      BZLA_PUSH_STACK(args, bzla_node_cond_invert(cur, result));
    }
    else
    {
      assert(d->as_ptr);
      result = bzla_node_copy(dual_bzla, d->as_ptr);
      goto PUSH_RESULT;
    }
  }
  assert(BZLA_COUNT_STACK(args) == 1);
  result = BZLA_POP_STACK(args);

  BZLA_RELEASE_STACK(stack);
  BZLA_RELEASE_STACK(args);

  for (j = 0; j < map->size; j++)
  {
    if (!map->data[j].as_ptr) continue;
    bzla_node_release(dual_bzla, map->data[j].as_ptr);
  }
  bzla_hashint_map_delete(map);
  return bzla_node_invert(result);
}

static void
collect_consts(Bzla *bzla, BzlaNode *root, BzlaNodePtrStack *consts)
{
  uint32_t i;
  int32_t id;
  BzlaNodePtrStack visit;
  BzlaIntHashTable *cache;
  BzlaNode *cur, *real_cur;
  BzlaMemMgr *mm;

  mm    = bzla->mm;
  cache = bzla_hashint_table_new(mm);
  BZLA_INIT_STACK(mm, visit);
  BZLA_PUSH_STACK(visit, root);
  while (!BZLA_EMPTY_STACK(visit))
  {
    cur      = BZLA_POP_STACK(visit);
    real_cur = bzla_node_real_addr(cur);

    id = bzla_node_is_bv_const(real_cur) ? bzla_node_get_id(cur) : real_cur->id;

    if (bzla_hashint_table_contains(cache, id)) continue;

    if (bzla_node_is_bv_const(real_cur)) BZLA_PUSH_STACK(*consts, cur);

    bzla_hashint_table_add(cache, id);
    for (i = 0; i < real_cur->arity; i++)
      BZLA_PUSH_STACK(visit, real_cur->e[i]);
  }
  BZLA_RELEASE_STACK(visit);
  bzla_hashint_table_delete(cache);
}

static BzlaGroundSolvers *
setup_solvers(BzlaQuantSolver *slv,
              BzlaNode *root,
              bool setup_dual,
              const char *prefix_forall,
              const char *prefix_exists)
{
  uint32_t width;
  char *sym;
  BzlaGroundSolvers *res;
  BzlaNode *cur, *var, *tmp;
  BzlaPtrHashTableIterator it;
  BzlaFunSolver *fslv;
  BzlaNodeMap *exp_map;
  Bzla *bzla;
  BzlaSortId dsortid, cdsortid, funsortid;
  BzlaMemMgr *mm;
  BzlaPtrHashTable *forall_ufs;

  bzla       = slv->bzla;
  mm         = bzla->mm;
  forall_ufs = bzla_hashptr_table_new(mm, 0, 0);
  BZLA_CNEW(mm, res);

  /* new forall solver */
  res->result = BZLA_RESULT_UNKNOWN;
  res->forall = bzla_new();
  bzla_opt_delete_opts(res->forall);
  bzla_opt_clone_opts(bzla, res->forall);
  bzla_set_msg_prefix(res->forall, prefix_forall);

  /* configure options */
  bzla_opt_set(res->forall, BZLA_OPT_PRODUCE_MODELS, 1);
  bzla_opt_set(res->forall, BZLA_OPT_INCREMENTAL, 1);

  if (setup_dual)
  {
    root = mk_dual_formula(bzla_node_real_addr(root)->bzla, res->forall, root);
  }
  else
  {
    exp_map = bzla_nodemap_new(bzla);
    tmp     = bzla_clone_recursively_rebuild_exp(
        bzla,
        res->forall,
        root,
        exp_map,
        bzla_opt_get(res->forall, BZLA_OPT_RW_LEVEL));
    /* all bv vars are quantified with exists */
    assert(res->forall->bv_vars->count == 0);
    bzla_nodemap_delete(exp_map);
    root = tmp;
  }
  assert(!bzla_node_is_proxy(root));

  res->forall_formula   = root;
  res->forall_evar_deps = bzla_nodemap_new(res->forall);
  res->forall_uvar_deps = bzla_nodemap_new(res->forall);
  compute_var_deps(
      res->forall, root, res->forall_evar_deps, res->forall_uvar_deps);
  res->forall_evars  = bzla_nodemap_new(res->forall);
  res->forall_uvars  = bzla_nodemap_new(res->forall);
  res->forall_skolem = bzla_nodemap_new(res->forall);
  res->forall_ces    = bzla_hashptr_table_new(res->forall->mm,
                                           (BzlaHashPtr) bzla_bv_hash_tuple,
                                           (BzlaCmpPtr) bzla_bv_compare_tuple);
  BZLA_INIT_STACK(res->forall->mm, res->forall_consts);
  collect_consts(res->forall, res->forall_formula, &res->forall_consts);

  /* store UFs in a separate table for later */
  bzla_iter_hashptr_init(&it, res->forall->ufs);
  while (bzla_iter_hashptr_has_next(&it))
  {
    cur = bzla_iter_hashptr_next(&it);
    bzla_hashptr_table_add(forall_ufs, cur);
  }

  /* map fresh bit vector vars to universal vars */
  bzla_iter_hashptr_init(&it, res->forall->forall_vars);
  while (bzla_iter_hashptr_has_next(&it))
  {
    cur = bzla_iter_hashptr_next(&it);
    assert(bzla_node_param_is_forall_var(cur));
    var = bzla_exp_var(res->forall, cur->sort_id, 0);
    bzla_nodemap_map(res->forall_uvars, cur, var);
    bzla_node_release(res->forall, var);
  }

  /* map fresh skolem constants to existential vars */
  bzla_iter_hashptr_init(&it, res->forall->exists_vars);
  while (bzla_iter_hashptr_has_next(&it))
  {
    cur = bzla_iter_hashptr_next(&it);
    assert(bzla_node_param_is_exists_var(cur));

    tmp = bzla_nodemap_mapped(res->forall_evar_deps, cur);
    if (tmp)
    {
      funsortid = bzla_sort_fun(res->forall, tmp->sort_id, cur->sort_id);
      var       = bzla_exp_uf(res->forall, funsortid, 0);
      bzla_sort_release(res->forall, funsortid);
    }
    else
      var = bzla_exp_var(res->forall, cur->sort_id, 0);

    bzla_nodemap_map(res->forall_skolem, cur, var);
    bzla_node_release(res->forall, var);
  }

  /* create ground solver for forall */
  assert(!res->forall->slv);
  fslv                = (BzlaFunSolver *) bzla_new_fun_solver(res->forall);
  fslv->assume_lemmas = true;
  res->forall->slv    = (BzlaSolver *) fslv;

  /* new exists solver */
  res->exists = bzla_new();
  bzla_opt_delete_opts(res->exists);
  bzla_opt_clone_opts(res->forall, res->exists);
  bzla_set_msg_prefix(res->exists, prefix_exists);
  bzla_opt_set(res->exists, BZLA_OPT_AUTO_CLEANUP_INTERNAL, 1);

  /* create ground solver for exists */
  res->exists->slv  = bzla_new_fun_solver(res->exists);
  res->exists_evars = bzla_nodemap_new(res->exists);
  res->exists_ufs   = bzla_nodemap_new(res->exists);

  /* map evars of exists solver to evars of forall solver */
  bzla_iter_hashptr_init(&it, res->forall->exists_vars);
  while (bzla_iter_hashptr_has_next(&it))
  {
    cur = bzla_iter_hashptr_next(&it);
    assert(bzla_node_param_is_exists_var(cur));
    width = bzla_node_bv_get_width(res->forall, cur);
    sym   = bzla_node_get_symbol(res->forall, cur);

    if ((tmp = bzla_nodemap_mapped(res->forall_evar_deps, cur)))
    {
      /* 'tmp' is an argument node that holds all universal dependencies of
       * existential variable 'cur'*/
      assert(bzla_node_is_args(tmp));

      cdsortid = bzla_sort_bv(res->exists, width);
      dsortid  = bzla_clone_recursively_rebuild_sort(
          res->forall, res->exists, tmp->sort_id);
      funsortid = bzla_sort_fun(res->exists, dsortid, cdsortid);
      var       = bzla_exp_uf(res->exists, funsortid, sym);
      bzla_sort_release(res->exists, cdsortid);
      bzla_sort_release(res->exists, dsortid);
      bzla_sort_release(res->exists, funsortid);
    }
    else
    {
      dsortid = bzla_sort_bv(res->exists, width);
      var     = bzla_exp_var(res->exists, dsortid, sym);
      bzla_sort_release(res->exists, dsortid);
    }
    bzla_nodemap_map(res->exists_evars, var, cur);
    bzla_nodemap_map(res->forall_evars, cur, var);
    bzla_node_release(res->exists, var);
  }

  /* map ufs of exists solver to ufs of forall solver */
  bzla_iter_hashptr_init(&it, forall_ufs);
  while (bzla_iter_hashptr_has_next(&it))
  {
    cur       = bzla_iter_hashptr_next(&it);
    funsortid = bzla_clone_recursively_rebuild_sort(
        res->forall, res->exists, cur->sort_id);
    var = bzla_exp_uf(
        res->exists, funsortid, bzla_node_get_symbol(res->forall, cur));
    bzla_sort_release(res->exists, funsortid);
    bzla_nodemap_map(res->exists_ufs, var, cur);
    bzla_node_release(res->exists, var);
  }
  bzla_hashptr_table_delete(forall_ufs);

  return res;
}

static void
delete_ground_solvers(BzlaQuantSolver *slv, BzlaGroundSolvers *gslv)
{
  BzlaPtrHashTableIterator it;
  BzlaBitVectorTuple *ce;

  /* delete exists solver */
  bzla_nodemap_delete(gslv->exists_evars);
  bzla_nodemap_delete(gslv->exists_ufs);

  /* delete forall solver */
  delete_model(gslv);
  bzla_nodemap_delete(gslv->forall_evars);
  bzla_nodemap_delete(gslv->forall_uvars);
  bzla_nodemap_delete(gslv->forall_evar_deps);
  bzla_nodemap_delete(gslv->forall_uvar_deps);
  bzla_nodemap_delete(gslv->forall_skolem);
  if (gslv->exists_cur_qi) bzla_nodemap_delete(gslv->exists_cur_qi);

  bzla_iter_hashptr_init(&it, gslv->forall_ces);
  while (bzla_iter_hashptr_has_next(&it))
  {
    if (it.bucket->data.as_ptr)
      bzla_bv_free_tuple(gslv->forall->mm, it.bucket->data.as_ptr);
    ce = bzla_iter_hashptr_next(&it);
    bzla_bv_free_tuple(gslv->forall->mm, ce);
  }
  bzla_hashptr_table_delete(gslv->forall_ces);
  BZLA_RELEASE_STACK(gslv->forall_consts);

  bzla_node_release(gslv->forall, gslv->forall_formula);
  bzla_delete(gslv->forall);
  bzla_delete(gslv->exists);
  BZLA_DELETE(slv->bzla->mm, gslv);
}

static BzlaNode *
build_refinement(Bzla *bzla, BzlaNode *root, BzlaNodeMap *map)
{
  assert(bzla);
  assert(root);
  assert(map);

  size_t j;
  int32_t i;
  BzlaMemMgr *mm;
  BzlaNode *cur, *real_cur, *result, **e;
  BzlaNodePtrStack visit, args;
  BzlaIntHashTable *mark;
  BzlaHashTableData *d;
  BzlaSortId sort;

  mm   = bzla->mm;
  mark = bzla_hashint_map_new(mm);
  BZLA_INIT_STACK(mm, visit);
  BZLA_INIT_STACK(mm, args);
  BZLA_PUSH_STACK(visit, root);

  while (!BZLA_EMPTY_STACK(visit))
  {
    cur      = BZLA_POP_STACK(visit);
    real_cur = bzla_node_real_addr(cur);
    assert(!bzla_node_is_proxy(real_cur));

    if ((result = bzla_nodemap_mapped(map, real_cur)))
    {
      result = bzla_node_copy(bzla, result);
      goto PUSH_RESULT;
    }

    d = bzla_hashint_map_get(mark, real_cur->id);
    if (!d)
    {
      (void) bzla_hashint_map_add(mark, real_cur->id);
      BZLA_PUSH_STACK(visit, cur);
      for (i = real_cur->arity - 1; i >= 0; i--)
        BZLA_PUSH_STACK(visit, real_cur->e[i]);
    }
    else if (!d->as_ptr)
    {
      assert(!bzla_node_is_param(real_cur)
             || !bzla_node_param_is_exists_var(real_cur)
             || !bzla_node_param_is_forall_var(real_cur));
      assert(!bzla_node_is_bv_var(real_cur));
      assert(!bzla_node_is_uf(real_cur));

      args.top -= real_cur->arity;
      e = args.top;

      if (bzla_node_is_bv_const(real_cur))
      {
        result = bzla_exp_bv_const(bzla, bzla_node_bv_const_get_bits(real_cur));
      }
      else if (bzla_node_is_param(real_cur))
      {
        assert(!bzla_node_param_is_exists_var(real_cur));
        assert(!bzla_node_param_is_forall_var(real_cur));
        sort   = bzla_sort_bv(bzla,
                            bzla_node_bv_get_width(real_cur->bzla, real_cur));
        result = bzla_exp_param(bzla, sort, 0);
        bzla_sort_release(bzla, sort);
      }
      else if (bzla_node_is_bv_slice(real_cur))
      {
        result = bzla_exp_bv_slice(bzla,
                                   e[0],
                                   bzla_node_bv_slice_get_upper(real_cur),
                                   bzla_node_bv_slice_get_lower(real_cur));
      }
      /* universal/existential vars get substituted */
      else if (bzla_node_is_quantifier(real_cur))
      {
        assert(!bzla_node_is_param(e[0]));
        result = bzla_node_copy(bzla, e[1]);
      }
      else
        result = bzla_exp_create(bzla, real_cur->kind, e, real_cur->arity);

      for (i = 0; i < real_cur->arity; i++) bzla_node_release(bzla, e[i]);

      d->as_ptr = bzla_node_copy(bzla, result);

    PUSH_RESULT:
      BZLA_PUSH_STACK(args, bzla_node_cond_invert(cur, result));
    }
    else
    {
      assert(d->as_ptr);
      result = bzla_node_copy(bzla, d->as_ptr);
      goto PUSH_RESULT;
    }
  }
  assert(BZLA_COUNT_STACK(args) == 1);
  result = BZLA_POP_STACK(args);

  BZLA_RELEASE_STACK(visit);
  BZLA_RELEASE_STACK(args);

  for (j = 0; j < mark->size; j++)
  {
    if (!mark->keys[j]) continue;
    assert(mark->data[j].as_ptr);
    bzla_node_release(bzla, mark->data[j].as_ptr);
  }
  bzla_hashint_map_delete(mark);

  return result;
}

static BzlaNode *
instantiate_args(Bzla *bzla, BzlaNode *args, BzlaNodeMap *map)
{
  assert(map);
  assert(bzla_node_is_args(args));

  BzlaNodePtrStack stack;
  BzlaArgsIterator it;
  BzlaNode *res, *arg, *mapped;
  BzlaMemMgr *mm;

  mm = bzla->mm;
  BZLA_INIT_STACK(mm, stack);
  bzla_iter_args_init(&it, args);
  while (bzla_iter_args_has_next(&it))
  {
    arg = bzla_iter_args_next(&it);
    assert(bzla_node_param_is_forall_var(arg));
    mapped = bzla_nodemap_mapped(map, arg);
    assert(mapped);
    BZLA_PUSH_STACK(stack, mapped);
  }
  res = bzla_exp_args(bzla, stack.start, BZLA_COUNT_STACK(stack));
  BZLA_RELEASE_STACK(stack);
  return res;
}

static void
refine_exists_solver(BzlaGroundSolvers *gslv, BzlaNodeMap *evar_map)
{
  assert(gslv->forall_uvars->table->count > 0);

  uint32_t i;
  Bzla *f_solver, *e_solver;
  BzlaNodeMap *map;
  BzlaNodeMapIterator it;
  BzlaNode *var_es, *var_fs, *c, *res, *uvar, *evar, *a;
  const BzlaBitVector *bv;
  BzlaBitVectorTuple *ce, *evar_tup;

  f_solver = gslv->forall;
  e_solver = gslv->exists;

  map = bzla_nodemap_new(f_solver);

  /* generate counter example for universal vars */
  assert(f_solver->last_sat_result == BZLA_RESULT_SAT);
  f_solver->slv->api.generate_model(f_solver->slv, false, false);

  /* instantiate universal vars with counter example */
  i  = 0;
  ce = bzla_bv_new_tuple(f_solver->mm, gslv->forall_uvars->table->count);
  bzla_iter_nodemap_init(&it, gslv->forall_uvars);
  while (bzla_iter_nodemap_has_next(&it))
  {
    var_fs = it.it.bucket->data.as_ptr;
    uvar   = bzla_iter_nodemap_next(&it);
    bv     = bzla_model_get_bv(f_solver, bzla_simplify_exp(f_solver, var_fs));
    c      = bzla_exp_bv_const(e_solver, (BzlaBitVector *) bv);
    bzla_nodemap_map(map, uvar, c);
    bzla_node_release(e_solver, c);
    bzla_bv_add_to_tuple(f_solver->mm, ce, bv, i++);
  }

  i        = 0;
  evar_tup = 0;
  if (gslv->forall_evars->table->count)
  {
    evar_tup =
        bzla_bv_new_tuple(f_solver->mm, gslv->forall_evars->table->count);
    bzla_iter_nodemap_init(&it, gslv->forall_evars);
    while (bzla_iter_nodemap_has_next(&it))
    {
      evar   = bzla_iter_nodemap_next(&it);
      var_fs = bzla_nodemap_mapped(evar_map, evar);
      assert(var_fs);
      bv = bzla_model_get_bv(f_solver, bzla_simplify_exp(f_solver, var_fs));
      bzla_bv_add_to_tuple(f_solver->mm, evar_tup, bv, i++);
    }
  }

  /* map existential variables to skolem constants */
  bzla_iter_nodemap_init(&it, gslv->forall_evars);
  while (bzla_iter_nodemap_has_next(&it))
  {
    var_es = it.it.bucket->data.as_ptr;
    var_fs = bzla_iter_nodemap_next(&it);

    a = bzla_nodemap_mapped(gslv->forall_evar_deps, var_fs);
    if (a)
    {
      assert(bzla_node_is_uf(var_es));
      a      = instantiate_args(e_solver, a, map);
      var_es = bzla_exp_apply(e_solver, var_es, a);
      bzla_nodemap_map(map, var_fs, var_es);
      bzla_node_release(e_solver, a);
      bzla_node_release(e_solver, var_es);
    }
    else
      bzla_nodemap_map(map, var_fs, var_es);
  }

  /* map UFs */
  bzla_iter_nodemap_init(&it, gslv->exists_ufs);
  while (bzla_iter_nodemap_has_next(&it))
  {
    var_fs = it.it.bucket->data.as_ptr;
    var_es = bzla_iter_nodemap_next(&it);
    bzla_nodemap_map(map, var_fs, var_es);
  }

  res = build_refinement(e_solver, gslv->forall_formula, map);

  bzla_nodemap_delete(map);

  assert(res != e_solver->true_exp);
  BZLA_ABORT(res == e_solver->true_exp,
             "invalid refinement '%s'",
             bzla_util_node2string(res));
  gslv->statistics.stats.refinements++;

  assert(!bzla_hashptr_table_get(gslv->forall_ces, ce));
  bzla_hashptr_table_add(gslv->forall_ces, ce)->data.as_ptr = evar_tup;
  gslv->forall_last_ce                                      = ce;

  bzla_assert_exp(e_solver, res);
  bzla_node_release(e_solver, res);
}

static BzlaNode *
mk_concrete_ite_model(BzlaGroundSolvers *gslv, BzlaNode *evar, FlatModel *model)

{
  assert(model);

  uint32_t i;
  bool opt_synth_complete;
  BzlaNode *uf;
  BzlaNode *res, *c, *cond, *e_if, *e_else, *tmp, *eq, *args, *uvar;
  BzlaPtrHashTableIterator it;
  BzlaNodePtrStack params;
  BzlaBitVector *value, *bv;
  BzlaBitVectorTuple *ce;
  BzlaSortId ufsortid;
  BzlaMemMgr *mm;
  Bzla *bzla;
  BzlaArgsIterator ait;

  bzla = gslv->forall;
  mm   = bzla->mm;
  BZLA_INIT_STACK(mm, params);
  opt_synth_complete =
      bzla_opt_get(bzla, BZLA_OPT_QUANT_SYNTH_ITE_COMPLETE) == 1;

  args = bzla_nodemap_mapped(gslv->forall_evar_deps, evar);
  assert(args);

  /* create params from domain sort */
  bzla_iter_args_init(&ait, args);
  while (bzla_iter_args_has_next(&ait))
    BZLA_PUSH_STACK(params, bzla_iter_args_next(&ait));

  if (opt_synth_complete)
    e_else = bzla_exp_bv_zero(bzla, evar->sort_id);
  else
  {
    ufsortid = bzla_sort_fun(bzla, args->sort_id, evar->sort_id);
    uf       = bzla_exp_uf(bzla, ufsortid, 0);
    bzla_sort_release(bzla, ufsortid);
    e_else = bzla_exp_apply(bzla, uf, args);
    assert(bzla_node_real_addr(e_else)->sort_id
           == bzla_sort_fun_get_codomain(bzla, uf->sort_id));
    bzla_node_release(bzla, uf);
  }

  /* generate ITEs */
  res = 0;
  bzla_iter_hashptr_init(&it, gslv->forall_ces);
  while (bzla_iter_hashptr_has_next(&it))
  {
    ce    = bzla_iter_hashptr_next(&it);
    value = flat_model_get_value(model, evar, ce);

    cond = 0;
    for (i = 0; i < BZLA_COUNT_STACK(params); i++)
    {
      uvar = BZLA_PEEK_STACK(params, i);
      bv   = flat_model_get_value(model, uvar, ce);
      c    = bzla_exp_bv_const(bzla, bv);

      eq = bzla_exp_eq(bzla, uvar, c);
      bzla_node_release(bzla, c);

      if (cond)
      {
        tmp = bzla_exp_bv_and(bzla, cond, eq);
        bzla_node_release(bzla, cond);
        bzla_node_release(bzla, eq);
        cond = tmp;
      }
      else
        cond = eq;
    }
    assert(cond);

    /* create ITE */
    e_if = bzla_exp_bv_const(bzla, value);
    res  = bzla_exp_cond(bzla, cond, e_if, e_else);

    bzla_node_release(bzla, cond);
    bzla_node_release(bzla, e_if);
    bzla_node_release(bzla, e_else);
    e_else = res;
  }
  assert(res);

  BZLA_RELEASE_STACK(params);
  return res;
}

/*------------------------------------------------------------------------*/

static BzlaQuantSolver *
clone_quant_solver(Bzla *clone, Bzla *bzla, BzlaNodeMap *exp_map)
{
  (void) clone;
  (void) bzla;
  (void) exp_map;
  return 0;
}

static void
delete_quant_solver(BzlaQuantSolver *slv)
{
  assert(slv);
  assert(slv->kind == BZLA_QUANT_SOLVER_KIND);
  assert(slv->bzla);
  assert(slv->bzla->slv == (BzlaSolver *) slv);

  Bzla *bzla;
  bzla = slv->bzla;
  delete_ground_solvers(slv, slv->gslv);
  if (slv->dgslv) delete_ground_solvers(slv, slv->dgslv);
  BZLA_DELETE(bzla->mm, slv);
  bzla->slv = 0;
}

/*------------------------------------------------------------------------*/

static void
build_input_output_values(BzlaGroundSolvers *gslv,
                          BzlaNode *evar,
                          FlatModel *flat_model,
                          BzlaBitVectorTuplePtrStack *value_in,
                          BzlaBitVectorPtrStack *value_out)
{
  uint32_t i, pos;
  BzlaPtrHashTableIterator it;
  BzlaBitVector *out;
  BzlaBitVectorTuple *in, *uvar_tup, *evar_tup;
  BzlaMemMgr *mm;
  Bzla *bzla;

  bzla = gslv->forall;
  mm   = bzla->mm;

  bzla_iter_hashptr_init(&it, flat_model->model);
  while (bzla_iter_hashptr_has_next(&it))
  {
    evar_tup = it.bucket->data.as_ptr;
    uvar_tup = bzla_iter_hashptr_next(&it);

    in = bzla_bv_new_tuple(mm, uvar_tup->arity + evar_tup->arity);

    pos = 0;
    for (i = 0; i < uvar_tup->arity; i++)
      bzla_bv_add_to_tuple(mm, in, uvar_tup->bv[i], pos++);
    for (i = 0; i < evar_tup->arity; i++)
      bzla_bv_add_to_tuple(mm, in, evar_tup->bv[i], pos++);

    out = flat_model_get_value(flat_model, evar, uvar_tup);
    BZLA_PUSH_STACK(*value_in, in);
    BZLA_PUSH_STACK(*value_out, bzla_bv_copy(mm, out));
  }
  assert(BZLA_COUNT_STACK(*value_in) == BZLA_COUNT_STACK(*value_out));
}

static BzlaBitVector *
eval_exp(Bzla *bzla,
         BzlaNode *exp,
         FlatModel *flat_model,
         BzlaBitVectorTuple *ce)
{
  assert(bzla);

  size_t j;
  int32_t i;
  BzlaNode *cur, *real_cur;
  BzlaNodePtrStack visit;
  BzlaIntHashTable *cache;
  BzlaHashTableData *d;
  BzlaBitVectorPtrStack arg_stack;
  BzlaMemMgr *mm;
  BzlaBitVector **bv, *result, *inv_result, *a;

  mm    = bzla->mm;
  cache = bzla_hashint_map_new(mm);

  BZLA_INIT_STACK(mm, arg_stack);
  BZLA_INIT_STACK(mm, visit);
  BZLA_PUSH_STACK(visit, exp);
  while (!BZLA_EMPTY_STACK(visit))
  {
    cur      = BZLA_POP_STACK(visit);
    real_cur = bzla_node_real_addr(cur);

    d = bzla_hashint_map_get(cache, real_cur->id);
    if (!d)
    {
      bzla_hashint_map_add(cache, real_cur->id);
      BZLA_PUSH_STACK(visit, cur);

      if (bzla_node_is_apply(real_cur)) continue;

      for (i = real_cur->arity - 1; i >= 0; i--)
        BZLA_PUSH_STACK(visit, real_cur->e[i]);
    }
    else if (!d->as_ptr)
    {
      assert(!bzla_node_is_fun(real_cur));
      assert(!bzla_node_is_apply(real_cur));
      assert(!bzla_node_is_bv_var(real_cur));

      arg_stack.top -= real_cur->arity;
      bv = arg_stack.top;

      switch (real_cur->kind)
      {
        case BZLA_BV_CONST_NODE:
          result = bzla_bv_copy(mm, bzla_node_bv_const_get_bits(real_cur));
          break;

        case BZLA_PARAM_NODE:
          a      = flat_model_get_value(flat_model, real_cur, ce);
          result = bzla_bv_copy(mm, a);
          break;

        case BZLA_BV_SLICE_NODE:
          result = bzla_bv_slice(mm,
                                 bv[0],
                                 bzla_node_bv_slice_get_upper(real_cur),
                                 bzla_node_bv_slice_get_lower(real_cur));
          break;

        case BZLA_BV_AND_NODE: result = bzla_bv_and(mm, bv[0], bv[1]); break;

        case BZLA_BV_EQ_NODE: result = bzla_bv_eq(mm, bv[0], bv[1]); break;

        case BZLA_BV_ADD_NODE: result = bzla_bv_add(mm, bv[0], bv[1]); break;

        case BZLA_BV_MUL_NODE: result = bzla_bv_mul(mm, bv[0], bv[1]); break;

        case BZLA_BV_ULT_NODE: result = bzla_bv_ult(mm, bv[0], bv[1]); break;

        case BZLA_BV_SLT_NODE: result = bzla_bv_slt(mm, bv[0], bv[1]); break;

        case BZLA_BV_SLL_NODE: result = bzla_bv_sll(mm, bv[0], bv[1]); break;

        case BZLA_BV_SRL_NODE: result = bzla_bv_srl(mm, bv[0], bv[1]); break;

        case BZLA_BV_UDIV_NODE: result = bzla_bv_udiv(mm, bv[0], bv[1]); break;

        case BZLA_BV_UREM_NODE: result = bzla_bv_urem(mm, bv[0], bv[1]); break;

        case BZLA_BV_CONCAT_NODE:
          result = bzla_bv_concat(mm, bv[0], bv[1]);
          break;

        case BZLA_EXISTS_NODE:
        case BZLA_FORALL_NODE: result = bzla_bv_copy(mm, bv[1]); break;

        default:
          assert(real_cur->kind == BZLA_COND_NODE);
          if (bzla_bv_is_true(bv[0]))
            result = bzla_bv_copy(mm, bv[1]);
          else
            result = bzla_bv_copy(mm, bv[2]);
      }

      if (!bzla_node_is_apply(real_cur))
      {
        for (i = 0; i < real_cur->arity; i++) bzla_bv_free(mm, bv[i]);
      }

      d->as_ptr = bzla_bv_copy(mm, result);

    EVAL_EXP_PUSH_RESULT:
      if (bzla_node_is_inverted(cur))
      {
        inv_result = bzla_bv_not(mm, result);
        bzla_bv_free(mm, result);
        result = inv_result;
      }
      BZLA_PUSH_STACK(arg_stack, result);
    }
    else
    {
      result = bzla_bv_copy(mm, d->as_ptr);
      goto EVAL_EXP_PUSH_RESULT;
    }
  }

  assert(BZLA_COUNT_STACK(arg_stack) == 1);
  result = BZLA_POP_STACK(arg_stack);

  for (j = 0; j < cache->size; j++)
  {
    a = cache->data[j].as_ptr;
    if (!a) continue;
    bzla_bv_free(mm, a);
  }
  BZLA_RELEASE_STACK(visit);
  BZLA_RELEASE_STACK(arg_stack);
  bzla_hashint_map_delete(cache);

  return result;
}

static void
update_flat_model(BzlaGroundSolvers *gslv,
                  FlatModel *flat_model,
                  BzlaNode *evar,
                  BzlaNode *result)
{
  uint32_t evar_pos;
  BzlaPtrHashTableIterator it;
  BzlaBitVectorTuple *ce, *evalues;
  BzlaBitVector *bv;
  BzlaPtrHashBucket *b;
  Bzla *bzla;
  BzlaMemMgr *mm;

  bzla     = gslv->forall;
  mm       = bzla->mm;
  evar_pos = bzla_hashint_map_get(flat_model->evar_index_map, evar->id)->as_int;

  bzla_iter_hashptr_init(&it, flat_model->model);
  while (bzla_iter_hashptr_has_next(&it))
  {
    b       = it.bucket;
    evalues = b->data.as_ptr;
    ce      = bzla_iter_hashptr_next(&it);
    bzla_bv_free(mm, evalues->bv[evar_pos]);
    bv                    = eval_exp(bzla, result, flat_model, ce);
    evalues->bv[evar_pos] = bv;
  }
}

static void
select_inputs(BzlaGroundSolvers *gslv, BzlaNode *var, BzlaNodePtrStack *inputs)
{
  BzlaNodeMapIterator nit;
  BzlaArgsIterator it;
  BzlaNode *args, *cur;

  if (bzla_node_param_is_exists_var(var))
  {
    args = bzla_nodemap_mapped(gslv->forall_evar_deps, var);
    bzla_iter_args_init(&it, args);
    while (bzla_iter_args_has_next(&it))
    {
      cur = bzla_iter_args_next(&it);
      BZLA_PUSH_STACK(*inputs, cur);
    }
  }
  else
  {
    assert(bzla_node_param_is_forall_var(var));
    bzla_iter_nodemap_init(&nit, gslv->exists_evars);
    while (bzla_iter_nodemap_has_next(&nit))
    {
      cur = bzla_iter_nodemap_next(&nit);
      BZLA_PUSH_STACK(*inputs, cur);
    }
  }
}

static BzlaNode *
synthesize(BzlaGroundSolvers *gslv,
           BzlaNode *evar,
           FlatModel *flat_model,
           uint32_t limit,
           BzlaNode *prev_synth)
{
  uint32_t i, pos, opt_synth_mode;
  BzlaNode *cur, *par, *result = 0;
  BzlaNodePtrStack visit;
  BzlaMemMgr *mm;
  BzlaIntHashTable *reachable, *cache, *value_in_map;
  BzlaNodeIterator it;
  BzlaNodePtrStack constraints, inputs;
  BzlaBitVectorTuplePtrStack value_in;
  BzlaBitVectorPtrStack value_out;
  BzlaNodeMapIterator nit;

  mm             = gslv->forall->mm;
  reachable      = bzla_hashint_table_new(mm);
  cache          = bzla_hashint_table_new(mm);
  value_in_map   = bzla_hashint_map_new(mm);
  opt_synth_mode = bzla_opt_get(gslv->forall, BZLA_OPT_QUANT_SYNTH);

  BZLA_INIT_STACK(mm, value_in);
  BZLA_INIT_STACK(mm, value_out);
  BZLA_INIT_STACK(mm, constraints);
  BZLA_INIT_STACK(mm, visit);
  BZLA_INIT_STACK(mm, inputs);

  /* value_in_map maps variables to the position in the assignment vector
   * value_in[k] */
  pos = 0;
  bzla_iter_nodemap_init(&nit, gslv->forall_uvars);
  bzla_iter_nodemap_queue(&nit, gslv->forall_evars);
  while (bzla_iter_nodemap_has_next(&nit))
  {
    cur = bzla_iter_nodemap_next(&nit);
    bzla_hashint_map_add(value_in_map, cur->id)->as_int = pos++;
  }

  select_inputs(gslv, evar, &inputs);

  /* 'evar' is a special placeholder for constraint evaluation */
  bzla_hashint_map_add(value_in_map, evar->id)->as_int = -1;

  build_input_output_values(gslv, evar, flat_model, &value_in, &value_out);

  if (opt_synth_mode == BZLA_QUANT_SYNTH_EL
      || opt_synth_mode == BZLA_QUANT_SYNTH_EL_ELMC)
  {
    result = bzla_synthesize_term(gslv->forall,
                                  inputs.start,
                                  BZLA_COUNT_STACK(inputs),
                                  value_in.start,
                                  value_out.start,
                                  BZLA_COUNT_STACK(value_in),
                                  value_in_map,
                                  constraints.start,
                                  BZLA_COUNT_STACK(constraints),
                                  gslv->forall_consts.start,
                                  BZLA_COUNT_STACK(gslv->forall_consts),
                                  limit,
                                  0,
                                  prev_synth);
  }

  if (!result
      && (opt_synth_mode == BZLA_QUANT_SYNTH_ELMC
          || opt_synth_mode == BZLA_QUANT_SYNTH_EL_ELMC))
  {
    /* mark reachable exps */
    BZLA_PUSH_STACK(visit, gslv->forall_formula);
    while (!BZLA_EMPTY_STACK(visit))
    {
      cur = bzla_node_real_addr(BZLA_POP_STACK(visit));

      if (bzla_hashint_table_contains(reachable, cur->id)) continue;

      bzla_hashint_table_add(reachable, cur->id);
      for (i = 0; i < cur->arity; i++) BZLA_PUSH_STACK(visit, cur->e[i]);
    }

    assert(bzla_hashint_table_contains(reachable, evar->id));

    /* collect constraints in cone of 'evar' */
    BZLA_PUSH_STACK(visit, evar);
    while (!BZLA_EMPTY_STACK(visit))
    {
      cur = bzla_node_real_addr(BZLA_POP_STACK(visit));

      if (!bzla_hashint_table_contains(reachable, cur->id)
          || bzla_hashint_table_contains(cache, cur->id))
        continue;

      /* cut-off at boolean layer */
      if (bzla_node_bv_get_width(gslv->forall, cur) == 1)
      {
        BZLA_PUSH_STACK(constraints, cur);
        continue;
      }

      bzla_hashint_table_add(cache, cur->id);
      bzla_iter_parent_init(&it, cur);
      while (bzla_iter_parent_has_next(&it))
      {
        par = bzla_iter_parent_next(&it);
        BZLA_PUSH_STACK(visit, par);
      }
    }
  }
  else if (opt_synth_mode == BZLA_QUANT_SYNTH_ELMR)
  {
    assert(opt_synth_mode == BZLA_QUANT_SYNTH_ELMR);
    BZLA_PUSH_STACK(constraints, gslv->forall_formula);
  }

  if (!result)
  {
    result = bzla_synthesize_term(gslv->forall,
                                  inputs.start,
                                  BZLA_COUNT_STACK(inputs),
                                  value_in.start,
                                  value_out.start,
                                  BZLA_COUNT_STACK(value_in),
                                  value_in_map,
                                  constraints.start,
                                  BZLA_COUNT_STACK(constraints),
                                  gslv->forall_consts.start,
                                  BZLA_COUNT_STACK(gslv->forall_consts),
                                  limit,
                                  0,
                                  0);
  }

  if (result && bzla_opt_get(gslv->forall, BZLA_OPT_QUANT_FIXSYNTH))
    update_flat_model(gslv, flat_model, evar, result);

  while (!BZLA_EMPTY_STACK(value_in))
    bzla_bv_free_tuple(mm, BZLA_POP_STACK(value_in));
  while (!BZLA_EMPTY_STACK(value_out))
    bzla_bv_free(mm, BZLA_POP_STACK(value_out));

  BZLA_RELEASE_STACK(inputs);

  bzla_hashint_map_delete(value_in_map);
  bzla_hashint_table_delete(reachable);
  bzla_hashint_table_delete(cache);
  BZLA_RELEASE_STACK(value_in);
  BZLA_RELEASE_STACK(value_out);
  BZLA_RELEASE_STACK(visit);
  BZLA_RELEASE_STACK(constraints);
  return result;
}

static BzlaPtrHashTable *
synthesize_model(BzlaGroundSolvers *gslv, FlatModel *flat_model)
{
  uint32_t limit;
  uint32_t opt_synth_limit, opt_synth_mode;
  BzlaPtrHashTable *synth_model, *prev_synth_model;
  Bzla *f_solver;
  BzlaNode *evar, *prev_synth_fun, *candidate;
  BzlaNodeMapIterator it;
  const BzlaBitVector *bv;
  SynthResult *synth_res, *prev_synth_res;
  BzlaPtrHashBucket *b;
  BzlaMemMgr *mm;

  f_solver         = gslv->forall;
  mm               = f_solver->mm;
  prev_synth_model = gslv->forall_synth_model;
  synth_model      = bzla_hashptr_table_new(mm, 0, 0);
  opt_synth_mode   = bzla_opt_get(f_solver, BZLA_OPT_QUANT_SYNTH);
  opt_synth_limit  = bzla_opt_get(f_solver, BZLA_OPT_QUANT_SYNTH_LIMIT);

  /* reset stats for currently synthesized model */
  gslv->statistics.stats.synthesize_model_const = 0;
  gslv->statistics.stats.synthesize_model_term  = 0;
  gslv->statistics.stats.synthesize_model_none  = 0;

  /* map existential variables to their resp. assignment */
  bzla_iter_nodemap_init(&it, gslv->forall_evars);
  // TODO: no UFs supported for now
  //  bzla_iter_nodemap_queue (&it, gslv->exists_ufs);
  while (bzla_iter_nodemap_has_next(&it))
  {
    evar = bzla_iter_nodemap_next(&it);
    assert(bzla_node_is_uf(evar) || bzla_node_param_is_exists_var(evar));

    if (bzla_terminate(gslv->forall)) break;

    synth_res = new_synth_result(mm);
    /* map skolem functions to resp. synthesized functions */
    if (bzla_nodemap_mapped(gslv->forall_evar_deps, evar)
        || bzla_node_is_uf(evar))
    {
      prev_synth_res = 0;
      prev_synth_fun = 0;
      candidate      = 0;
      if (opt_synth_mode)
      {
        limit = opt_synth_limit;

        /* check previously synthesized function */
        if (prev_synth_model
            && (b = bzla_hashptr_table_get(prev_synth_model, evar)))
        {
          prev_synth_res = b->data.as_ptr;
          assert(prev_synth_res);

          limit = prev_synth_res->limit;
          if (!prev_synth_res->partial) prev_synth_fun = prev_synth_res->value;
          /* we did not find expressions that cover all input/output
           * pairs previously, increase previous limit */
          else
            limit = limit * 1.5;
        }

        // TODO: set limit of UFs to 10000 fixed
        if (limit > opt_synth_limit * 10) limit = opt_synth_limit;

        candidate = synthesize(gslv, evar, flat_model, limit, prev_synth_fun);
        synth_res->limit = limit;
      }

      assert(!bzla_node_is_uf(evar));
      if (candidate)
      {
        synth_res->partial = false;
        if (bzla_node_is_bv_const(candidate))
          gslv->statistics.stats.synthesize_const++;
        else
          gslv->statistics.stats.synthesize_model_term++;
        synth_res->value = candidate;
      }
      else
      {
        synth_res->value   = mk_concrete_ite_model(gslv, evar, flat_model);
        synth_res->partial = true;
        gslv->statistics.stats.synthesize_model_none++;
      }
    }
    else
    {
      bv               = flat_model_get_value(flat_model, evar, 0);
      synth_res->value = bzla_exp_bv_const(f_solver, (BzlaBitVector *) bv);
    }
    assert(synth_res->value);
    bzla_hashptr_table_add(synth_model, evar)->data.as_ptr = synth_res;
  }

  /* update overall synthesize statistics */
  gslv->statistics.stats.synthesize_const +=
      gslv->statistics.stats.synthesize_model_const;
  gslv->statistics.stats.synthesize_term +=
      gslv->statistics.stats.synthesize_model_term;
  gslv->statistics.stats.synthesize_none +=
      gslv->statistics.stats.synthesize_model_none;

  return synth_model;
}

static void
update_formula(BzlaGroundSolvers *gslv)
{
  Bzla *forall;
  BzlaNode *f, *g;

  forall = gslv->forall;
  f      = gslv->forall_formula;
  /* update formula if changed via simplifications */
  if (bzla_node_is_proxy(f))
  {
    g = bzla_node_copy(forall, bzla_simplify_exp(forall, f));
    bzla_node_release(forall, f);
    gslv->forall_formula = g;
  }
}

/* instantiate each universal variable with the resp. fresh bit vector variable
 * and replace existential variables with the synthesized model.
 * 'model' maps existential variables to synthesized function models. */
static BzlaNode *
instantiate_formula(BzlaGroundSolvers *gslv,
                    BzlaPtrHashTable *model,
                    BzlaNodeMap *evar_map)
{
  assert(gslv);
  assert(!bzla_node_is_proxy(gslv->forall_formula));

  int32_t i;
  size_t j;
  Bzla *bzla;
  BzlaMemMgr *mm;
  BzlaNodePtrStack visit, args;
  BzlaNode *cur, *real_cur, *result, **e, *a, *fun;
  BzlaIntHashTable *mark;
  BzlaHashTableData *d;
  BzlaNodeMap *uvar_map, *skolem;
  BzlaPtrHashBucket *b;
  BzlaNodeMap *deps;
  SynthResult *synth_res;
  BzlaPtrHashTableIterator it;

  bzla     = gslv->forall;
  mm       = bzla->mm;
  mark     = bzla_hashint_map_new(mm);
  uvar_map = gslv->forall_uvars;
  deps     = gslv->forall_evar_deps;
  skolem   = gslv->forall_skolem;

  BZLA_INIT_STACK(mm, visit);
  BZLA_INIT_STACK(mm, args);
  BZLA_PUSH_STACK(visit, gslv->forall_formula);
  while (!BZLA_EMPTY_STACK(visit))
  {
    cur      = BZLA_POP_STACK(visit);
    real_cur = bzla_node_real_addr(cur);

    d = bzla_hashint_map_get(mark, real_cur->id);
    if (!d)
    {
      if (bzla_node_is_param(real_cur)
          && bzla_node_param_is_exists_var(real_cur) && model
          && (b = bzla_hashptr_table_get(model, real_cur)))
      {
        synth_res = b->data.as_ptr;
        assert(synth_res->value);
        BZLA_PUSH_STACK(visit, bzla_node_cond_invert(cur, synth_res->value));
        continue;
      }
      bzla_hashint_map_add(mark, real_cur->id);
      BZLA_PUSH_STACK(visit, cur);
      for (i = real_cur->arity - 1; i >= 0; i--)
        BZLA_PUSH_STACK(visit, real_cur->e[i]);
    }
    else if (d->as_ptr == 0)
    {
      assert(real_cur->arity <= BZLA_COUNT_STACK(args));
      args.top -= real_cur->arity;
      e = args.top;

      if (bzla_node_is_uf(real_cur))
      {
        if (model && (b = bzla_hashptr_table_get(model, real_cur)))
        {
          synth_res = b->data.as_ptr;
          result    = bzla_node_copy(bzla, synth_res->value);
        }
        else
          result = bzla_node_copy(bzla, real_cur);
      }
      else if (real_cur->arity == 0)
      {
        /* instantiate universal vars with fresh bv vars in 'uvar_map' */
        if (bzla_node_is_param(real_cur))
        {
          if (bzla_node_param_is_forall_var(real_cur))
          {
            result = bzla_nodemap_mapped(uvar_map, real_cur);
            assert(result);
            result = bzla_node_copy(bzla, result);
          }
          else
          {
            assert(bzla_node_param_is_exists_var(real_cur));
            /* exististential vars will be substituted while
             * traversing down */
            assert(!model || !bzla_hashptr_table_get(model, real_cur));
            /* no model -> substitute with skolem constant */
            fun = bzla_nodemap_mapped(skolem, real_cur);
            assert(fun);
            if ((a = bzla_nodemap_mapped(deps, real_cur)))
            {
              a      = instantiate_args(bzla, a, uvar_map);
              result = bzla_exp_apply(bzla, fun, a);
              bzla_node_release(bzla, a);
            }
            else
              result = bzla_node_copy(bzla, fun);
            bzla_nodemap_map(evar_map, real_cur, result);
          }
        }
        else
          result = bzla_node_copy(bzla, real_cur);
      }
      else if (bzla_node_is_bv_slice(real_cur))
      {
        result = bzla_exp_bv_slice(bzla,
                                   e[0],
                                   bzla_node_bv_slice_get_upper(real_cur),
                                   bzla_node_bv_slice_get_lower(real_cur));
      }
      /* universal variable got substituted by var in 'uvar_map' */
      else if (bzla_node_is_forall(real_cur) || bzla_node_is_exists(real_cur))
        result = bzla_node_copy(bzla, e[1]);
      else
        result = bzla_exp_create(bzla, real_cur->kind, e, real_cur->arity);

      for (i = 0; i < real_cur->arity; i++) bzla_node_release(bzla, e[i]);

      d->as_ptr = bzla_node_copy(bzla, result);
    PUSH_RESULT:
      BZLA_PUSH_STACK(args, bzla_node_cond_invert(cur, result));
    }
    else
    {
      assert(d->as_ptr);
      result = bzla_node_copy(bzla, d->as_ptr);
      goto PUSH_RESULT;
    }
  }
  assert(BZLA_COUNT_STACK(args) == 1);
  result = BZLA_POP_STACK(args);

  BZLA_RELEASE_STACK(visit);
  BZLA_RELEASE_STACK(args);

  /* map existential var to resp. substituted term (needed for getting
   * the value for the counterexamples) */
  if (model)
  {
    bzla_iter_hashptr_init(&it, model);
    while (bzla_iter_hashptr_has_next(&it))
    {
      synth_res = it.bucket->data.as_ptr;
      cur       = bzla_iter_hashptr_next(&it);

      a = synth_res->value;
      d = bzla_hashint_map_get(mark, bzla_node_real_addr(a)->id);
      assert(d);
      bzla_nodemap_map(evar_map, cur, bzla_node_cond_invert(a, d->as_ptr));
    }
  }

  for (j = 0; j < mark->size; j++)
  {
    if (!mark->keys[j]) continue;
    assert(mark->data[j].as_ptr);
    bzla_node_release(bzla, mark->data[j].as_ptr);
  }
  bzla_hashint_map_delete(mark);

  assert(!bzla_node_real_addr(result)->quantifier_below);
  assert(!bzla_node_real_addr(result)->parameterized);
  return result;
}

static void
build_input_output_values_quant_inst(BzlaGroundSolvers *gslv,
                                     BzlaNode *uvar,
                                     BzlaBitVectorTuplePtrStack *value_in,
                                     BzlaBitVectorPtrStack *value_out)
{
  uint32_t i, pos, uvar_pos;
  BzlaPtrHashTableIterator it;
  BzlaNodeMapIterator nit;
  BzlaBitVector *out;
  BzlaBitVectorTuple *in, *uvar_tup, *evar_tup;
  BzlaMemMgr *mm;
  Bzla *bzla;

  bzla = gslv->forall;
  mm   = bzla->mm;

  uvar_pos = 0;
  bzla_iter_nodemap_init(&nit, gslv->forall_uvars);
  while (bzla_iter_nodemap_has_next(&nit))
  {
    if (uvar == bzla_iter_nodemap_next(&nit)) break;
    uvar_pos++;
  }

  bzla_iter_hashptr_init(&it, gslv->forall_ces);
  while (bzla_iter_hashptr_has_next(&it))
  {
    evar_tup = it.bucket->data.as_ptr;
    uvar_tup = bzla_iter_hashptr_next(&it);

    in = bzla_bv_new_tuple(mm, uvar_tup->arity + evar_tup->arity);

    pos = 0;
    for (i = 0; i < uvar_tup->arity; i++)
      bzla_bv_add_to_tuple(mm, in, uvar_tup->bv[i], pos++);
    for (i = 0; i < evar_tup->arity; i++)
      bzla_bv_add_to_tuple(mm, in, evar_tup->bv[i], pos++);

    out = uvar_tup->bv[uvar_pos];
    BZLA_PUSH_STACK(*value_in, in);
    BZLA_PUSH_STACK(*value_out, bzla_bv_copy(mm, out));
  }
  assert(BZLA_COUNT_STACK(*value_in) == BZLA_COUNT_STACK(*value_out));
}

static BzlaNode *
build_quant_inst_refinement(BzlaGroundSolvers *gslv, BzlaNodeMap *map)
{
  uint32_t j, arity;
  int32_t i;
  BzlaNodePtrStack visit, args, params;
  BzlaIntHashTable *mark;
  BzlaArgsIterator ait;
  BzlaNode *cur, *real_cur, **e, *result, *a, *evar;
  BzlaMemMgr *mm;
  BzlaHashTableData *d;
  BzlaNodeMap *deps;
  Bzla *bzla;
  BzlaSortId sort;

  bzla = gslv->exists;
  mm   = bzla->mm;
  mark = bzla_hashint_map_new(mm);
  deps = gslv->forall_evar_deps;

  BZLA_INIT_STACK(mm, params);
  BZLA_INIT_STACK(mm, visit);
  BZLA_INIT_STACK(mm, args);

  BZLA_PUSH_STACK(visit, gslv->forall_formula);
  while (!BZLA_EMPTY_STACK(visit))
  {
    cur      = BZLA_POP_STACK(visit);
    real_cur = bzla_node_real_addr(cur);

    d = bzla_hashint_map_get(mark, real_cur->id);
    if (!d)
    {
      if (bzla_node_is_param(real_cur))
      {
        if (bzla_node_param_is_forall_var(real_cur))
        {
          result = bzla_nodemap_mapped(map, real_cur);
          assert(result);
          BZLA_PUSH_STACK(visit, bzla_node_cond_invert(cur, result));
          continue;
        }
      }

      (void) bzla_hashint_map_add(mark, real_cur->id);
      BZLA_PUSH_STACK(visit, cur);
      for (i = real_cur->arity - 1; i >= 0; i--)
        BZLA_PUSH_STACK(visit, real_cur->e[i]);

      if (bzla_node_is_param(real_cur)
          && bzla_node_param_is_exists_var(real_cur)
          && (a = bzla_nodemap_mapped(deps, real_cur)))
      {
        bzla_iter_args_init(&ait, a);
        while (bzla_iter_args_has_next(&ait))
          BZLA_PUSH_STACK(params, bzla_iter_args_next(&ait));
        while (!BZLA_EMPTY_STACK(params))
          BZLA_PUSH_STACK(visit, BZLA_POP_STACK(params));
      }
    }
    else if (!d->as_ptr)
    {
      assert(!bzla_node_is_param(real_cur)
             || !bzla_node_param_is_forall_var(real_cur));
      assert(!bzla_node_is_bv_var(real_cur));
      assert(!bzla_node_is_uf(real_cur));

      args.top -= real_cur->arity;
      e = args.top;

      if (bzla_node_is_bv_const(real_cur))
      {
        result = bzla_exp_bv_const(bzla, bzla_node_bv_const_get_bits(real_cur));
      }
      else if (bzla_node_is_param(real_cur))
      {
        assert(!bzla_node_param_is_forall_var(real_cur));
        if (bzla_node_param_is_exists_var(real_cur))
        {
          evar = bzla_nodemap_mapped(gslv->forall_evars, real_cur);
          a    = bzla_nodemap_mapped(deps, real_cur);
          if (a)
          {
            arity = bzla_node_args_get_arity(a->bzla, a);
            assert(BZLA_COUNT_STACK(args) >= arity);
            args.top -= arity;
            e      = args.top;
            result = bzla_exp_apply_n(bzla, evar, e, arity);

            for (j = 0; j < arity; j++) bzla_node_release(bzla, e[j]);
          }
          else
            result = bzla_node_copy(bzla, evar);
        }
        else
        {
          sort   = bzla_sort_bv(bzla,
                              bzla_node_bv_get_width(real_cur->bzla, real_cur));
          result = bzla_exp_param(bzla, sort, 0);
          bzla_sort_release(bzla, sort);
        }
      }
      else if (bzla_node_is_bv_slice(real_cur))
      {
        result = bzla_exp_bv_slice(bzla,
                                   e[0],
                                   bzla_node_bv_slice_get_upper(real_cur),
                                   bzla_node_bv_slice_get_lower(real_cur));
      }
      /* universal/existential vars get substituted */
      else if (bzla_node_is_quantifier(real_cur))
      {
        assert(!bzla_node_is_param(e[0]));
        result = bzla_node_copy(bzla, e[1]);
      }
      else
        result = bzla_exp_create(bzla, real_cur->kind, e, real_cur->arity);

      for (i = 0; i < real_cur->arity; i++) bzla_node_release(bzla, e[i]);

      d->as_ptr = bzla_node_copy(bzla, result);

    PUSH_RESULT:
      BZLA_PUSH_STACK(args, bzla_node_cond_invert(cur, result));
    }
    else
    {
      assert(d->as_ptr);
      result = bzla_node_copy(bzla, d->as_ptr);
      goto PUSH_RESULT;
    }
  }
  assert(BZLA_COUNT_STACK(args) == 1);
  result = BZLA_POP_STACK(args);

  BZLA_RELEASE_STACK(visit);
  BZLA_RELEASE_STACK(args);
  BZLA_RELEASE_STACK(params);

  for (j = 0; j < mark->size; j++)
  {
    if (!mark->keys[j]) continue;
    assert(mark->data[j].as_ptr);
    bzla_node_release(bzla, mark->data[j].as_ptr);
  }
  bzla_hashint_map_delete(mark);

  return result;
}

static void
synthesize_quant_inst(BzlaGroundSolvers *gslv)
{
  uint32_t pos, num_synth = 0;
  BzlaNode *cur, *uvar, *result = 0, *uconst, *c;
  BzlaNode *a, *prev_synth;
  BzlaMemMgr *mm;
  BzlaIntHashTable *value_in_map, *input_cache;
  BzlaNodePtrStack constraints, inputs, consts;
  BzlaBitVectorTuplePtrStack value_in;
  const BzlaBitVector *bv;
  BzlaBitVectorPtrStack value_out;
  BzlaNodeMapIterator it, iit;
  BzlaHashTableData *d;
  BzlaNodeMap *map, *prev_qi;
  Bzla *f_solver, *e_solver;
  BzlaArgsIterator ait;

  f_solver     = gslv->forall;
  e_solver     = gslv->exists;
  mm           = f_solver->mm;
  map          = bzla_nodemap_new(f_solver);
  value_in_map = bzla_hashint_map_new(mm);

  BZLA_INIT_STACK(mm, value_in);
  BZLA_INIT_STACK(mm, value_out);
  BZLA_INIT_STACK(mm, inputs);
  BZLA_INIT_STACK(mm, consts);
  BZLA_INIT_STACK(mm, constraints);
  BZLA_PUSH_STACK(constraints, bzla_node_invert(gslv->forall_formula));

  prev_qi             = gslv->exists_cur_qi;
  gslv->exists_cur_qi = bzla_nodemap_new(e_solver);

  /* value_in_map maps variables to the position in the assignment vector
   * value_in[k] */
  pos = 0;
  bzla_iter_nodemap_init(&it, gslv->forall_uvars);
  bzla_iter_nodemap_queue(&it, gslv->forall_evars);
  while (bzla_iter_nodemap_has_next(&it))
  {
    cur = bzla_iter_nodemap_next(&it);
    bzla_hashint_map_add(value_in_map, cur->id)->as_int = pos++;
  }

  bzla_iter_nodemap_init(&it, gslv->forall_uvars);
  while (bzla_iter_nodemap_has_next(&it))
  {
    uconst = it.it.bucket->data.as_ptr;
    uvar   = bzla_iter_nodemap_next(&it);
    a      = bzla_nodemap_mapped(gslv->forall_uvar_deps, uvar);

    input_cache = bzla_hashint_table_new(mm);
    BZLA_RESET_STACK(inputs);
    if (a)
    {
      bzla_iter_args_init(&ait, a);
      while (bzla_iter_args_has_next(&ait))
      {
        cur = bzla_iter_args_next(&ait);
        assert(bzla_node_is_regular(cur));
        assert(!bzla_hashint_table_contains(input_cache, cur->id));
        bzla_hashint_table_add(input_cache, cur->id);
        BZLA_PUSH_STACK(inputs, cur);
      }
    }
    bzla_iter_nodemap_init(&iit, gslv->forall_evars);
    while (bzla_iter_nodemap_has_next(&iit))
    {
      cur = bzla_iter_nodemap_next(&iit);
      if (!bzla_nodemap_mapped(gslv->forall_evar_deps, cur)
          && !bzla_hashint_table_contains(input_cache, cur->id))
      {
        bzla_hashint_table_add(input_cache, cur->id);
        BZLA_PUSH_STACK(inputs, cur);
      }
    }
    bzla_hashint_table_delete(input_cache);

    result = 0;
    if (!BZLA_EMPTY_STACK(inputs))
    {
      build_input_output_values_quant_inst(gslv, uvar, &value_in, &value_out);
      d   = bzla_hashint_map_get(value_in_map, uvar->id);
      pos = d->as_int;
      /* 'uvar' is a special placeholder for constraint evaluation */
      d->as_int = -1;

      prev_synth = 0;
      if (prev_qi) prev_synth = bzla_nodemap_mapped(prev_qi, uvar);

      result = bzla_synthesize_term(f_solver,
                                    inputs.start,
                                    BZLA_COUNT_STACK(inputs),
                                    value_in.start,
                                    value_out.start,
                                    BZLA_COUNT_STACK(value_in),
                                    value_in_map,
                                    constraints.start,
                                    BZLA_COUNT_STACK(constraints),
                                    consts.start,
                                    BZLA_COUNT_STACK(consts),
                                    10000,
                                    0,
                                    prev_synth);

      while (!BZLA_EMPTY_STACK(value_in))
        bzla_bv_free_tuple(mm, BZLA_POP_STACK(value_in));
      while (!BZLA_EMPTY_STACK(value_out))
        bzla_bv_free(mm, BZLA_POP_STACK(value_out));
      /* restore position of 'uvar' */
      d->as_int = pos;
    }

    if (result)
    {
      bzla_nodemap_map(map, uvar, result);
      bzla_node_release(f_solver, result);
      num_synth++;
      bzla_nodemap_map(gslv->exists_cur_qi, uvar, result);
    }
    else
    {
      bv = bzla_model_get_bv(f_solver, bzla_simplify_exp(f_solver, uconst));
      c  = bzla_exp_bv_const(f_solver, (BzlaBitVector *) bv);
      bzla_nodemap_map(map, uvar, c);
      bzla_node_release(f_solver, c);
    }
  }

  if (num_synth > 0)
  {
    /* map UFs */
#if 0
      bzla_iter_nodemap_init (&it, gslv->exists_ufs);
      while (bzla_iter_nodemap_has_next (&it))
        {
          var_fs = it.it.bucket->data.as_ptr;
          var_es = bzla_iter_nodemap_next (&it);
          bzla_nodemap_map (map, var_fs, var_es);
        }
#endif
    result = build_quant_inst_refinement(gslv, map);
    bzla_assert_exp(e_solver, result);
    bzla_node_release(e_solver, result);
  }

  while (!BZLA_EMPTY_STACK(value_in))
    bzla_bv_free_tuple(mm, BZLA_POP_STACK(value_in));
  while (!BZLA_EMPTY_STACK(value_out))
    bzla_bv_free(mm, BZLA_POP_STACK(value_out));

  BZLA_RELEASE_STACK(inputs);
  BZLA_RELEASE_STACK(consts);
  BZLA_RELEASE_STACK(constraints);

  if (prev_qi) bzla_nodemap_delete(prev_qi);
  bzla_hashint_map_delete(value_in_map);
  bzla_nodemap_delete(map);
  BZLA_RELEASE_STACK(value_in);
  BZLA_RELEASE_STACK(value_out);
}

static BzlaSolverResult
find_model(BzlaGroundSolvers *gslv, bool skip_exists)
{
  bool opt_synth_qi;
  double start;
  BzlaSolverResult res          = BZLA_RESULT_UNKNOWN, r;
  BzlaNode *g                   = 0;
  BzlaPtrHashTable *synth_model = 0;
  BzlaNodeMap *evar_map         = 0;
  FlatModel *flat_model         = 0;

  evar_map     = bzla_nodemap_new(gslv->forall);
  opt_synth_qi = bzla_opt_get(gslv->forall, BZLA_OPT_QUANT_SYNTH_QI) == 1;

  /* exists solver does not have any constraints, so it does not make much
   * sense to initialize every variable by zero and ask if the model
   * is correct. */
  if (!skip_exists)
  {
    /* query exists solver */
    start = time_stamp();
    r     = bzla_check_sat(gslv->exists, -1, -1);
    gslv->statistics.time.e_solver += time_stamp() - start;

    if (r == BZLA_RESULT_UNSAT) /* formula is UNSAT */
    {
      res = BZLA_RESULT_UNSAT;
      goto DONE;
    }
    /* solver terminated due to termination callback */
    else if (r == BZLA_RESULT_UNKNOWN)
    {
      assert(bzla_terminate(gslv->exists));
      goto DONE;
    }

    start      = time_stamp();
    flat_model = flat_model_generate(gslv);

    /* synthesize model based on 'partial_model' */
    synth_model = synthesize_model(gslv, flat_model);
    flat_model_free(flat_model);

    /* save currently synthesized model */
    delete_model(gslv);
    gslv->forall_synth_model = synth_model;
    gslv->statistics.time.synth += time_stamp() - start;
  }

  start = time_stamp();
  if (evar_map)
  {
    bzla_nodemap_delete(evar_map);
    evar_map = bzla_nodemap_new(gslv->forall);
  }
  g = instantiate_formula(gslv, synth_model, evar_map);
  gslv->statistics.time.checkinst += time_stamp() - start;

  /* if there are no universal variables in the formula, we have a simple
   * ground formula */
  if (gslv->forall_uvars->table->count == 0)
  {
    assert(skip_exists);
    bzla_assert_exp(gslv->forall, g);
    start = time_stamp();
    res   = bzla_check_sat(gslv->forall, -1, -1);
    gslv->statistics.time.f_solver += time_stamp() - start;
    goto DONE;
  }

  bzla_assume_exp(gslv->forall, bzla_node_invert(g));

  /* query forall solver */
  start = time_stamp();
  r     = bzla_check_sat(gslv->forall, -1, -1);
  update_formula(gslv);
  assert(!bzla_node_is_proxy(gslv->forall_formula));
  gslv->statistics.time.f_solver += time_stamp() - start;

  if (r == BZLA_RESULT_UNSAT) /* formula is SAT */
  {
    res = BZLA_RESULT_SAT;
    goto DONE;
  }
  /* solver terminated due to termination callback */
  else if (r == BZLA_RESULT_UNKNOWN)
  {
    assert(bzla_terminate(gslv->forall));
    goto DONE;
  }

  /* if refinement fails, we got a counter-example that we already got in
   * a previous call. in this case we produce a model using all refinements */
  start = time_stamp();
  refine_exists_solver(gslv, evar_map);
  gslv->statistics.time.refine += time_stamp() - start;

  if (opt_synth_qi)
  {
    start = time_stamp();
    synthesize_quant_inst(gslv);
    gslv->statistics.time.qinst += time_stamp() - start;
  }

DONE:
  bzla_nodemap_delete(evar_map);
  if (g) bzla_node_release(gslv->forall, g);
  return res;
}

#ifdef BZLA_HAVE_PTHREADS
static void *
thread_work(void *state)
{
  BzlaSolverResult res = BZLA_RESULT_UNKNOWN;
  BzlaGroundSolvers *gslv;
  bool skip_exists = true;

  gslv = state;
  while (res == BZLA_RESULT_UNKNOWN && !*gslv->found_result)
  {
    res         = find_model(gslv, skip_exists);
    skip_exists = false;
    gslv->statistics.stats.refinements++;
  }
  pthread_mutex_lock(gslv->found_result_mutex);
  if (!*gslv->found_result)
  {
    BZLA_MSG(gslv->exists->msg,
             1,
             "found solution in %.2f seconds",
             bzla_util_process_time_thread());
    *gslv->found_result = true;
  }
  assert(*gslv->found_result || res == BZLA_RESULT_UNKNOWN);
  pthread_mutex_unlock(gslv->found_result_mutex);
  gslv->result = res;
  return NULL;
}

static int32_t
thread_terminate(void *state)
{
  bool found_result = *((bool *) state);
  return found_result;
}

static BzlaSolverResult
run_parallel(BzlaGroundSolvers *gslv, BzlaGroundSolvers *dgslv)
{
  bool thread_found_result;
  pthread_mutex_t thread_result_mutex = PTHREAD_MUTEX_INITIALIZER;
  BzlaSolverResult res;
  pthread_t thread_orig, thread_dual;

  thread_found_result   = false;
  g_measure_thread_time = true;
  bzla_set_term(gslv->forall, thread_terminate, &thread_found_result);
  bzla_set_term(gslv->exists, thread_terminate, &thread_found_result);
  bzla_set_term(dgslv->forall, thread_terminate, &thread_found_result);
  bzla_set_term(dgslv->exists, thread_terminate, &thread_found_result);

  gslv->found_result        = &thread_found_result;
  gslv->found_result_mutex  = &thread_result_mutex;
  dgslv->found_result       = &thread_found_result;
  dgslv->found_result_mutex = &thread_result_mutex;

  pthread_create(&thread_orig, 0, thread_work, gslv);
  pthread_create(&thread_dual, 0, thread_work, dgslv);
  pthread_join(thread_orig, 0);
  pthread_join(thread_dual, 0);

  if (gslv->result != BZLA_RESULT_UNKNOWN)
  {
    res = gslv->result;
  }
  else
  {
    assert(dgslv->result != BZLA_RESULT_UNKNOWN);
    if (dgslv->result == BZLA_RESULT_SAT)
    {
      BZLA_MSG(dgslv->forall->msg,
               1,
               "dual solver result: sat, original formula: unsat");
      res = BZLA_RESULT_UNSAT;
    }
    else
    {
      assert(dgslv->result == BZLA_RESULT_UNSAT);
      res = BZLA_RESULT_SAT;
      BZLA_MSG(dgslv->forall->msg,
               1,
               "dual solver result: unsat, original formula: sat");
    }
  }
  return res;
}
#endif

static BzlaNode *
simplify(Bzla *bzla, BzlaNode *g)
{
  BzlaNode *tmp;

  if (bzla_opt_get(bzla, BZLA_OPT_QUANT_MINISCOPE))
  {
    tmp = bzla_miniscope_node(bzla, g);
    bzla_node_release(bzla, g);
    g = tmp;
  }
  if (bzla_opt_get(bzla, BZLA_OPT_QUANT_DER))
  {
    tmp = bzla_der_node(bzla, g);
    bzla_node_release(bzla, g);
    g = tmp;
  }
  if (bzla_opt_get(bzla, BZLA_OPT_QUANT_CER))
  {
    tmp = bzla_cer_node(bzla, g);
    bzla_node_release(bzla, g);
    g = tmp;
  }
  return g;
}

static BzlaSolverResult
sat_quant_solver(BzlaQuantSolver *slv)
{
  assert(slv);
  assert(slv->kind == BZLA_QUANT_SOLVER_KIND);
  assert(slv->bzla);
  assert(slv->bzla->slv == (BzlaSolver *) slv);

  bool skip_exists = true;
  BzlaSolverResult res;
  BzlaNode *g;

  BZLA_ABORT(bzla_opt_get(slv->bzla, BZLA_OPT_INCREMENTAL),
             "incremental mode not supported for BV");

  /* make sure that all quantifiers occur in the correct phase */
  g = bzla_normalize_quantifiers(slv->bzla);
  g = simplify(slv->bzla, g);

  slv->gslv = setup_solvers(slv, g, false, "forall", "exists");
  bzla_node_release(slv->bzla, g);

#ifdef BZLA_HAVE_PTHREADS
  bool opt_dual_solver;
  opt_dual_solver = bzla_opt_get(slv->bzla, BZLA_OPT_QUANT_DUAL_SOLVER) == 1;

  /* disable dual solver if UFs are present in the formula */
  if (slv->gslv->exists_ufs->table->count > 0) opt_dual_solver = false;

  if (opt_dual_solver)
  {
    slv->dgslv = setup_solvers(
        slv, slv->gslv->forall_formula, true, "dual_forall", "dual_exists");
    res = run_parallel(slv->gslv, slv->dgslv);
  }
  else
#endif
  {
    while (true)
    {
      res = find_model(slv->gslv, skip_exists);
      if (res != BZLA_RESULT_UNKNOWN) break;
      skip_exists = false;
    }
    slv->gslv->result = res;
  }
  slv->bzla->last_sat_result = res;
  return res;
}

static void
generate_model_quant_solver(BzlaQuantSolver *slv,
                            bool model_for_all_nodes,
                            bool reset)
{
  assert(slv);
  assert(slv->kind == BZLA_QUANT_SOLVER_KIND);
  assert(slv->bzla);
  assert(slv->bzla->slv == (BzlaSolver *) slv);

  (void) model_for_all_nodes;
  (void) reset;

  bzla_model_init_bv(slv->bzla, &slv->bzla->bv_model);
  bzla_model_init_fun(slv->bzla, &slv->bzla->fun_model);

  // TODO (ma): not supported for now (needs more general model infrastructure)
}

static void
print_stats_quant_solver(BzlaQuantSolver *slv)
{
  assert(slv);
  assert(slv->kind == BZLA_QUANT_SOLVER_KIND);
  assert(slv->bzla);
  assert(slv->bzla->slv == (BzlaSolver *) slv);
  assert(slv->gslv);

  BZLA_MSG(slv->bzla->msg, 1, "");
  BZLA_MSG(slv->bzla->msg,
           1,
           "cegqi solver refinements: %u",
           slv->gslv->statistics.stats.refinements);
  BZLA_MSG(slv->bzla->msg,
           1,
           "cegqi solver failed refinements: %u",
           slv->gslv->statistics.stats.failed_refinements);
  if (slv->gslv->result == BZLA_RESULT_SAT
      || slv->gslv->result == BZLA_RESULT_UNKNOWN)
  {
    BZLA_MSG(slv->bzla->msg,
             1,
             "model synthesized const: %u (%u)",
             slv->gslv->statistics.stats.synthesize_model_const,
             slv->gslv->statistics.stats.synthesize_const);
    BZLA_MSG(slv->bzla->msg,
             1,
             "model synthesized term: %u (%u)",
             slv->gslv->statistics.stats.synthesize_model_term,
             slv->gslv->statistics.stats.synthesize_term);
    BZLA_MSG(slv->bzla->msg,
             1,
             "model synthesized none: %u (%u)",
             slv->gslv->statistics.stats.synthesize_model_none,
             slv->gslv->statistics.stats.synthesize_none);
  }
  if (bzla_opt_get(slv->bzla, BZLA_OPT_QUANT_DUAL_SOLVER))
  {
    assert(slv->dgslv);
    BZLA_MSG(slv->bzla->msg,
             1,
             "cegqi dual solver refinements: %u",
             slv->dgslv->statistics.stats.refinements);
    BZLA_MSG(slv->bzla->msg,
             1,
             "cegqi dual solver failed refinements: %u",
             slv->dgslv->statistics.stats.failed_refinements);
    if (slv->dgslv->result == BZLA_RESULT_SAT
        || slv->dgslv->result == BZLA_RESULT_UNKNOWN)
    {
      BZLA_MSG(slv->bzla->msg,
               1,
               "dual model synthesized const: %u (%u)",
               slv->dgslv->statistics.stats.synthesize_model_const,
               slv->dgslv->statistics.stats.synthesize_const);
      BZLA_MSG(slv->bzla->msg,
               1,
               "dual model synthesized term: %u (%u)",
               slv->dgslv->statistics.stats.synthesize_model_term,
               slv->dgslv->statistics.stats.synthesize_term);
      BZLA_MSG(slv->bzla->msg,
               1,
               "dual model synthesized none: %u (%u)",
               slv->dgslv->statistics.stats.synthesize_model_none,
               slv->dgslv->statistics.stats.synthesize_none);
    }
  }
}

static void
print_time_stats_quant_solver(BzlaQuantSolver *slv)
{
  assert(slv);
  assert(slv->kind == BZLA_QUANT_SOLVER_KIND);
  assert(slv->bzla);
  assert(slv->bzla->slv == (BzlaSolver *) slv);

  BZLA_MSG(slv->bzla->msg,
           1,
           "%.2f seconds exists solver",
           slv->gslv->statistics.time.e_solver);
  BZLA_MSG(slv->bzla->msg,
           1,
           "%.2f seconds forall solver",
           slv->gslv->statistics.time.f_solver);
  BZLA_MSG(slv->bzla->msg,
           1,
           "%.2f seconds synthesizing functions",
           slv->gslv->statistics.time.synth);
  BZLA_MSG(slv->bzla->msg,
           1,
           "%.2f seconds add refinement",
           slv->gslv->statistics.time.refine);
  BZLA_MSG(slv->bzla->msg,
           1,
           "%.2f seconds quantifier instantiation",
           slv->gslv->statistics.time.qinst);
  BZLA_MSG(slv->bzla->msg,
           1,
           "%.2f seconds check instantiation",
           slv->gslv->statistics.time.checkinst);
  if (bzla_opt_get(slv->bzla, BZLA_OPT_QUANT_DUAL_SOLVER))
  {
    assert(slv->dgslv);
    BZLA_MSG(slv->bzla->msg,
             1,
             "%.2f seconds dual exists solver",
             slv->dgslv->statistics.time.e_solver);
    BZLA_MSG(slv->bzla->msg,
             1,
             "%.2f seconds dual forall solver",
             slv->dgslv->statistics.time.f_solver);
    BZLA_MSG(slv->bzla->msg,
             1,
             "%.2f seconds dual synthesizing functions",
             slv->dgslv->statistics.time.synth);
    BZLA_MSG(slv->bzla->msg,
             1,
             "%.2f seconds dual add refinement",
             slv->dgslv->statistics.time.refine);
    BZLA_MSG(slv->bzla->msg,
             1,
             "%.2f seconds dual quantifier instantiation",
             slv->dgslv->statistics.time.qinst);
    BZLA_MSG(slv->bzla->msg,
             1,
             "%.2f seconds dual check instantiation",
             slv->dgslv->statistics.time.checkinst);
  }
}

/* Note: Models are always printed in SMT2 format. */
static void
print_model_quant_solver(BzlaQuantSolver *slv, const char *format, FILE *file)
{
  BzlaNode *cur;
  BzlaPtrHashTableIterator it;
  SynthResult *synth_res;

  if (slv->gslv->result == BZLA_RESULT_SAT)
  {
    if (slv->gslv->forall_synth_model)
    {
      format = "smt2"; /* Force SMT2 models */
      fprintf(
          file, "(model%s", slv->gslv->forall_synth_model->count ? "\n" : " ");

      bzla_iter_hashptr_init(&it, slv->gslv->forall_synth_model);
      while (bzla_iter_hashptr_has_next(&it))
      {
        synth_res = it.bucket->data.as_ptr;
        cur       = bzla_iter_hashptr_next(&it);
        assert(bzla_node_is_uf(cur) || bzla_node_param_is_exists_var(cur));
        bzla_print_node_model(
            slv->gslv->forall, cur, synth_res->value, format, file);
      }

      fprintf(file, ")\n");
    }
    else
    {
      // TODO: first check model call is already UNSAT -> any value to
      // existential vars makes formula SAT
    }
  }
  else
  {
    assert(slv->dgslv);
    assert(slv->dgslv->result == BZLA_RESULT_UNSAT);
    assert(bzla_opt_get(slv->bzla, BZLA_OPT_QUANT_DUAL_SOLVER));
    fprintf(file, "cannot generate model, disable --quant:dual\n");
  }
}

BzlaSolver *
bzla_new_quantifier_solver(Bzla *bzla)
{
  assert(bzla);

  BzlaQuantSolver *slv;

  BZLA_CNEW(bzla->mm, slv);

  slv->kind      = BZLA_QUANT_SOLVER_KIND;
  slv->bzla      = bzla;
  slv->api.clone = (BzlaSolverClone) clone_quant_solver;
  slv->api.delet = (BzlaSolverDelete) delete_quant_solver;
  slv->api.sat   = (BzlaSolverSat) sat_quant_solver;
  slv->api.generate_model =
      (BzlaSolverGenerateModel) generate_model_quant_solver;
  slv->api.print_stats = (BzlaSolverPrintStats) print_stats_quant_solver;
  slv->api.print_time_stats =
      (BzlaSolverPrintTimeStats) print_time_stats_quant_solver;
  slv->api.print_model = (BzlaSolverPrintModel) print_model_quant_solver;

  BZLA_MSG(bzla->msg, 1, "enabled quant engine");

  return (BzlaSolver *) slv;
}
