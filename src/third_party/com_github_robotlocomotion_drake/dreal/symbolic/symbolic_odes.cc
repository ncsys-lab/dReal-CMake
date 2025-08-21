//
// Created by Kunal Sheth on 8/20/25.
//

#include "symbolic_odes.h"

#include "dreal/symbolic/symbolic_formula_cell.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>

#include "dreal/symbolic/hash.h"
#include "dreal/symbolic/symbolic_environment.h"
#include "dreal/symbolic/symbolic_expression.h"
#include "dreal/symbolic/symbolic_formula.h"
#include "dreal/symbolic/symbolic_variable.h"
#include "dreal/symbolic/symbolic_variables.h"
#include "dreal/symbolic/symbolic_odes.h"

namespace dreal
{
    class Term;
}

namespace dreal
{
    namespace drake
    {
        namespace symbolic
        {
            using std::all_of;
            using std::any_of;
            using std::equal;
            using std::hash;
            using std::lexicographical_compare;
            using std::ostream;
            using std::ostringstream;
            using std::runtime_error;
            using std::set;
            using std::vector;
            using std::string;

            FormulaForallT::FormulaForallT(const int flow, const Expression& lb, const Expression& ub, const Formula& f)
                : FormulaCell{
                      FormulaKind::ForallT,
                      hash_combine(f.get_hash(), flow, lb.get_hash(), ub.get_hash()),
                      hash_combine(f.get_al_hash(), flow, lb.get_al_hash(), ub.get_al_hash()),
                      lb.include_ite() || ub.include_ite() || f.include_ite(),
                      lb.GetVariables() + ub.GetVariables() // variables in f are bound, not free
                  },
                  mode_{flow},
                  lb_{lb},
                  ub_{ub},
                  f_{f} {
                // todo: type check?
            }

            bool FormulaForallT::EqualTo(const FormulaCell& f) const {
                // Formula::EqualTo guarantees the following assertion.
                assert(get_kind() == f.get_kind());
                const FormulaForallT& f_forallT{static_cast<const FormulaForallT&>(f)};
                return (
                    mode_ == f_forallT.mode_ &&
                    lb_.EqualTo(f_forallT.lb_) &&
                    ub_.EqualTo(f_forallT.ub_) &&
                    f_.EqualTo(f_forallT.f_)
                );
            }

            bool FormulaForallT::Less(const FormulaCell& f) const {
                // Formula::Less guarantees the following assertion.
                assert(get_kind() == f.get_kind());
                const FormulaForallT& f_forallT{static_cast<const FormulaForallT&>(f)};
                if (mode_ < f_forallT.mode_) return true;
                if (f_forallT.mode_ < mode_) return false;
                if (lb_ < f_forallT.lb_) return true;
                if (f_forallT.lb_ < lb_) return false;
                if (ub_ < f_forallT.ub_) return true;
                if (f_forallT.ub_ < ub_) return false;
                return this->f_.Less(f_forallT.f_);
            }

            bool FormulaForallT::Evaluate(const Environment&) const {
                throw runtime_error("not implemented yet");
            }

            Formula FormulaForallT::Substitute(const ExpressionSubstitution& expr_subst,
                                               const FormulaSubstitution& formula_subst) {
                // Quantified variables are already bound and should not be substituted by s.
                // We construct a new substitution new_s from s by removing the entries of
                // bound variables.
                throw runtime_error("not implemented yet"); // no easy way to do this? since the flow defines the bound variables...
            }

            ostream& FormulaForallT::Display(ostream& os) const {
                return os << "forallT(flow_" << mode_ << ", from t=" << lb_ << " to " << ub_ << ", " << f_ << ")";
            }

            Formula forallT(const int flow, const Expression& lb, const Expression& ub, const Formula& f) {
                return Formula{new FormulaForallT(flow, lb, ub, f)};
            }

