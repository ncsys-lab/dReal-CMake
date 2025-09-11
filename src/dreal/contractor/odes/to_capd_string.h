//
// Created by Kunal Sheth on 9/2/25.
//
#ifdef DREAL4_CMAKE_TO_CAPD_STRING_H
#error to_capd_string.h may only be included once.
#endif

#ifndef DREAL4_CMAKE_TO_CAPD_STRING_H
#define DREAL4_CMAKE_TO_CAPD_STRING_H
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/assert.h"
#include "dreal/util/exception.h"

namespace dreal
{
    using std::string;

    bool starts_with(std::string const& s, std::string const& prefix) {
        if (!s.compare(0, prefix.size(), prefix)) { return true; }
        return false;
    }

    bool ends_with(std::string const& s, std::string const& ending) {
        if (s.length() >= ending.length()) {
            return (0 == s.compare(s.length() - ending.length(), ending.length(), ending));
        }
        return false;
    }

    template <typename T>
    std::string join(T const& container, std::string const& sep) {
        auto it = begin(container);
        auto end_it = end(container);
        std::ostringstream ss;
        ss << *(it++);
        for (; it != end_it; it++) {
            ss << sep << *it;
        }
        return ss.str();
    }

    string to_capd_string(const double value) {
        string name = std::to_string(value);
        if (name.find('e') != string::npos || name.find('E') != string::npos) {
            // Scientific Notation
            std::ostringstream ss;
            double const r = stod(name);
            ss << std::setprecision(16) << std::fixed << r;
            name = ss.str();
        }
        if (starts_with(name, "-")) {
            name = "(" + name + ")";
        }
        return name;
    }

    string to_capd_string(const Expression& e) {
        std::ostringstream ss;
        switch (e.get_kind()) {
        case ExpressionKind::Constant:
            return to_capd_string(get_constant_value(e));
        case ExpressionKind::RealConstant:
            return to_capd_string(to_real_constant(e)->get_value()); // todo: this is inaccurate?
        case ExpressionKind::Var:
            return get_variable(e).get_name();
        case ExpressionKind::Add: {
            const auto* const add = to_addition(e);
            ss << '(' << to_capd_string(add->get_constant());
            for (const auto& [expr, coeff] : add->get_expr_to_coeff_map()) {
                ss << '+' + to_capd_string(coeff) + '*' + to_capd_string(expr);
            }
            ss << ')';
            break;
        }
        case ExpressionKind::Mul: {
            const auto* const mul = to_multiplication(e);
            ss << '(' << to_capd_string(mul->get_constant());
            for (const auto& [base, exp] : mul->get_base_to_exponent_map()) {
                ss << "*(" << to_capd_string(base) << '^' << to_capd_string(exp) << ')';
            }
            ss << ')';
            break;
        }
        case ExpressionKind::Div: {
            ss << '(' << to_capd_string(get_first_argument(e)) << '/' << to_capd_string(get_second_argument(e)) << ')';
            break;
        }
        case ExpressionKind::Log:
            ss << "log(" << to_capd_string(get_argument(e)) << ")";
            break;
        case ExpressionKind::Abs:
            ss << "sqrt(sqr(" << to_capd_string(get_argument(e)) << "))";
            break;
        case ExpressionKind::Exp:
            ss << "exp(" << to_capd_string(get_argument(e)) << ")";
            break;
        case ExpressionKind::Sqrt:
            ss << "sqrt(" << to_capd_string(get_argument(e)) << ")";
            break;
        case ExpressionKind::Pow:
            ss << '(' << to_capd_string(get_first_argument(e)) << '^' << to_capd_string(get_second_argument(e)) << ')';
            break;
        case ExpressionKind::Sin:
            ss << "sin(" << to_capd_string(get_argument(e)) << ")";
            break;
        case ExpressionKind::Cos:
            ss << "cos(" << to_capd_string(get_argument(e)) << ")";
            break;
        case ExpressionKind::Tan: {
            // CAPD does not support tan. Use tan(x) = sin(x) / cos(x)
            const auto x = to_capd_string(get_argument(e));
            ss << "(sin(" << x << ")/cos(" << x << "))";
            break;
        }
        case ExpressionKind::Asin:
            ss << "asin(" << to_capd_string(get_argument(e)) << ")";
            break;
        case ExpressionKind::Acos:
            ss << "acos(" << to_capd_string(get_argument(e)) << ")";
            break;
        case ExpressionKind::Atan:
            ss << "atan(" << to_capd_string(get_argument(e)) << ")";
            break;
        case ExpressionKind::Atan2: {
            // CAPD does not support atan2. Use atan2(y, x) = 2atan([sqrt(x^2 + y^2) - x] / y)
            const auto y = to_capd_string(get_first_argument(e));
            const auto x = to_capd_string(get_second_argument(e));
            ss << "(2 * atan((sqrt(sqr(" << x << ") + sqr(" << y << ")) - " << x << ") / " << y << "))";
            break;
        }
        case ExpressionKind::Sinh: {
            // CAPD does not support sinh. Use sinh(x) = (exp(x) - exp(-x)) / 2
            const auto x = to_capd_string(get_argument(e));
            ss << "((exp(" << x << ") - exp(-" << x << ")) / 2)";
            break;
        }
        case ExpressionKind::Cosh: {
            // CAPD does not support cosh. Use cosh(x) = (exp(x) + exp(-x)) / 2
            const auto x = to_capd_string(get_argument(e));
            ss << "((exp(" << x << ") + exp(-" << x << ")) / 2)";
            break;
        }
        case ExpressionKind::Tanh: {
            // CAPD does not support tanh. Use tanh(x) = (exp(x) - exp(-x)) / (exp(x) + exp(-x))
            const auto x = to_capd_string(get_argument(e));
            ss << "((exp(" << x << ") - exp(-" << x << ")) / (exp(" << x << ") + exp(-" << x << ")))";
            break;
        }
        case ExpressionKind::Min:
            ss << "min(" << to_capd_string(get_first_argument(e)) << ',' << to_capd_string(get_second_argument(e)) << ")";
            break;
        case ExpressionKind::Max:
            ss << "max(" << to_capd_string(get_first_argument(e)) << ',' << to_capd_string(get_second_argument(e)) << ")";
            break;
        case ExpressionKind::IfThenElse:
            throw DREAL_RUNTIME_ERROR("Not implemented.");
        case ExpressionKind::NaN:
            throw std::runtime_error("NaN is detected while visiting an expression.");
        case ExpressionKind::UninterpretedFunction:
            throw DREAL_RUNTIME_ERROR("Not implemented.");
        default:
            throw std::runtime_error("Should not be reachable.");
        }
        return ss.str();
    }

