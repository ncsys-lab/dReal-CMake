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

namespace dreal
{
    void audit(const Formula& formula, const std::optional<Box>& box) {
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