            bool is_forallT(const Formula& f) { return is_forallT(*f.ptr_); }
            bool is_forallT(const FormulaCell& f) { return f.get_kind() == FormulaKind::ForallT; }

            const FormulaForallT* to_forallT(const FormulaCell* f_ptr) {
                assert(is_forall(*f_ptr));
                return static_cast<const FormulaForallT*>(f_ptr);
            }

            const FormulaForallT* to_forallT(const Formula& f) { return to_forallT(f.ptr_); }

            ////////////////////////////////////////////////////////////////////////////////

            size_t alpha_hash_vec(const vector<Expression>& vec) {
                size_t seed{};
                for (const auto& v : vec)
                    seed = hash_combine(seed, v.get_al_hash());
                return seed;
            }

            Variables extract_variables(const Expression& time_0, const Expression& time_t,
                                        const std::vector<Expression>& vec_0, const std::vector<Expression>& vec_t) {
                Variables ret{time_0.GetVariables()};
                ret.insert(time_t.GetVariables());
                for (const auto& v : vec_0)
                    ret.insert(v.GetVariables());
                for (const auto& v : vec_t)
                    ret.insert(v.GetVariables());
                return ret;
            }

            FormulaIntegral::FormulaIntegral(
                const Expression& time_0, const Expression& time_t,
                std::vector<Expression> vec_0, std::vector<Expression> vec_t,
                std::string flow_name)
                : FormulaCell{
                      FormulaKind::Integral,
                      hash_combine(
                          hash_value<vector<Expression>>{}(vec_0),
                          hash_value<vector<Expression>>{}(vec_t),
                          time_0, time_t, flow_name
                      ),
                      hash_combine(
                          alpha_hash_vec(vec_0),
                          alpha_hash_vec(vec_t),
                          time_0.get_al_hash(), time_t.get_al_hash(), flow_name
                      ),
                      false, extract_variables(time_0, time_t, vec_0, vec_t)
                  },
                  time_0_{time_0},
                  time_t_{time_t},
                  vec_0_{std::move(vec_0)},
                  vec_t_{std::move(vec_t)},
                  flow_name_{std::move(flow_name)} {
                assert(vec_0_.size() == vec_t_.size()); // todo: more checks?
            }

            bool FormulaIntegral::EqualTo(const FormulaCell& e) const {
                // Expression::EqualTo guarantees the following assertion.
                assert(get_kind() == e.get_kind());
                const FormulaIntegral& int_e{static_cast<const FormulaIntegral&>(e)};

                if (flow_name_ != int_e.flow_name_) return false;
                if (!time_0_.EqualTo(int_e.time_0_)) return false;
                if (!time_t_.EqualTo(int_e.time_t_)) return false;
                if (!equal( // checks length automatically
                    vec_0_.cbegin(), vec_0_.cend(), int_e.vec_0_.cbegin(), int_e.vec_0_.cend(),
                    [](const Expression& e1, const Expression& e2) { return e1.EqualTo(e2); }))
                    return false;
                if (!equal( // checks length automatically
                    vec_t_.cbegin(), vec_t_.cend(), int_e.vec_t_.cbegin(), int_e.vec_t_.cend(),
                    [](const Expression& e1, const Expression& e2) { return e1.EqualTo(e2); }))
                    return false;

                return true;
            }

