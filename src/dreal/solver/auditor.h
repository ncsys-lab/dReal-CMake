//
// Created by Kunal Sheth on 3/30/25.
//

#ifndef AUDITOR_H
#define AUDITOR_H

#include <dreal/util/box.h>
#include <dreal/util/predicate_abstractor.h>
#include <optional>

namespace dreal
{

    void sat_log_label_clause(const std::string& label);
    void sat_log_literal(int lit, const std::optional<Variable>& def = {});
    void sat_log_literal0();

    void theory_audit_literals(
        const PredicateAbstractor& pa, /*copy*/ std::vector<Formula> lits,
        const std::optional<Box>& box
    );
    void theory_audit_formula(const Formula& formula, const std::optional<Box>& box);
}

#endif //AUDITOR_H
