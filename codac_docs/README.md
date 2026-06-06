# Codac documentation snapshot

Saved 2026-06-05 while investigating whether to backtrack from Codac v2 to v1 for broader ODE contractor support.

## Layout

- `v1/` — Codac v1.x manual pages and key source-file findings
- `v2/` — Codac v2.x manual pages and key source-file findings
- `analysis/` — comparison and recommendation

## Source URLs (snapshot)

| Local file | Origin URL |
|---|---|
| `v1/dynamic_contractors_index.md` | https://codac.io/v1/manual/05-dynamic-contractors/index.html |
| `v1/ctc_deriv.md` | https://codac.io/v1/manual/05-dynamic-contractors/01-ctc-deriv.html |
| `v1/ctc_eval.md` | https://codac.io/v1/manual/05-dynamic-contractors/02-ctc-eval.html |
| `v1/ctc_lohner.md` | https://codac.io/v1/manual/05-dynamic-contractors/03-ctc-lohner.html |
| `v1/ctc_picard.md` | https://codac.io/v1/manual/05-dynamic-contractors/04-ctc-picard.html (note: page incomplete upstream) |
| `v1/install_linux.md` | https://codac.io/v1/install/01-installation-full-linux.html |
| `v1/cmakelists_summary.md` | https://raw.githubusercontent.com/codac-team/codac/codac1/CMakeLists.txt |
| `v1/headers_and_api.md` | derived from `github.com/codac-team/codac/tree/codac1/src/core/contractors/dyn` |
| `v2/ctc_lohner.md` | https://codac.io/manual/contractors/dynamic/ctclohner.html |
| `v2/dynamic_contractors_index.md` | https://codac.io/manual/contractors/dynamic/ |
| `v2/local_header_inventory.md` | `gcc_build/codac-install/include/codac-core/codac2_*.h` |
| `v2/capd_extension.md` | https://codac.io/manual/extensions/capd/ + src |
| `analysis/v1_vs_v2_assessment.md` | new analysis |
| `analysis/alternatives_to_ctclohner.md` | new analysis |
| `analysis/codac_capd_extension_assessment.md` | new analysis |

## Quick conclusion

Backtracking to v1 does **not** close the performance gap with CAPD. v1 and v2 both implement the **same order-2 Lohner** algorithm. The only v1-exclusive ODE contractor is `CtcPicard`, which v1's own docs say is *worse* than Lohner once tubes are thin (our usual case after a few ICP iterations). Full reasoning in `analysis/v1_vs_v2_assessment.md`.