            bool FormulaIntegral::Less(const FormulaCell& e) const {
                // Expression::Less guarantees the following assertion.
                assert(get_kind() == e.get_kind());
                const FormulaIntegral& int_e{static_cast<const FormulaIntegral&>(e)};

                if (flow_name_ < int_e.flow_name_) return true;
                if (int_e.flow_name_ < flow_name_) return false;
                if (time_0_ < int_e.time_0_) return true;
                if (int_e.time_0_ < time_0_) return false;
                if (time_t_ < int_e.time_t_) return true;
                if (int_e.time_t_ < time_t_) return false;
                if (vec_0_.size() < int_e.vec_0_.size()) return true;
                if (int_e.vec_0_.size() < vec_0_.size()) return false;
                if (vec_t_.size() < int_e.vec_t_.size()) return true;
                if (int_e.vec_t_.size() < vec_t_.size()) return false;
                for (size_t i = 0; i < vec_0_.size(); ++i) {
                    if (vec_0_[i] < int_e.vec_0_[i]) return true;
                    if (int_e.vec_0_[i] < vec_0_[i]) return false;
                }
                for (size_t i = 0; i < vec_t_.size(); ++i) {
                    if (vec_t_[i] < int_e.vec_t_[i]) return true;
                    if (int_e.vec_t_[i] < vec_t_[i]) return false;
                }
                return false;
            }

            bool FormulaIntegral::Evaluate(const Environment& env) const {
                throw runtime_error("Not yet implemented.");
            }

            Formula FormulaIntegral::Substitute(
                const ExpressionSubstitution& expr_subst,
                const FormulaSubstitution& formula_subst) {
                const auto time_0_subst{time_0_.Substitute(expr_subst, formula_subst)};
                const auto time_t_subst{time_t_.Substitute(expr_subst, formula_subst)};

                std::vector<Expression> vec_0_subst(vec_0_.size());
                std::vector<Expression> vec_t_subst(vec_t_.size());
                for (size_t i = 0; i < vec_0_.size(); ++i) vec_0_subst[i] = vec_0_[i].Substitute(expr_subst, formula_subst);
                for (size_t i = 0; i < vec_t_.size(); ++i) vec_t_subst[i] = vec_t_[i].Substitute(expr_subst, formula_subst);

                const bool equals = (
                    time_0_subst.EqualTo(time_0_) &&
                    time_t_subst.EqualTo(time_t_) &&
                    equal( // checks length automatically
                        vec_0_.cbegin(), vec_0_.cend(), vec_0_subst.cbegin(), vec_0_subst.cend(),
                        [](const Expression& e1, const Expression& e2) { return e1.EqualTo(e2); }) &&
                    equal( // checks length automatically
                        vec_t_.cbegin(), vec_t_.cend(), vec_t_subst.cbegin(), vec_t_subst.cend(),
                        [](const Expression& e1, const Expression& e2) { return e1.EqualTo(e2); })
                );

                if (!equals) return integral(time_0_subst, time_t_subst, vec_0_subst, vec_t_subst, flow_name_);
                else return GetFormula();
            }

            ostream& FormulaIntegral::Display(ostream& os) const {
                os << "(= [";
                for (size_t i = 0; i < vec_0_.size(); ++i) {
                    if (i > 0) os << ", ";
                    os << vec_0_[i];
                }
                os << "] (integral " << flow_name_ << ", from t=" << time_0_ << " to " << time_t_ << ", ";
                os << "[";
                for (size_t i = 0; i < vec_t_.size(); ++i) {
                    if (i > 0) os << ", ";
                    os << vec_t_[i];
                }
                os << "]";
                return os << "))";
            }

            Formula integral(const Expression& time_0, const Expression& time_t,
                             const std::vector<Expression>& vec_0, const std::vector<Expression>& vec_t,
                             const std::string& flow_name) {
                return Formula{new FormulaIntegral(time_0, time_t, vec_0, vec_t, flow_name)};
            }

            bool is_integral(const Formula& e) { return is_integral(*e.ptr_); }
            bool is_integral(const FormulaCell& c) { return c.get_kind() == FormulaKind::Integral; }

            const FormulaIntegral* to_integral(const FormulaCell* const expr_ptr) {
                assert(is_integral(*expr_ptr));
                return static_cast<const FormulaIntegral*>(expr_ptr);
            }

            const FormulaIntegral* to_integral(const Formula& e) { return to_integral(e.ptr_); }
        } // namespace symbolic
    } // namespace drake
} // namespace dreal
