//
// Created by Kunal Sheth on 3/30/25.
//

#include "auditor.h"

#include "dreal/solver/context_impl.h"

#include <limits>
#include <ostream>
#include <sstream>
#include <unordered_set>
#include <dreal/symbolic/prefix_printer.h>
#include <dreal/symbolic/symbolic_formula_cell.h>

#include <fmt/format.h>

#include "dreal/solver/filter_assertion.h"
#include "dreal/util/assert.h"
#include "dreal/util/logging.h"
#include "dreal/version.h"

namespace dreal
{
    std::ofstream literal_log("/tmp/dreal_audit_sat_literal_log.txt");
    std::set<int> literal_log_clause;

    void sat_log_label_clause(const std::string& s) {
        if (!DREAL_EXPERIMENTAL_SAT_AUDIT_ENABLED) return;
        literal_log << s << std::endl;
    }

    void sat_log_literal(const int lit, const std::optional<Variable>& def) {
        if (!DREAL_EXPERIMENTAL_SAT_AUDIT_ENABLED) return;
        DREAL_ASSERT(lit != 0);

        static std::set<Variable> seen_literals;
        if (def && !seen_literals.count(*def)) {
            literal_log << "def:\t" << ::abs(lit) << " := " << *def << std::endl;
            seen_literals.emplace(*def);
        }

        literal_log_clause.insert(lit);
    }

    void sat_log_literal0() {
        if (!DREAL_EXPERIMENTAL_SAT_AUDIT_ENABLED) return;
        for (int v : literal_log_clause) literal_log << v << ' ';
        literal_log << '0' << std::endl;
        literal_log_clause.clear();
    }

    void theory_audit_literals(const PredicateAbstractor& pa, /*copy*/ std::vector<Formula> lits, const std::optional<Box>& box) {
        // the operands passed here are expensive to produce, so really shouldn't be called unnecessarily.
        DREAL_ASSERT(DREAL_EXPERIMENTAL_THEORY_AUDIT_ENABLED);

        for (auto& l : lits) {
            if (is_variable(l)) {
                // f = b
                const auto iter = pa.var_to_formula_map().find(get_variable(l));
                if (iter != pa.var_to_formula_map().end()) l = iter->second;
                // else ...  the variable is probably, actually, a BOOL such as `DigitalPDController_0__omega_sensor_bit0__t0ad`
            }
            else {
                // f = ¬b
                DREAL_ASSERT(is_negation(l) && is_variable(get_operand(l)));
                const auto iter = pa.var_to_formula_map().find(get_variable(get_operand(l)));
                if (iter != pa.var_to_formula_map().end()) l = !iter->second;
                // else ...  the variable is probably, actually, a BOOL such as `DigitalPDController_0__omega_sensor_bit0__t0ad`
            }
        }
        theory_audit_formula(make_disjunction(lits), box);
    }

    void theory_audit_formula(const Formula& formula, const std::optional<Box>& box) {
        // the operands passed here are expensive to produce, so really shouldn't be called unnecessarily.
        DREAL_ASSERT(DREAL_EXPERIMENTAL_THEORY_AUDIT_ENABLED);

        const auto& vs = formula.GetFreeVariables();

        std::string lemma_comment;
        {
            std::ostringstream s;
            s << ";\t∀ ";
            for (const auto& v : vs) {
                s << v;
                if (box) s << "∈" << (*box)[v];
                s << ", ";
            }
            s << "\n;\t\t" << formula;
            lemma_comment = s.str();
        }

        std::cout << lemma_comment << std::endl; // todo: gate with macro or flag and use spdlog

        static int audit_no = 0;
        std::ofstream myfile;
        myfile.open("/tmp/dreal_audit/lemma" + std::to_string(audit_no++) + ".smt2");

        myfile << "\n" << lemma_comment << "\n\n";

        for (const auto& v : vs) {
            myfile << "(declare-const " << v << " Real)\n";
            if (box) {
                const auto& boxv = (*box)[v]; // todo: confirm inclusive v.s. exclusive
                if (isfinite(boxv.lb())) myfile << "(assert " << ToPrefix(boxv.lb() <= v) << " )\n";
                if (isfinite(boxv.ub())) myfile << "(assert " << ToPrefix(v <= boxv.ub()) << " )\n";
            }
        }

        myfile << "(assert " << ToPrefix(!formula) << " )\n";

        myfile << "(check-sat)(get-model)(exit)" << std::endl;
        myfile.flush();
        myfile.close();
    }
}
