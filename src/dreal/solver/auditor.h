//
// Created by Kunal Sheth on 3/30/25.
//

#ifndef AUDITOR_H
#define AUDITOR_H

#include <dreal/util/box.h>
#include <dreal/util/predicate_abstractor.h>
#include <optional>
#include <dreal/util/pattern_matching/substitutions_map.h>

namespace dreal
{

    void sat_log_label_clause(const std::string& label);
    void sat_log_literal(int lit, const std::optional<Variable>& def = {});
    void sat_log_literal0();

    void theory_audit_literals(
        const std::string& label,
        const PredicateAbstractor& pa, /*copy*/ std::vector<Formula> lits,
        const std::optional<Box>& box
    );
    void theory_audit_formula(const std::string& label, const Formula& formula, const std::optional<Box>& box);

    void pm_dump_all(
        const std::vector<Formula>& base_conflict,
        const std::vector<std::pair<std::vector<Formula>, substitutions_map>>& all_matches
    );
}

#endif //AUDITOR_H
