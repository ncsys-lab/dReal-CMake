# Chapter: References (algorithm → paper map)

Source: [`reference.rst.txt`](../../../ibex-docs/_sources/reference.rst.txt) ·
`reference.html`. IBEX's bibliography. Useful as an **algorithm → primary source**
map when an audit item needs the method's actual definition (the `.rst` chapters
often defer details to these).

| IBEX feature | Paper | Where in this tree |
|---|---|---|
| X-Taylor linearization (`LinearizerXTaylor`) | Araya, Trombettoni, Neveu, *A Contractor Based on Convex Interval Taylor*, CPAIOR 2012 | [contractor](contractor.md), [linear node](../classes/linear/LinearizerXTaylor.md) |
| Affine linearization (`LinearizerAffine2`, plugin) | Ninin & Messine, *Automatic Linear Reformulation … Affine Arithmetic*, ISMP 2009 | [contractor](contractor.md), [AUDIT D](../AUDIT.md) |
| Constructive interval disjunction (CID / 3BCID) | Trombettoni & Chabert, *Constructive Interval Disjunction*, CP 2007 | [Ctc3BCid](../classes/contractors/Ctc3BCid.md) |
| ACID (adaptive CID) | Neveu, Trombettoni, Araya, *Adaptive Constructive Interval Disjunction*, Constraints 2015 | [CtcAcid](../classes/contractors/CtcAcid.md), **AUDIT A** |
| Inner regions / inner arithmetic | Araya et al. 2014; Chabert & Beldiceanu, *Sweeping with Continuous Domains*, CP 2010 | [interval](interval.md) |
| Lsmear bisection | Araya & Neveu, *Lsmear*, JOGO 2018 | [strategy](strategy.md) |
| Separators / set inversion | Jaulin & Desrochers 2014; Jaulin & Walter, *SIVIA*, Automatica 1993; Jaulin, *set-valued CSP*, Computing 2012 | [separator](separator.md), [set](set.md) |
| HC4 / hull-box consistency | Benhamou et al., *Revising Hull and Box Consistency*, ICLP 1999 | [contractor](contractor.md) |
| Interval Newton | Hansen & Sengupta 1980; Moore 1966; Neumaier 1990 | [CtcNewton](../classes/contractors/CtcNewton.md) |

(The `.rst` page is marked "under construction"; entries above are the ones an
audit reader actually needs. Full table in the source.)
