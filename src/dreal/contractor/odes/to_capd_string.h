#pragma once
// Convert a dReal symbolic Expression into the string format that capd::IMap
// accepts, e.g. "(0+(-9.8)*(v^1))" for the RHS of dv/dt = -9.8.
//
// The full IMap string is "par:a,b;var:x,v;fun:f1,f2;" — the caller is
// responsible for the `par:`, `var:`, `fun:` framing; this header only
// converts the per-expression RHS strings.
//
// Forward-ported from the pre-Codac-migration version (deleted in commit
// 331b20703); API was updated for today's symbolic helpers (e.g.
// get_constant_in_addition vs to_addition(e)->get_constant()).
//
// Defined inline so the header can be included from any TU without
// multiple-definition errors.

#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

#include "dreal/symbolic/symbolic.h"
#include "dreal/util/assert.h"
#include "dreal/util/exception.h"
#include "dreal/util/rounding.h"

namespace dreal
{
    // Render a double as a decimal literal CAPD's IMap parser accepts, with
    // *enough digits to round-trip the exact double*. This is a soundness
    // obligation, not cosmetics: CAPD parses the literal into an outward-rounded
    // interval, so as long as the string carries all 17 significant digits
    // (max_digits10), that interval brackets the true double and the integrated
    // vector field is faithful. The previous std::to_string rendered only 6
    // fractional digits (sprintf %f), so 1/3 -> "0.333333" and CAPD integrated
    // 3*(1/3) as 0.999999 — a 1e-6-unfaithful field that false-unsats clock ODEs
    // whose terminal gate sits at the integration-window end (water/thermostat
    // automata; ode_soundness_repros/ws_taupin.smt2).
    //
    // FE_TONEAREST: a double->decimal conversion is correctly rounded only in
    // round-to-nearest; under FE_UPWARD the last digit can mis-round. Every call
    // site reaches here under a NearestRoundingScope (make_capd_ode_cache builds
    // the IMap strings), so this asserts the inherited mode rather than threading
    // a NearestRounding token (cf. format_double in util/rounded_format.h). The
    // assert compiles out under NDEBUG.
    inline std::string to_capd_string(double value) {
        DREAL_ASSERT_ROUNDING(FE_TONEAREST);
        std::ostringstream ss;
        ss << std::setprecision(std::numeric_limits<double>::max_digits10)
           << value;
        std::string name = ss.str();
        if (name.find('e') != std::string::npos || name.find('E') != std::string::npos) {
            // CAPD's IMap parser does not accept "1e-3" reliably across builds,
            // so re-render in fixed notation. setprecision(40) fractional digits
            // round-trips any double down to ~1e-23 in magnitude (trailing zeros
            // are harmless to the parser); below the default-format threshold
            // (|value| < 1e-4) this is the only branch that runs, so normal-
            // magnitude coefficients keep their short default-format string.
            std::ostringstream fx;
            fx << std::fixed << std::setprecision(40) << value;
            name = fx.str();
        }
        if (!name.empty() && name.front() == '-') {
            name = "(" + name + ")";
        }
        return name;
    }

    inline std::string to_capd_string(const Expression& e) {
        std::ostringstream ss;
        switch (e.get_kind()) {
        case ExpressionKind::Constant:
        case ExpressionKind::RealConstant:
            return to_capd_string(get_constant_value(e));

        case ExpressionKind::Var:
            return get_variable(e).get_name();

        case ExpressionKind::Add: {
            ss << '(' << to_capd_string(get_constant_in_addition(e));
            for (const auto& [term, coeff] : get_expr_to_coeff_map_in_addition(e)) {
                ss << '+' << to_capd_string(coeff) << '*' << to_capd_string(term);
            }
            ss << ')';
            break;
        }

        case ExpressionKind::Mul: {
            ss << '(' << to_capd_string(get_constant_in_multiplication(e));
            for (const auto& [base, exp] : get_base_to_exponent_map_in_multiplication(e)) {
                ss << "*(" << to_capd_string(base) << '^' << to_capd_string(exp) << ')';
            }
            ss << ')';
            break;
        }

        case ExpressionKind::Div:
            ss << '(' << to_capd_string(get_first_argument(e))
               << '/' << to_capd_string(get_second_argument(e)) << ')';
            break;

        case ExpressionKind::Log:
            ss << "log(" << to_capd_string(get_argument(e)) << ")";
            break;

        case ExpressionKind::Abs:
            // CAPD does not expose abs in IMap; emulate as sqrt(sqr(x)).
            ss << "sqrt(sqr(" << to_capd_string(get_argument(e)) << "))";
            break;

        case ExpressionKind::Exp:
            ss << "exp(" << to_capd_string(get_argument(e)) << ")";
            break;

        case ExpressionKind::Sqrt:
            ss << "sqrt(" << to_capd_string(get_argument(e)) << ")";
            break;

        case ExpressionKind::Pow:
            ss << '(' << to_capd_string(get_first_argument(e))
               << '^' << to_capd_string(get_second_argument(e)) << ')';
            break;

        case ExpressionKind::Sin:
            ss << "sin(" << to_capd_string(get_argument(e)) << ")";
            break;

        case ExpressionKind::Cos:
            ss << "cos(" << to_capd_string(get_argument(e)) << ")";
            break;

        case ExpressionKind::Tan: {
            // CAPD IMap does not support tan directly; use sin/cos.
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
            const auto y = to_capd_string(get_first_argument(e));
            const auto x = to_capd_string(get_second_argument(e));
            ss << "(2 * atan((sqrt(sqr(" << x << ") + sqr(" << y << ")) - "
               << x << ") / " << y << "))";
            break;
        }

        case ExpressionKind::Sinh: {
            const auto x = to_capd_string(get_argument(e));
            ss << "((exp(" << x << ") - exp(-" << x << ")) / 2)";
            break;
        }

        case ExpressionKind::Cosh: {
            const auto x = to_capd_string(get_argument(e));
            ss << "((exp(" << x << ") + exp(-" << x << ")) / 2)";
            break;
        }

        case ExpressionKind::Tanh: {
            const auto x = to_capd_string(get_argument(e));
            ss << "((exp(" << x << ") - exp(-" << x << "))"
               << " / (exp(" << x << ") + exp(-" << x << ")))";
            break;
        }

        case ExpressionKind::Min:
            ss << "min(" << to_capd_string(get_first_argument(e))
               << ',' << to_capd_string(get_second_argument(e)) << ")";
            break;

        case ExpressionKind::Max:
            ss << "max(" << to_capd_string(get_first_argument(e))
               << ',' << to_capd_string(get_second_argument(e)) << ")";
            break;

        case ExpressionKind::IfThenElse:
            throw DREAL_RUNTIME_ERROR("to_capd_string: IfThenElse not supported in ODE RHS");
        case ExpressionKind::NaN:
            throw std::runtime_error("to_capd_string: NaN encountered while visiting ODE RHS");
        case ExpressionKind::UninterpretedFunction:
            throw DREAL_RUNTIME_ERROR("to_capd_string: UninterpretedFunction not supported in ODE RHS");
        default:
            throw std::runtime_error("to_capd_string: unsupported ExpressionKind");
        }
        return ss.str();
    }
}
