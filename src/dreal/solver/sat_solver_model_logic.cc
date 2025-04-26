#include <dreal/version.h>

#include "dreal/solver/sat_solver.h"

namespace dreal {

int SatSolver::get_partial_model(std::vector<int> &model_is) {
  DREAL_ASSERT(model_is.size() == cadical->vars() + 1);

  // make sure not to mix `flip` and `val` calls... slows things down.
  // do all the "flipping" first, then call "val" in one sweep at the end.

  // MASK to prevent XOR issue...
  // e.g. if we have 1 2 0, with model -1 2 0
  // then flipping -1 to +1, subsequently makes 2 flippable to -2
  // however, this DOES NOT MEAN they both can be set to 0 / don't care.
  // need to mask away variables which were not flippable to begin with.
  for (int i = 1; i <= cadical->vars(); ++i) {
    model_is[i] = cadical->flippable(i); // mask
  }
  for (int i = 1; i <= cadical->vars(); ++i) {
    // try to zero-out non-decision variables first,
    // make the theory solver "focus" on decision variables to promote backtracking.
    if (cadical->is_decision(i)) continue;
    model_is[i] = model_is[i] && cadical->flip(i);
  }
  for (int i = 1; i <= cadical->vars(); ++i) {
    if (!cadical->is_decision(i)) continue;
    model_is[i] = model_is[i] && cadical->flip(i);
  }

  // UPDATE - don't bother, this folds into the "combinatorial" issue below...
  // I cannot trigger this in a unit test but it happens
  // sporadically on large problems...
  // a future flip "locks" a previous assignment, making it no longer flippable.
  // for (int i = 1; i <= cadical->vars(); ++i) {
  //   model_is[i] = model_is[i] && cadical->flippable(i);
  // }

  int num_omitted_literals = 0;
  for (int i = 1; i <= cadical->vars(); ++i) {
    if (model_is[i] /* was flippable and then flipped */) {
      num_omitted_literals++;
      model_is[i] = 0;
    } else {
      model_is[i] = cadical->val(i) > 0 ? +1 : -1;
    }
  }

  if (DREAL_EXPERIMENTAL_SAT_AUDIT_ENABLED) {
    // todo:
    //    so it turns out we can basically never actually calculate what is flippable and what is not.
    //    unless we were to go through the full O(2^N) combinatorial set of flips-and-not-flips
    //    the best we can do is give an underconstrained model...
    //    and then scrutinize any theory-SAT results with the full model.
    // for (int i = 1; i <= cadical->vars(); ++i) {
    //   if (partial_model[i] == 0) {
    //     cadical->assume(+i);
    //     for (int j = 1; j <= cadical->vars(); ++j) if (partial_model[j] != 0) cadical->assume(j * partial_model[j]);
    //     DREAL_ASSERT (cadical->solve() == CaDiCaL::SATISFIABLE);
    //     cadical->assume(-i);
    //     for (int j = 1; j <= cadical->vars(); ++j) if (partial_model[j] != 0) cadical->assume(j * partial_model[j]);
    //     DREAL_ASSERT (cadical->solve() == CaDiCaL::SATISFIABLE);
    //   }
    // }
  }

  return num_omitted_literals;
}
}
