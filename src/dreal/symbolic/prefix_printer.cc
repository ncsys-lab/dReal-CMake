/*
   Copyright 2017 Toyota Research Institute

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#include "dreal/symbolic/prefix_printer.h"

#include <limits>
#include <sstream>
#include <stdexcept>
#include <dreal/util/rounded_format.h>
#include <dreal/util/rounding.h>

#include "dreal/symbolic/odes/symbolic_odes_cell.h"

using std::ostream;
using std::ostringstream;
using std::runtime_error;
using std::string;

namespace dreal {

namespace {
ostream& print_constant(ostream& os, double c) {
  // Decimal formatting is correct only under FE_TONEAREST; route the doubles
  // through the token-gated format_double so the mode is compile-time proven.
  const NearestRoundingScope g;
  const NearestRounding nr{g.token()};
  if (c >= 0) {
    return format_double(os, c, nr);
  } else {
    os << "(- ";
    format_double(os, -c, nr);
    return os << ")";
  }
}

}  // namespace

PrefixPrinter::PrefixPrinter(ostream& os)
    : os_{os}, old_precision_{os.precision()} {
  // See
  // https://stackoverflow.com/questions/554063/how-do-i-print-a-double-value-with-full-precision-using-cout#comment40126260_554134.
  os_.precision(std::numeric_limits<double>::max_digits10 + 2);
}

PrefixPrinter::~PrefixPrinter() { os_.precision(old_precision_); }

ostream& PrefixPrinter::Print(const Expression& e) {
  return VisitExpression<ostream&>(this, e);
}

ostream& PrefixPrinter::Print(const Formula& f) {
  return VisitFormula<ostream&>(this, f);
}

ostream& PrefixPrinter::VisitVariable(const Expression& e) {
  return os_ << get_variable(e);
}

ostream& PrefixPrinter::VisitConstant(const Expression& e) {
  return print_constant(os_, get_constant_value(e));
}

ostream& PrefixPrinter::VisitRealConstant(const Expression& e) {
  NearestRoundingScope g;
  const double mid{get_lb_of_real_constant(e) / 2.0 +
                   get_ub_of_real_constant(e) / 2.0};
  return print_constant(os_, mid);
}

ostream& PrefixPrinter::VisitUnaryFunction(const std::string& name,
                                           const Expression& e) {
  os_ << "(" << name << " ";
  Print(get_argument(e));
  return os_ << ")";
}

ostream& PrefixPrinter::VisitBinaryFunction(const std::string& name,
                                            const Expression& e) {
  os_ << "(" << name << " ";
  Print(get_first_argument(e));
  os_ << " ";
  Print(get_second_argument(e));
  return os_ << ")";
}

ostream& PrefixPrinter::VisitAddition(const Expression& e) {
  const double constant{get_constant_in_addition(e)};
  os_ << "(+";
  if (constant != 0.0) {
    os_ << " ";
    print_constant(os_, constant);
  }
  for (const auto& p : get_expr_to_coeff_map_in_addition(e)) {
    const Expression& e_i{p.first};
    const double c_i{p.second};
    os_ << " ";
    if (c_i == 1.0) {
      Print(e_i);
    } else {
      os_ << "(* ";
      print_constant(os_, c_i);
      os_ << " ";
      Print(e_i);
      os_ << ")";
    }
  }
  return os_ << ")";
}

ostream& PrefixPrinter::VisitMultiplication(const Expression& e) {
  const double constant{get_constant_in_multiplication(e)};
  os_ << "(*";
  if (constant != 1.0) {
    os_ << " ";
    print_constant(os_, constant);
  }
  for (const auto& p : get_base_to_exponent_map_in_multiplication(e)) {
    const Expression& b_i{p.first};
    const Expression& e_i{p.second};
    os_ << " ";
    if (is_one(e_i)) {
      Print(b_i);
    } else {
      os_ << "(^ ";
      Print(b_i);
      os_ << " ";
      Print(e_i);
      os_ << ")";
    }
  }
  return os_ << ")";
}

ostream& PrefixPrinter::VisitDivision(const Expression& e) {
  return VisitBinaryFunction("/", e);
}

ostream& PrefixPrinter::VisitLog(const Expression& e) {
  return VisitUnaryFunction("log", e);
}

ostream& PrefixPrinter::VisitAbs(const Expression& e) {
  return VisitUnaryFunction("abs", e);
}

ostream& PrefixPrinter::VisitExp(const Expression& e) {
  return VisitUnaryFunction("exp", e);
}

ostream& PrefixPrinter::VisitSqrt(const Expression& e) {
  return VisitUnaryFunction("sqrt", e);
}

ostream& PrefixPrinter::VisitPow(const Expression& e) {
  return VisitBinaryFunction("^", e);
}

ostream& PrefixPrinter::VisitSin(const Expression& e) {
  return VisitUnaryFunction("sin", e);
}

ostream& PrefixPrinter::VisitCos(const Expression& e) {
  return VisitUnaryFunction("cos", e);
}

ostream& PrefixPrinter::VisitTan(const Expression& e) {
  return VisitUnaryFunction("tan", e);
}

ostream& PrefixPrinter::VisitAsin(const Expression& e) {
  return VisitUnaryFunction("asin", e);
}

ostream& PrefixPrinter::VisitAcos(const Expression& e) {
  return VisitUnaryFunction("acos", e);
}

ostream& PrefixPrinter::VisitAtan(const Expression& e) {
  return VisitUnaryFunction("atan", e);
}

ostream& PrefixPrinter::VisitAtan2(const Expression& e) {
  return VisitBinaryFunction("atan2", e);
}

ostream& PrefixPrinter::VisitSinh(const Expression& e) {
  return VisitUnaryFunction("sinh", e);
}

ostream& PrefixPrinter::VisitCosh(const Expression& e) {
  return VisitUnaryFunction("cosh", e);
}

ostream& PrefixPrinter::VisitTanh(const Expression& e) {
  return VisitUnaryFunction("tanh", e);
}

ostream& PrefixPrinter::VisitMin(const Expression& e) {
  return VisitBinaryFunction("min", e);
}

ostream& PrefixPrinter::VisitMax(const Expression& e) {
  return VisitBinaryFunction("max", e);
}

ostream& PrefixPrinter::VisitIfThenElse(const Expression& e) {
  os_ << "(ite ";
  Print(get_conditional_formula(e));
  os_ << " ";
  Print(get_then_expression(e));
  os_ << " ";
  Print(get_else_expression(e));
  return os_ << ")";
}

ostream& PrefixPrinter::VisitUninterpretedFunction(const Expression&) {
  throw runtime_error("Not implemented.");
}

ostream& PrefixPrinter::VisitFalse(const Formula&) { return os_ << "false"; }

ostream& PrefixPrinter::VisitTrue(const Formula&) { return os_ << "true"; }

ostream& PrefixPrinter::VisitVariable(const Formula& f) {
  return os_ << get_variable(f);
}

ostream& PrefixPrinter::VisitEqualTo(const Formula& f) {
  os_ << "(= ";
  Print(get_lhs_expression(f));
  os_ << " ";
  Print(get_rhs_expression(f));
  return os_ << ")";
}

ostream& PrefixPrinter::VisitNotEqualTo(const Formula& f) {
  os_ << "(not (= ";
  Print(get_lhs_expression(f));
  os_ << " ";
  Print(get_rhs_expression(f));
  return os_ << "))";
}

ostream& PrefixPrinter::VisitGreaterThan(const Formula& f) {
  os_ << "(> ";
  Print(get_lhs_expression(f));
  os_ << " ";
  Print(get_rhs_expression(f));
  return os_ << ")";
}

ostream& PrefixPrinter::VisitGreaterThanOrEqualTo(const Formula& f) {
  os_ << "(>= ";
  Print(get_lhs_expression(f));
  os_ << " ";
  Print(get_rhs_expression(f));
  return os_ << ")";
}

ostream& PrefixPrinter::VisitLessThan(const Formula& f) {
  os_ << "(< ";
  Print(get_lhs_expression(f));
  os_ << " ";
  Print(get_rhs_expression(f));
  return os_ << ")";
}

ostream& PrefixPrinter::VisitLessThanOrEqualTo(const Formula& f) {
  os_ << "(<= ";
  Print(get_lhs_expression(f));
  os_ << " ";
  Print(get_rhs_expression(f));
  return os_ << ")";
}

ostream& PrefixPrinter::VisitConjunction(const Formula& f) {
  os_ << "(and";
  for (const auto& f_i : get_operands(f)) {
    os_ << " ";
    Print(f_i);
  }
  return os_ << ")";
}

ostream& PrefixPrinter::VisitDisjunction(const Formula& f) {
  os_ << "(or";
  for (const auto& f_i : get_operands(f)) {
    os_ << " ";
    Print(f_i);
  }
  return os_ << ")";
}

ostream& PrefixPrinter::VisitNegation(const Formula& f) {
  os_ << "(not ";
  Print(get_operand(f));
  return os_ << ")";
}

ostream& PrefixPrinter::VisitForall(const Formula& f) {
  // SMT-LIB2 prefix: (forall ((v Real) ...) body). The quantifier domain is folded into the
  // body (the parser rebuilds `forall(vars, imply(domain, matrix))`), so printing each binder
  // with its bare sort and unbounded range round-trips: an unbounded binder contributes a
  // `True` domain, leaving the stored body — which already carries any domain implication —
  // unchanged.
  const auto* const fc = to_forall(f);
  os_ << "(forall (";
  for (const Variable& v : fc->get_quantified_variables()) {
    os_ << '(' << v << ' ';
    switch (v.get_type()) {
      case Variable::Type::CONTINUOUS: os_ << "Real"; break;
      case Variable::Type::INTEGER:
      case Variable::Type::BINARY:     os_ << "Int"; break;
      case Variable::Type::BOOLEAN:    os_ << "Bool"; break;
    }
    os_ << ')';
  }
  os_ << ") ";
  Print(fc->get_quantified_formula());
  return os_ << ')';
}

ostream& PrefixPrinter::VisitForallT(const Formula& f) {
  const auto* const fc = to_forallT(f);

  // keep semantics compatible with dReal3
  auto flow_id = fc->get_flow()->name;
  const std::string prefix = "flow_";
  if (flow_id.rfind(prefix, 0) == 0) flow_id.erase(0, prefix.size());

  os_ << "(forall_t " << flow_id << " [";
  Print(fc->get_lb());
  os_ << ' ';
  Print(fc->get_ub());
  os_ << "] ";
  Print(fc->get_bound_f());
  return os_ << ')';
}

ostream& PrefixPrinter::VisitIntegral(const Formula& f) {
  const auto *const ic = to_integral(f);
  os_ << "(= [";
  for (const auto& v : ic->get_vec_t()) {
    Print(v);
    os_ << ' ';
  }
  os_ << "] (integral ";
  Print(ic->get_time_0());
  os_ << ' ';
  Print(ic->get_time_t());
  os_ << " [";
  for (const auto& v : ic->get_vec_0()) {
    Print(v);
    os_ << ' ';
  }
  return os_ << "] " << ic->get_flow()->name << "))";
}

string ToPrefix(const Expression& e) {
  ostringstream oss;
  PrefixPrinter pp{oss};
  pp.Print(e);
  return oss.str();
}

string ToPrefix(const Formula& f) {
  ostringstream oss;
  PrefixPrinter pp{oss};
  pp.Print(f);
  return oss.str();
}

string ToPrefix(const OdeFlow& f) {
  ostringstream oss;
  PrefixPrinter pp{oss};
  /*
  (define-ode flow_1 (
                      (= d/dt[x] 1.0)
                      (= d/dt[P] (* (/ 1.0 (^ (* 2.0 3.14159265359) 0.5)) (exp (/ (- 0.0 (^ (- x 0.0) 2.0)) 2.0))))))
   */
  oss << "(define-ode " << f.name << " (\n";
  for (const auto& [ode_var, ode_rhs] : f.ode_list) {
    oss << "\t(= d/dt[" << ode_var << "] ";
    pp.Print(ode_rhs);
    oss << ")\n";
  }
  oss << "))";
  return oss.str();
}

}  // namespace dreal
