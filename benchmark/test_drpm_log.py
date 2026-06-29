#!/usr/bin/env python3
"""Self-contained tests for drpm_log.py (run: python3 test_drpm_log.py).

The synthetic lines are constructed to match exactly what
`drpm_benchmark_log` in src/dreal/solver/context_impl.cc emits to std::cerr:

    out << ".\t";
    out << " S.FCM " << fcm;            // bool -> 0/1
    out << " S.ms " << sat_ms;          // double, cerr setprecision(3)
    out << " T.ms " << theory_ms;
    out << '\t';
    out << " L " << lemma_size;         // unsigned
    out << ' ' << mode;                 // 'M' (pattern-matched) or 'A' (added direct)
    out << "\t";
    out << " PM.ms " << pm_ms;
    out << " PM " << num_pm;            // unsigned
    #if CAV26_FILTER_SYMMETRIES        // off by default (version.h)
    out << '\t';
    out << " C26.npT " << ...; out << " C26.npL " << ...; out << " C26.np* " << ...;
    #endif
    out << '\n';

No pytest dependency (matches the harness's stdlib-only convention).
"""
import sys

import drpm_log

# CAV26 off (default build): mode 'M'
LINE_M = ".\t S.FCM 1 S.ms 0.123 T.ms 4.56\t L 42 M\t PM.ms 0.789 PM 3"
# mode 'A' (lemma over drpm_max_size, added direct without pattern matching)
LINE_A = ".\t S.FCM 0 S.ms 12.3 T.ms 1.23e+03\t L 970 A\t PM.ms 0 PM 0"
# CAV26 build: three extra fields appended
LINE_C26 = ".\t S.FCM 1 S.ms 0.5 T.ms 9\t L 7 M\t PM.ms 0.1 PM 2\t C26.npT 5 C26.npL 2 C26.np* 7"
# A non-drpm stderr line (DREAL_LOG / other cerr output) must be skipped, not parsed.
LINE_OTHER = "[2026-06-27] some other diagnostic line from the solver"
# Looks like a drpm line (has the signature) but is truncated -> must fail loud.
LINE_BROKEN = ".\t S.FCM 1 S.ms 0.123 T.ms 4.56\t L "

FAILURES = []


def check(cond, msg):
    if not cond:
        FAILURES.append(msg)


def main():
    r = drpm_log.parse_line(LINE_M)
    check(r is not None, "LINE_M should parse")
    check(r.fcm is True, f"fcm: {r.fcm!r}")
    check(r.sat_ms == 0.123, f"sat_ms: {r.sat_ms!r}")
    check(r.theory_ms == 4.56, f"theory_ms: {r.theory_ms!r}")
    check(r.lemma_size == 42, f"lemma_size: {r.lemma_size!r}")
    check(r.mode == "M", f"mode: {r.mode!r}")
    check(r.pm_ms == 0.789, f"pm_ms: {r.pm_ms!r}")
    check(r.pm_matches == 3, f"pm_matches: {r.pm_matches!r}")
    check(r.c26_not_pure_any is None, f"c26 should be None: {r.c26_not_pure_any!r}")

    a = drpm_log.parse_line(LINE_A)
    check(a.fcm is False, f"A fcm: {a.fcm!r}")
    check(a.theory_ms == 1.23e3, f"A theory_ms (sci): {a.theory_ms!r}")
    check(a.lemma_size == 970, f"A lemma_size: {a.lemma_size!r}")
    check(a.mode == "A", f"A mode: {a.mode!r}")

    c = drpm_log.parse_line(LINE_C26)
    check(c.c26_not_pure_time == 5, f"c26 npT: {c.c26_not_pure_time!r}")
    check(c.c26_not_pure_logic == 2, f"c26 npL: {c.c26_not_pure_logic!r}")
    check(c.c26_not_pure_any == 7, f"c26 np*: {c.c26_not_pure_any!r}")

    check(drpm_log.parse_line(LINE_OTHER) is None, "non-drpm line must return None")
    check(drpm_log.parse_line("") is None, "empty line must return None")

    try:
        drpm_log.parse_line(LINE_BROKEN)
        check(False, "broken drpm-signature line must raise, not return")
    except ValueError:
        pass

    # iter_records skips non-drpm lines and yields the rest, in order.
    recs = list(drpm_log.iter_records([LINE_OTHER, LINE_M, LINE_OTHER, LINE_A]))
    check([x.lemma_size for x in recs] == [42, 970], f"iter order/skip: {[x.lemma_size for x in recs]}")

    s = drpm_log.summarize([1.0, 2.0, 3.0, 4.0])
    check(s["n"] == 4 and s["median"] == 2.5 and s["max"] == 4.0 and s["total"] == 10.0,
          f"summarize: {s}")

    if FAILURES:
        print("FAIL:", file=sys.stderr)
        for m in FAILURES:
            print("  -", m, file=sys.stderr)
        sys.exit(1)
    print("ok — all drpm_log parser tests passed")


if __name__ == "__main__":
    main()
