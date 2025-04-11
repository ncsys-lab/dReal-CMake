
#pragma once

// This file was automatically generated on 2025-04-10 21:28:55.533980 by Kunal in the `WORTH_IT_regression` python notebook.
//
// Notes:
//     len(df_overestimated) / len(df)  = 0.012
//     len(df_valid_over) / len(df)     = 0.073
//     len(df_valid_under) / len(df)    = 0.893
//     len(df_underestimated) / len(df) = 0.022
//
//     MINIMUM_WEIGHT_THRESHOLD = 0.001
//     r2_score(y_test, y_pred) = 0.8104209494854622
//     len(X_train) = 2946
//     len(X_test) / len(X_train) = 0.25

#define WORTH_IT_REGRESSION_MODEL(k) \
(0.0 * k.lemma_stats.sqrt_cntr \
 + 0.0 * k.assertions_stats.true_cntr \
 + 0.0 * k.assertions_stats.false_cntr \
 + 0.0 * k.lemma_stats.log_cntr \
 + 0.0 * k.lemma_stats.disjunction_cntr \
 + 0.0 * k.lemma_stats.false_cntr \
 + 0.0 * k.lemma_stats.forall_cntr \
 + 0.0 * k.lemma_stats.ifthenelse_cntr \
 + 0.0 * k.assertions_stats.notequalto_cntr \
 + 0.0 * k.lemma_stats.max_cntr \
 + 0.0 * k.assertions_stats.ifthenelse_cntr \
 + 0.0 * k.lemma_stats.true_cntr \
 + 0.0 * k.assertions_stats.uninterpretedfunction_cntr \
 + 0.0 * k.assertions_stats.min_cntr \
 + 0.0 * k.lemma_stats.uninterpretedfunction_cntr \
 + 0.0 * k.lemma_stats.min_cntr \
 + 0.0 * k.assertions_stats.disjunction_cntr \
 + 0.0 * k.lemma_stats.notequalto_cntr \
 + 0.0 * k.lemma_stats.exp_cntr \
 + 0.0 * k.assertions_stats.forall_cntr \
 + 0.0 * k.assertions_stats.max_cntr \
 + 0.0 * k.assertions_stats.exp_cntr \
 + 0.0 * k.assertions_stats.sqrt_cntr \
 + 0.0 * k.theory_checksat_ms \
 + 0.001217835225040596 * k.lemma_stats.addition_cntr \
 + -0.001962464451051034 * k.assertions_stats.realconstant_cntr \
 + -0.00793823796983405 * k.box_boolean_count \
 + -0.018145367826638337 * k.lemma_stats.variable_cntr \
 + 0.01829872636385954 * k.assertions_stats.negation_cntr \
 + -0.0229918912587667 * k.assertions_stats.variable_cntr \
 + -0.026376483705783937 * k.lemma_stats.negation_cntr \
 + 0.03017973232438398 * k.assertions_stats.addition_cntr \
 + -0.04107952767894013 * k.assertions_stats.trigonometry_cntr \
 + 0.05688306599609716 * k.assertions_stats.multiplication_cntr \
 + -0.060369550994358256 * k.assertions_stats.constant_cntr \
 + -0.07187529638117285 * k.box_continuous_count \
 + 0.07715080264816576 * k.assertions_stats.unique_variables \
 + 0.09232150621308066 * k.assertions_stats.pow_cntr \
 + 0.1026319444631465 * k.lemma_stats.pow_cntr \
 + -0.12459598630366009 * k.lemma_stats.constant_cntr \
 + 0.12927570458507232 * k.lemma_stats.trigonometry_cntr \
 + -0.1543638666996886 * k.lemma_stats.realconstant_cntr \
 + 0.15572623801096852 * k.lemma_stats.multiplication_cntr \
 + 0.15676812548357882 * k.lemma_stats.unique_variables \
 + -0.2653378609984196 * log2(1+k.box_boolean_count) \
 + -0.3545373118515116 * log2(1+k.box_continuous_count) \
 + -0.37408199290852107 * k.assertions_stats.division_cntr \
 + 0.5067092831074196 * k.lemma_stats.division_cntr \
 + -0.5157600045238886 * k.box_integer_count \
 + -1.0349214239587998 * k.lemma_stats.inequality_cntr \
 + 1.1187279976413107 * k.lemma_stats.abs_cntr \
 + 1.1380968359246078 * log2(1+k.theory_checksat_ms) \
 + 1.3026143754266153 * k.assertions_stats.log_cntr \
 + -1.451141643731522 * k.lemma_stats.equalto_cntr \
 + 1.6079717697721332 * k.assertions_stats.equalto_cntr \
 + -1.6377938127199587 * log2(1+k.box_integer_count) \
 + 1.6855709083177568 * k.assertions_stats.inequality_cntr \
 + 1.7110196321336344 * k.assertions_stats.abs_cntr \
 + 1.8035404953112604 * log2(1+k.assertions_size) \
 + -1.9480121854993735 * log2(1+k.lemma_size) \
 + -2.4860630676902487 * k.lemma_size \
 + 3.293542678090737 * k.assertions_size \
 + 3.666239777026109 * k.lemma_stats.conjunction_cntr \
 + -4.862501458432937 * k.assertions_stats.conjunction_cntr \
 + -8.42502822088598)