    string to_capd_string(const Formula& f, bool is_inverted = false) {
        std::ostringstream ss;

        switch (f.get_kind()) {
        case FormulaKind::False:
            throw DREAL_RUNTIME_ERROR("Not implemented.");
        case FormulaKind::True:
            throw DREAL_RUNTIME_ERROR("Not implemented.");
        case FormulaKind::Var:
            throw DREAL_RUNTIME_ERROR("Not implemented.");

        case FormulaKind::Neq:
            is_inverted = !is_inverted;
            [[fallthrough]];
        case FormulaKind::Eq: {
            if (is_inverted) { ss << "(0=0)"; }
            else {
                ss << '(' << to_capd_string(get_lhs_expression(f)) << '=' << to_capd_string(get_rhs_expression(f)) << ')';
            }
            break;
        }

        case FormulaKind::Gt: {
            string op = is_inverted ? "<=" : ">";
            ss << "(" << to_capd_string(get_lhs_expression(f)) << op << to_capd_string(get_rhs_expression(f)) << ")";
        }
        case FormulaKind::Geq: {
            string op = is_inverted ? "<" : ">=";
            ss << "(" << to_capd_string(get_lhs_expression(f)) << op << to_capd_string(get_rhs_expression(f)) << ")";
        }
        case FormulaKind::Lt: {
            string op = is_inverted ? ">=" : "<";
            ss << "(" << to_capd_string(get_lhs_expression(f)) << op << to_capd_string(get_rhs_expression(f)) << ")";
        }
        case FormulaKind::Leq: {
            string op = is_inverted ? ">" : "<=";
            ss << "(" << to_capd_string(get_lhs_expression(f)) << op << to_capd_string(get_rhs_expression(f)) << ")";
        }

        case FormulaKind::And:
            throw DREAL_RUNTIME_ERROR("Not implemented.");
        case FormulaKind::Or:
            throw DREAL_RUNTIME_ERROR("Not implemented.");
        case FormulaKind::Not:
            return to_capd_string(get_operand(f), !is_inverted);
        case FormulaKind::Forall:
            throw DREAL_RUNTIME_ERROR("Not implemented.");
        case FormulaKind::ForallT:
            throw DREAL_RUNTIME_ERROR("Not implemented.");
        case FormulaKind::Integral:
            throw DREAL_RUNTIME_ERROR("Not implemented.");
        default:
            throw std::runtime_error("Should not be reachable.");
        }
        return ss.str();
    }
}

#endif //DREAL4_CMAKE_TO_CAPD_STRING_H
