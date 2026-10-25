# Ship rule for changing a default, written before any holdout run (2026-10-22)

A candidate that is "4 of 5 better, 1 worse" on the sequences it was developed on proves
nothing about the next robot, because those five have been looked at hundreds of times.
This file fixes the rule before the data that decides it is touched. The commit that adds
it is the timestamp.

## Inventory, 2026-10-22

All 12 NCLT sequences have raw sensor CSVs and ground truth in `~/nclt/<seq>/`. Replay logs
(`~/nclt/exported/nclt_<seq>.txt`) exist for five:

    DEV (used for every investigation since 2026-10-17; contaminated by design)
      2012-02-04  2012-06-15  2012-08-20  2012-09-28  2013-04-05

    HOLDOUT (never replayed in that investigation; node runs exist for some, unused for tuning)
      2012-01-08  2012-03-31  2012-05-11  2012-10-28  2012-11-04  2012-12-01  2013-02-23

2012-01-08's earlier export is truncated at 66% and is NOT reused; it is rebuilt with the rest.

## Building the holdout logs, and the gate on that

The holdout logs do not exist yet. Either path is a change to the measurement chain:

1. Record inputs on seven 1x node runs and export them (about 10 h, sequential), or
2. A direct converter from the NCLT CSVs to the replay log format.

A converter is admissible ONLY if, run on the five DEV sequences, it reproduces the existing
exported logs event for event (same event types, same timestamps, values within float
formatting). If it does not, it is not used. This repo already has a dataset player whose
frame error invalidated every number measured through it (#169).

## The rule

Measured in replay (deterministic, so no run-to-run noise), shipped NCLT config plus the
candidate's keys, every arm scored through `hardware/ate_tum.py`, against the shipped config
on the same holdout sequence. A candidate becomes a default only if ALL of these hold on the
HOLDOUT:

1. **No sequence worse by more than 5%** in ATE RMSE 3D.
2. **At least 5 of 7 better by 2% or more** in ATE RMSE 3D.
3. **No sequence's median error worse by more than 3%.**
4. **No sequence's `output_rate_path_ratio` worse by more than 0.05.**
5. **No degenerate estimate:** truth-rate path length ratio within 0.10 of the shipped
   config's on every sequence. (2012-06-15 at AY sigma 0.5 scored -44.8% RMSE while
   churning in place; this clause exists for that.)
6. **One shot.** A candidate is run on the holdout once. If it fails, it does not ship, and
   a retuned version may not be re-tested on the same holdout: the holdout is then spent for
   that line of work and has to be replaced (KAIST, or a hardware run with RTK truth).

DEV results are reported alongside, but they cannot make a candidate pass.

## Candidates queued against this rule

    A   ukf.semidefinite_sqrt (the leak fix; changes every output)
    B   platform.profile ground_robot + gyro heading in coast
        (gnss.coast_imu_wz_scale 1, gnss_coast_encoder_wz_scale 1e6)
    C   A + B

Order: A first, because it changes the meaning of "frozen" in B.

## Outcome, 2026-10-23

Holdout logs built for all seven; candidates A and C run once each.

    A   FAIL   4/7 better by 2%; 2012-11-04 +20.5% RMSE, +12.4% median
    C   FAIL   3/7 better; 2012-01-08 +251.9%, 2012-03-31 +15.3%

Neither ships. B was not run: it is C without the leak fix, and its DEV result (2012-08-20
+9.2%) was already the one C was meant to repair. The holdout is spent for this line of work
(rule 6), so B cannot now be tested on it either. Detail in
tools/benchmark_regression.md, 2026-10-23.

## Addendum, 2026-10-25, written and committed BEFORE any run it describes

A separate line of work, so the holdout is fresh for it: **candidate R, re-anchoring on by
default**, the shipping re-anchor configuration (`gnss.reanchor_enabled: true`, rate trigger
only, bias trigger off: the library defaults for every other `gnss.reanchor_*` key).

**Pipeline changes for R, and why:** replay with `imuq_rp=1`, which fuses the IMU orientation
exactly as the node does. On 2026-10-25 that was shown to reproduce the node on 2012-01-08 to
0.3% where plain replay was 3.43x off. The baseline arm is the shipped config on the same
pipeline. Everything else in the rule (clauses 1 to 6, scoring through hardware/ate_tum.py)
applies unchanged. R is run once on the seven holdout sequences; DEV results are reported
alongside and cannot make it pass.
