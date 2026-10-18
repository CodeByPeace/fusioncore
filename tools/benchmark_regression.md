# Benchmark regression tracking

FusionCore is a tuned filter with many coupled parameters. A change made to win
one scenario (for example a long GPS blackout) can quietly worsen another. This
directory holds a small system that makes those trade-offs **visible** instead of
letting benchmark numbers drift away from the code over time.

## The pieces

- `evaluate.py` writes `metrics.json` next to `BENCHMARK.md` on every run
  (machine-readable ATE / drift / RPE per filter).
- `benchmark_baseline.json` records the reference FusionCore XY ATE per sequence,
  with provenance (`commit`, `measured`, and a `verified` flag).
- `check_benchmark_regression.py` diffs fresh `metrics.json` against the baseline
  and exits non-zero if FusionCore XY ATE grew beyond the threshold.

## Workflow

1. Run a sequence (produces `results.../metrics.json`):

   ```
   scratchpad/run_nclt_fast.sh 2013-04-05 1.0 0.0
   ```

   or re-evaluate existing `.tum` trajectories without re-running the filter:

   ```
   python3 tools/evaluate.py \
     --gt <gt.tum> --fusioncore <fc.tum> --rl <rl.tum> \
     --sequence 2013-04-05 --out_dir <results_dir>
   ```

2. Check for regressions:

   ```
   python3 tools/check_benchmark_regression.py \
     --glob 'benchmarks/nclt/*/results_fast/metrics.json'
   ```

   Exit code is non-zero if any sequence regressed, so this can gate a release or
   a CI job before published numbers are updated.

3. When a run is trusted (controlled config, current `main`), update the sequence
   entry in `benchmark_baseline.json` and set `"verified": true`.

## Status and honesty notes

- The baseline is **partial**: only the sequences currently staged on disk are
  tracked. A controlled full-suite re-run on current `main` is still owed before
  any published benchmark table is updated.
- `verified: false` entries were measured mid-investigation and should be re-run
  under a controlled current-`main` config before being trusted.
- Rule of thumb: do not update a published number without a `verified` baseline
  entry and a clean regression check.

Why this exists: the published benchmark snapshot predated weeks of filter
changes, some of which improved the long-blackout sequences while regressing
another, and nothing caught it because scores were never tracked per commit.
This closes that gap.

## Working on the machine during a playback: what it does and does not do (2026-10-08/09)

**The claim first written here on 2026-10-08 was WRONG and is withdrawn.** It said that
working on this machine during a playback starved robot_localization and moved the control
by 148%, voiding the run. The first half is real. The second is not, and a clean re-run
disproved it the same night.

2012-06-15 was run twice, hours apart, with nothing changed but whether this session was
using the machine:

```
                        starvation   RL control ATE   FusionCore ATE   achieved Hz
  afternoon, machine in use     62         45.772           78.112          97.9
  night, machine left alone      0         45.864           82.011          99.9
                                          +0.20%           +4.99%
```

The control reproduced to **0.20%** across a 62 versus 0 difference in starvation events.
Whatever those events are, they did not move the measurement. FusionCore's 4.99% sits
inside its documented 5.8% spread on this sequence, so it moved no more than it moves
anyway.

**A second error in the same note:** robot_localization was described as starved down to
19.7 Hz against FusionCore's 97.9. It was not. 20 Hz is simply the rate it runs at here,
and it logged 65325 poses in the first run against 66158 in the second, 1.28% apart. Two
different configured rates were read as a shortfall.

**What actually moved the control, and it is far more important.** robot_localization runs
no FusionCore code, so the only thing that can change its score is its INPUT. The input did
change: #169 corrected the frame of the wheel-odometry yaw rate the player publishes, and
both filters consume it. Every entry in benchmark_baseline.json is marked `pre_frame_fix`
for exactly this reason. Measured on the three baseline sequences:

```
  sequence      RL before   RL after    change
  2012-06-15      18.487     45.864    +148.1%
  2012-08-20      10.519     10.395      -1.2%
  2013-04-05     266.700     65.776     -75.3%
```

So the control is only an integrity check WHEN THE INPUT IS UNCHANGED. Comparing across a
deliberate change to the data the player emits, a moving control is the expected result and
not a fault. rerun_baseline.sh prints "CONTROL MOVED, FC DELTA NOT ATTRIBUTABLE" on two of
these three, and on this particular comparison that warning is correct about attribution
and misleading about cause. Read it as "the baseline predates a change to the input", not
as "the run was contaminated".

**What still stands, and is still worth doing.** 54 starvation events in 31 worked minutes
against 1 in 19 idle minutes is real and timestamped. Leaving the machine alone produced 0
events across all three sequences. Starvation is a cost worth avoiding and a signal worth
recording, and the 2026-10-05 incident where a run was voided after polling with
`ros2 topic hz` is a different and heavier kind of load than reading a log file. But the
rule is now stated at its measured strength rather than above it: **keep off the machine,
and do not attribute a moved control to it without a paired run.**

**The method lesson, which is the one to keep.** The afternoon's conclusion came from a
single run plus a plausible mechanism, and the repo already had a rule against exactly
that: "when a repeat disagrees with the first run, the next move is another repeat, not a
theory." A theory was published instead, into this file and into the project memory, within
an hour of the observation. The repeat cost nothing and settled it.


---

## 2026-10-17: 2012-02-04's tight spread was two samples that landed together

`benchmark_baseline.json` recorded 2012-02-04 at **n=2 with a 0.51% spread** and said in
as many words that "a few percent is meaningful here". A third run with the same shipped
config, on `6537cf3`, read **134.045** against the recorded 153.883 and 153.097.

```
  arm                   run 1      run 2      run 3     spread
  FusionCore          153.883    153.097    134.045    14.80%
  robot_localization  123.131    123.149    123.121     0.02%
```

The control holding at 0.02% across all three is what places the 14.80% in FusionCore's
own column rather than in the harness or the input. **29x wider than the figure this file
asserted for eight days.**

The lesson is narrow and it is not "FusionCore is noisy". It is that **a tight spread at
n=2 is not evidence of a tight spread.** Two samples landing close together is the
expected outcome a good fraction of the time even from a wide distribution, and nothing
about the first two runs looked wrong. Every n=2 entry in the baseline file carries the
same exposure, and the ones with a tight recorded spread carry it most, because a tight
spread is what licenses reading a small delta as real.

Practical consequences, and the first one is the expensive one:

- **Any A/B on 2012-02-04 needs repeats per arm.** At n=1 per arm a 14.8% difference
  means nothing on this sequence. The 2026-10-16 profile run read 32.422 there, a 78.8%
  improvement, and it cannot be graded.
- The gate threshold is 10%, so this sequence cannot be graded by
  `check_benchmark_regression.py` at all until n grows per arm.
- **The 272.258 control from that same profile run is a rare failure mode, not the
  control's operating point.** Three clean runs put it at 123.1. A control that moves
  121% is still a reason to void a run; it is not a reason to believe the control
  changed.

One further observation, recorded without being used: run 3 logged **32 robot_localization
starvation events** against 0 in the first two, clustered and matching no work done on the
machine, so the load was external. Its control moved 0.02% and its pose count held, so it
was not starved in output. Attributing the FusionCore move to machine load would repeat
the 2026-10-08 mistake documented above, where a paired run measured 62 starvations
against 0 and moved the control 0.20%.

---

## 2026-10-17: no input feature separates the wins from the losses

FusionCore wins 4 of the 9 sequences measured on corrected input and loses 5, with
baseline accuracy around 7 m on the wins and 23 to 46 m on the losses. That gap is the
oldest open question here, and no field, platform or vertical claim is supportable until
it is explained, because without it you cannot say which conditions FusionCore is good
in.

`tools/winloss_features.py` puts the verdict beside fifteen candidate columns, computed
from the raw data rather than from a run: motion features from `odometry_mu_100hz.csv`,
which is what `nclt_player.py` actually feeds the filter, and GNSS-quality features from
`gps.csv`. It prints an explicit separation check, because a table of 9 rows and 15
columns invites finding a pattern that is not there.

```
  sequence     verd        FC       RL |    min  path_m  stop%  nstops | strght%  rot_deg   med_w
  2012-03-31   WIN       28.5    160.6 |     88    6094    8.1      38 |    44.6    36965   0.064
  2012-09-28   WIN        8.9     91.9 |     78    5680    5.9      17 |    42.8    34602   0.069
  2012-10-28   WIN       17.1     25.1 |     86    5821    7.8      33 |    45.6    34034   0.058
  2013-04-05   WIN        9.2     65.8 |     70    4675    9.8      37 |    48.3    23389   0.049
  2012-01-08   loss     185.2     22.8 |     94    6545    7.2      24 |    46.1    37565   0.057
  2012-02-04   loss     147.0    123.1 |     79    5625    6.6      16 |    45.3    32429   0.060
  2012-05-11   loss       9.6      8.9 |     85    6144    6.0      21 |    42.3    40530   0.074
  2012-06-15   loss      32.1     45.8 |     55    4129    5.9      14 |    44.9    22593   0.060
  2012-08-20   loss     157.6     10.4 |     84    6106    6.3      17 |    42.8    35878   0.068
```

**Every column overlaps. Nothing separates.** Stops, stop count, longest stop, straight
fraction, total rotation, rotation per metre, median yaw rate, path length, duration, max
GNSS gap, gaps over 60 s, usable-fix percentage, mode-flip rate and fix count: in all
fifteen the win range and the loss range intersect, usually heavily. Straight fraction is
42.8 to 48.3% on the wins and 42.3 to 46.1% on the losses. Median yaw rate is 0.05 to
0.07 rad/s on both.

**Which makes sense once said out loud: NCLT is one Segway driving one campus across
fifteen months.** The sequences are near-interchangeable by bulk motion statistics, so
the thing that turns a 9 m result into a 185 m result is not visible in them.

Three consequences.

**The field question cannot be answered from NCLT.** Picking a vertical needs to know
which conditions FusionCore is good in, and this dataset does not vary enough in the
right dimensions to tell you. That has to come from bags off other robots, which makes
the outstanding asks to Swap Robotics, Joseph at Upside and Aigen the path to the answer
rather than a parallel activity.

**The remaining hypothesis is filter-internal, not conditional.** FusionCore is 2.5x to
7.6x worse in the MEDIAN on the losses, not only in RMSE (23.37 against 9.18, 33.65
against 4.44, 46.12 against 22.09), so it is holding a sustained wrong state rather than
taking one excursion and recovering. A sustained offset with no input feature to explain
it points at the filter entering a bad state and staying there, which is #150.

**None of these runs gives the filter much chance to observe bias while stationary.**
Contiguous stops of 2 s or longer total 59 to 183 s out of 55 to 94 minutes, on every
sequence, wins and losses alike. So "it stopped more often" cannot be the explanation
either, and ZUPT is doing less work here than its presence suggests.

---

## 2026-10-18 (SUPERSEDED BELOW): the attitude bug was NOT the NCLT lock-in

The exact-log-map fix made heading converge in a controlled test where it had previously
random-walked the whole circle. The obvious question was whether that bug was also what
locks a losing NCLT sequence into a wrong state, since a covariance that lost 40% of
itself on every rebuild is exactly the precondition for a filter that is confident and
wrong.

It is not. 2012-08-20, the 15x loss, run through the deterministic core replay as a 2x2
over the attitude flag and the `ground_robot` bias freeze:

```
  cell          gnss rejects      path_m      ATE RMSE 3D
  OFF_free              2121    10586.56         171.393
  ON_free               2299    25011.02         158.843
  OFF_frozen            2070     9771.16         145.765
  ON_frozen             2759     9789.83         166.135
```

Encoder reference path is 6105.75 m. Nothing drops toward the 7 m that the winning
sequences achieve. The best cell is the bias freeze alone at 145.765, about 15% better
than baseline, which is the same order the profile already measured through the node.

**So two suspects are cleared and the bistability question stays open.** The attitude
representation was a real bug with a real controlled-test fix, and it is not what makes
a sequence lose.

One warning sign against turning the flag on: **ON_free produced a 25,011 m fused path
for a 6,106 m drive**, four times the true distance, while its ATE happened to read
lower. A lower ATE on a trajectory that long is not an improvement, and it is another
reason the flag stays off until the turning error is understood.

Caveats, stated because the numbers invite over-reading. This is **n=1 per cell on one
sequence**. Replay ATE is not comparable to node ATE on the same data: the node scored
157.591 here while the replay baseline is 171.393, because `export_bag.py` flattens
lat/lon with its own conversion while the node uses PROJ ECEF to ENU, which is a known
and documented gap. The comparison across cells is valid because all four share one
exported log and one projection.

`tools/yaml_to_replay_args.py` was written for this and is the supported way to drive an
offline A/B with the same configuration the node would have used.

---

## 2026-10-18, corrected: the attitude flag is ACTIVELY HARMFUL on real data

The section above measured one sequence, in ATE RMSE, against a target quoted in
MEDIAN. Those are different metrics and the comparison as framed was invalid. The data
was fine; the framing was mine and wrong, which is the third summarisation error in two
days and the reason the rule about pasting raw numbers now exists.

Re-scored with every metric the scorer already reported, and with a WIN added so there
is something to compare a loss against:

```
  2012-08-20 (LOSS, node 157.591, node median 33.65)
  cell              RMSE     median   path_ratio    drift
  OFF_free       171.393     35.436        0.888    20.24
  ON_free        158.843     30.012        0.614    12.96
  OFF_frozen     145.765     31.618        0.967    18.76
  ON_frozen      166.135     30.660        0.940    20.77

  2013-04-05 (WIN, node 9.193, node median 7.31)
  cell              RMSE     median   path_ratio    drift
  OFF_free        16.677      6.960        1.019     3.17
  ON_free         64.159     11.029        0.171     2.04
  OFF_frozen      15.355      7.001        1.073     3.07
  ON_frozen       27.444      8.165        1.049     5.37
```

**The attitude flag makes the WIN 3.8x worse**, 16.677 to 64.159, with the median going
6.960 to 11.029. And the path length ratio collapses to **0.171**, meaning the
trajectory is 83% too short.

**That reframes the apparent improvement on the loss.** ON_free looked better there
(171.393 to 158.843) only because the loss is already bad enough to hide the same
degeneracy: its path ratio is 0.614. The flag causes systematic UNDER-TRAVEL on both
sequences, and ATE flattered it on the one where there was more room to hide.

Without the path-length ratio beside ATE this would have been recorded as a partial
improvement. It is not. **Report the ratio next to ATE on every benchmark from now on.**
A ratio far from 1.0 catches jitter, oscillation and degeneracy no matter how the SE3
alignment happens to land.

**OFF_frozen is the best cell on both sequences**: 145.765 at ratio 0.967 on the loss,
15.355 at ratio 1.073 on the win. The `ground_robot` bias freeze helps a loss and a win
alike, which is consistent with what the profile already measured through the node.

**So the attitude flag stays off, and not merely pending the turning work.** It has a
systematic under-travel problem on real data. The likely mechanism is the one this filter
has had all along: the exact log map no longer under-reports large rotations, so the
attitude covariance is correctly LARGER, the sigma points spread wider, and averaging
`R(q)*v*dt` across a wider attitude cloud cancels more forward displacement. The
small-angle bug was partially masking the cancellation defect by shrinking the covariance.

Both conclusions from the superseded section survive: the attitude bug was not the NCLT
lock-in, and the bistability question is still open.


---

## 2026-10-18: the old tuning WAS compensating for the leak, and a cheap bistability repro

Path length ratio has a closed form under heading uncertainty: averaging `R(psi)*v` over
a yaw spread of sigma gives `v*exp(-sigma^2/2)`, so the ratio measures yaw sigma
directly. A ratio of 0.171 implies **sigma = 107.7 degrees**, which is approaching
uniform. So the under-travel is not a defect in the averaging. It is correct averaging
fed a covariance that is far too large.

Hypothesis tested: `ukf.q_angular_vel` was tuned against a covariance that leaked up to
40% on every rebuild, so with the leak fixed the same value inflates without limit.

2013-04-05, attitude flag ON, bias free, sweeping only `ukf.q_angular_vel`:

```
  q_angular_vel      RMSE     median   path_ratio   implied yaw sigma
  0.1  (shipped)   64.159     11.029        0.171       107.7 deg
  0.01             26.696      7.832        1.053         0.0 deg
  0.001            41.131      8.403        0.302        88.7 deg
  1e-4             28.673      7.856        0.174       107.2 deg
  1e-5             31.042      8.067        0.997         4.4 deg
```

**Confirmed.** Dropping the shipped 0.1 to 0.01 takes the path ratio from 0.171 to
1.053, RMSE from 64.159 to 26.696 and the median from 11.029 to 7.832. A parameter
tuned around a bug became wrong the moment the bug was fixed.

**But look at the column, because it is not monotonic.** 0.01 is healthy, 0.001 is
degenerate, 1e-4 is degenerate, 1e-5 is healthy again. Three orders of magnitude of
process noise and the filter flips between a path ratio near 1.0 and one near 0.17, with
nothing in between.

**That is the bistability, reproduced in seconds instead of 90 minutes.** The same input,
the same code, a small parameter change, and two qualitatively different outcomes: one
tracking, one with yaw sigma near 108 degrees. Up to now bistability was an inference
from the fact that no input feature separates the wins from the losses. This is a
controlled, cheap, repeatable handle on it.

**It does not rescue the flag.** The best ON cell is 26.696 against 16.677 for the
shipped default with the flag off, so the flag still loses on this sequence even when
tuned. It stays off.

The useful output is the repro, not the tuning. Any future work on the wrong-basin
question should start here, because it costs seconds per sample.

---

## 2026-10-18: the outlier gate IS the discrete switch, and it protects rather than locks out

Step 1 of the twin-run plan: rerun the `q_angular_vel` sweep with all outlier gates
disabled. If the flipping disappears, the gate is the switch.

2013-04-05, attitude flag ON, bias free, only `ukf.q_angular_vel` and
`outlier_rejection` changed:

```
  gates ON                                        gates OFF
  q_ang_vel     RMSE   median   ratio  rejects    RMSE   median   ratio  rejects
  0.1         64.159   11.029   0.171       61   271.480  42.400  0.153       12
  0.01        26.696    7.832   1.053      145   259.383  40.609  0.161       12
  0.001       41.131    8.403   0.302      101   238.688  35.816  0.599       12
  1e-4        28.673    7.856   0.174       68   251.513  40.034  0.184       12
  1e-5        31.042    8.067   0.997      147   227.750  36.445  0.289       12
```

**The flipping disappears with the gates off.** Every cell is uniformly degenerate,
227 to 271 m, ratio 0.15 to 0.60, and the healthy outcomes at q = 0.01 and 1e-5 are
gone. So the gate is the discrete switch, exactly as predicted.

**But the direction is the opposite of the lock-out theory, and the rejection counts say
so plainly.** With the gates on, the HEALTHY runs reject MORE: 145 and 147 rejections at
q = 0.01 and 1e-5, against 61, 68 and 101 in the degenerate ones. A locked-out filter
would show the reverse, long streaks of rejection in the bad runs.

The mechanism that fits every column: the Mahalanobis test normalises by
`S = HPH' + R`. In a degenerate run the attitude covariance is enormous, so S is
enormous, so the gate is effectively wide open and accepts bad fixes (61 rejections).
In a healthy run P is sane, S is sane, the gate does its job (147 rejections) and the
filter stays tracking. **The gate is not causing the lock-in. It is the thing preventing
it, and it stops working precisely when the covariance it normalises by has blown up.**

That makes it self-reinforcing, which is what a bistability needs: a big P disables the
only mechanism that would have kept P small.

**Consequence for the fix.** A recovery path after N consecutive rejections, the obvious
remedy under the lock-out theory, would have been aimed at the wrong end of the loop:
the bad runs are not rejecting enough to trigger it. The lever is bounding the attitude
covariance so the gate keeps working, which is also what the `ground_robot` freeze does
and is consistent with OFF_frozen being the best cell on both a win and a loss.

Still to do on this thread: a fine sweep between 0.01 and 0.001 to find neighbouring
healthy and degenerate values, then a twin-run diff logging every discrete decision with
a timestamp to find the first one that differs. And the same on a losing sequence with
the flag OFF, to confirm it is the same switch rather than one the flag created.

Caveat by the rule: "implied yaw sigma" in the section above is an INFERENCE from path
ratio, not a measurement, and path ratio also moves with jitter. Yaw sigma should be
logged from P directly before that number is quoted again.

---

## 2026-10-18: twin-run diff. P blows up FIRST, and no rejection cascade is involved

The fine sweep between 0.001 and 0.01 gives adjacent twins that differ by 33% in one
parameter and land in different worlds:

```
  q_angular_vel     RMSE   median   path_ratio   rejects
  0.001           41.131    8.403        0.302       101
  0.0015          26.096    7.619        1.001       143   HEALTHY
  0.002           67.471   11.997        0.173        67   DEGENERATE
  0.003           28.410    7.475        0.303       111
  0.004           32.709    7.688        1.002       148
  0.005           29.892    8.044        0.992       146
  0.0065          34.443    8.490        0.173        72
  0.008           43.234    8.712        0.192       112
  0.01            26.696    7.832        1.053       145
```

Path ratio is bimodal: every run is either about 1.00 or about 0.2, with nothing
between. Healthy runs reject 143 to 148, degenerate ones 67 to 112.

Running the 0.0015 / 0.002 twins with per-fix logging:

```
    fix#   t_rel    sep_m   sigma_h   sigma_d   acc_h   acc_d
       0     0.0    0.000    1.0694    1.0694       1       1
     100    22.6    0.013    1.8174    1.7065       1       1
     450    92.6    0.548    1.4135    1.5508       1       1
    1000   206.5    0.001    1.7572    1.7426       1       1
    1500   362.2    0.751    1.5058    2.9761       1       1   <- sigma doubles
    2000   464.2    0.574    1.3803    2.9405       1       1
    2500   564.4    0.880    1.7077    2.9498       1       1
    2576   579.6   13.559    1.5301    2.9337       0       1   <- first disagreement
```

**Three findings, and all three contradict the obvious story.**

**1. Both twins accepted EVERY fix for 2576 fixes. Zero rejections in either, right up to
the first disagreement.** There is no rejection cascade, no lockout, no latch. Whatever
causes this, it is not the gate throwing fixes away.

**2. The covariance diverges 217 seconds before the position does.** The degenerate
twin's sigma doubles from about 1.4 to 2.94 at fix 1500, t = 362 s, and stays there,
while the healthy twin holds 1.4 to 1.7. Position separation is still under 1 m at that
point and only jumps to 13.6 m at fix 2576, t = 580 s.

**3. So P blows up FIRST.** This answers the question the two levers hung on. It is not a
bad fix accepted while P was sane and then wrecking everything. It is the covariance
growing, which widens the gate, which lets through a fix the healthy twin rejects
(mah2 0.13 against 16.58 on the same fix, threshold 16.27).

**The lever is therefore bounding covariance growth at its source, not the GNSS
measurement model.** That is the `ground_robot` bias freeze now and IMU-as-input later,
and it is consistent with OFF_frozen winning on both a win and a loss.

The separate point about R stands on its own evidence and is unaffected: with gates off,
even a sane-P run at q = 0.01 collapses from 26.696 to 259.383, so some NCLT fixes are
far worse than the covariance they declare. That is a second, independent defect.

What is still not known is what happens at t = 362 s to double the covariance. That is
the next thing to look at, and the twins make it a seconds-long experiment.

---

## OPEN DEFECT: NCLT GNSS fixes are worse than the covariance they declare

Filed separately because it is independent of the bistability above and has its own
evidence.

With all outlier gates disabled, a run whose covariance is demonstrably sane collapses
anyway. 2013-04-05, attitude flag ON, `q_angular_vel = 0.01`, the healthiest cell in the
sweep:

```
  gates ON    RMSE  26.696   median  7.832   path ratio 1.053   rejects 145
  gates OFF   RMSE 259.383   median 40.609   path ratio 0.161   rejects  12
```

A 9.7x degradation with no change to the filter's tuning. The chi-squared gate is the
only thing standing between the filter and a set of fixes whose declared covariance does
not describe them. The record already carries the extreme case: on 2012-08-20 two raw
fixes 0.07 s apart differ by 825 m, which is 11789 m/s on a 1.5 m/s Segway, and no
reported sigma describes that.

**Consequence: the filter currently depends on the gate for correctness, not just for
robustness.** That is a fragile place to be, because the same experiment shows the gate
stops working when the covariance it normalises by grows.

The honest fix is on the measurement side rather than the filter side: set R from the
quality the receiver actually reports, reject degraded fix modes outright, and
cross-check successive fixes against each other and against odometry before trusting
them. Any absolute innovation limit added here must come with re-anchoring when several
successive fixes agree with each other but disagree with the filter, because that means
the filter is the wrong one, and without it this rebuilds the latch bug that once
disabled spike rejection for entire runs.

## Agreed plan and stop rule, 2026-10-18

1. **Find what doubles the covariance at t = 362 s.** Rerun the 0.0015 / 0.002 twins on
   2013-04-05 with the flag on, logging every discrete branch (ZUPT on and off, coast
   enter and exit, adaptive-noise switches, wheel-odometry gate) plus yaw sigma straight
   from P and the effective gate radius `sqrt(chi2 * S)` in metres, step by step from
   340 to 380 s. Report the first step where P differs by more than 1% and which branch
   fired at or just before it.
2. **Confirm it is the same loop in the shipped config.** Same twin method on
   2012-08-20 with the flag OFF, sweeping q or the gate threshold until the outcome
   flips, and check the pattern holds: P first, then the gate widens, then a bad fix
   gets through.
3. **Stop rule. After 1 and 2, or two sessions at most, pause the deep dive and ship.**
   Run the `ground_robot` freeze across all six available bags in replay; if it wins on
   most, propose it as the default for the wheeled-robot preset. Fix the projection gap
   so `export_bag.py` and the node share one lat/lon projection. Rerun the benchmarks
   once, then draft the public correction with the updated numbers.

Standing rules for all of it: replay only and never through the live node for any
benchmark; report RMSE, median and path ratio together; the attitude flag stays off.

---

## 2026-10-18, Block 1: there is NO event at 362 s. Divergence starts at 10 s.

`tools/repro/pblowup.cpp` logs the diagonal blocks of P directly, plus yaw sigma computed
from the quaternion covariance rather than inferred from path ratio. Twins q = 0.0015 and
q = 0.002 on 2013-04-05, attitude flag ON.

First step at which each block of P differs by more than 1% between the twins:

```
  block           t_rel        healthy     degenerate
  P_angvel         9.59       0.241693       0.244623
  P_quat          16.63         0.2807         0.2766
  yaw_sig         18.65        55.4783        54.9199
  P_gbias         23.70         0.1329         0.1313
  P_accel         27.21        12.1707        12.3492
  P_pos           28.72        21.0795        22.1537
  P_vel           29.22        31.2951        30.2855
```

**Everything happens in the first 30 seconds, and the first block to move is the one the
swept parameter controls directly.** `q_angular_vel` sets the angular-velocity process
noise, so `P_angvel` separating at 9.59 s is the parameter doing exactly what it says.
It reaches the quaternion 7 s later, yaw sigma 2 s after that, and the rest of the state
within another 10 s.

**So the earlier "sigma doubles at t = 362 s" was wrong, and the error was mine.** That
number came from the `sigma_x` column of the per-fix GNSS CSV, which is the adaptively
scaled measurement noise, not the filter covariance. Measured from P directly, the twins
have already parted by 30 s.

**The far more important finding is in the yaw sigma trajectory, and it applies to BOTH
twins:**

```
    t_rel    healthy      degen
     22.2    52.5504    51.0143
     44.3    44.7697    24.9704
     66.5    55.9493    19.2326
    110.8     8.1878    39.6904
    177.2    32.7259    70.1856
    243.7    43.0086    46.1270
    332.2     4.5278    25.6770
    398.6    13.1652    24.4433
```

**Yaw sigma runs 5 to 70 degrees for the whole run in both twins.** The healthy one is at
55.9 degrees at t = 66.5 s. A filter with continuous 1 Hz GNSS and working wheel odometry
should know its heading to single digits. Neither twin ever does.

**That reframes the whole question.** There is no discrete event to find and no single
branch to instrument. Heading uncertainty is pathological from the first minute, both
twins wander inside that uncertainty, and the outlier gate turns a continuous divergence
into two discrete basins, which is why the outcome is bimodal and why it disappears when
the gates are switched off. Sensitive dependence, not a switch firing at a moment.

The practical consequence is unchanged and is now better supported: **bound the attitude
covariance.** Nothing downstream can be fixed while yaw sigma spends the run between 5
and 70 degrees.

**Caveat, stated because it matters for Block 3:** these runs have the attitude flag ON,
and the shipped configuration has it OFF. Whether the shipped losses go through this same
loop is exactly what Block 3 tests, and it has not been run.

---

## 2026-10-18, Block 2: there is NO projection gap. The premise was wrong.

The plan called for one shared lat/lon projection between `export_bag.py` and the node,
on the belief that their different code paths explained node 157.591 against replay
171.393 on 2012-08-20.

**They are already the same transformation.** The node converts through PROJ with
`input.gnss_crs: EPSG:4326` and `output.crs: EPSG:4978`, which is WGS84 geodetic to
WGS84 ECEF. `export_bag.py` uses the closed form for exactly that. Tested on twelve real
2012-08-20 fixes spanning the track:

```
          lat          lon |  max|diff| m
    42.293916   -83.708331 |    2.910e-10
    42.293949   -83.715033 |    9.313e-10
    42.292802   -83.712655 |    5.821e-11
    42.290485   -83.717180 |    4.657e-10
    42.291835   -83.715965 |    1.746e-10
    42.293924   -83.711118 |    0.000e+00
```

**Worst disagreement anywhere on the track: 9.313e-10 m.** Sub-nanometre. Both paths
then call the same `fusioncore::sensors::ecef_to_enu`. The comment in the node saying
"default behavior is identical to the hand-coded WGS84 to ECEF math" is correct and was
worth believing. (The rows showing nan are fixes whose altitude is nan in the raw NCLT
`gps.csv`, which is a property of the dataset, not of either projection.)

**So the node-versus-replay difference is not a pipeline gap.** Comparing like with like,
the node's frozen-config run scored 157.591 and the replay's OFF_frozen cell scored
145.765, a 7.5% difference.

**That is well inside the node's own measured run-to-run spread.** 2012-02-04 repeated
through the node at 153.883, 153.097 and 134.045, a 14.80% spread on identical input and
an unchanged control. A 7.5% node-to-replay difference needs no explanation beyond the
node being the nondeterministic one.

**Two consequences.**

The acceptance test as written, node and replay agreeing within 0.5%, **is not
achievable, and not because of the projection.** The node does not reproduce itself to
0.5%. Asking the replay to match it to 0.5% asks it to match a number that moves.

**Absolute published numbers should come from the replay, not the node**, for the same
reason the benchmarking rule already says replay only: it is the deterministic path. The
node remains the right integration test, checking that it lands within the replay's
figure plus the node's own known spread.

Block 2 therefore passes with no code change, and Block 4 is unblocked.

---

## 2026-10-18, Block 4: the ground_robot freeze wins 6 of 6 in replay

All six exported sequences through the deterministic core replay, shipped NCLT config,
only the seven `ground_robot` keys changed between arms. Attitude flag OFF throughout.

```
  sequence     arm           RMSE    median   path_ratio    drift
  2012-01-08   free        19.974     7.183        0.848     3.61
  2012-01-08   frozen      19.423     7.123        0.877     3.63
  2012-02-04   free       142.792    34.495        1.055    18.07
  2012-02-04   frozen     142.311    34.582        1.108    18.90
  2012-06-15   free        78.139    29.978        1.113    14.84
  2012-06-15   frozen      64.295    18.164        1.180    12.94
  2012-08-20   free       171.393    35.436        0.888    20.24
  2012-08-20   frozen     145.765    31.618        0.967    18.76
  2012-09-28   free        23.638     7.235        1.048     3.43
  2012-09-28   frozen      22.501     7.075        1.084     3.38
  2013-04-05   free        16.677     6.960        1.019     3.17
  2013-04-05   frozen      15.355     7.001        1.073     3.07
```

**Frozen is better on RMSE on all six**: -2.8%, -0.3%, -17.7%, -15.0%, -4.8%, -7.9%.

**Median is better on four of six**, and the two it loses are 34.582 against 34.495 and
7.001 against 6.960, which are 0.25% and 0.6%. The biggest median gain is 2012-06-15 at
29.978 to **18.164**, a 39% improvement.

**No path-ratio degeneracy anywhere.** Every cell sits between 0.848 and 1.180, and the
freeze moves every ratio upward, which helps the two sequences that were under-travelling
(0.848 to 0.877 and 0.888 to 0.967) and slightly overshoots on the four that were already
above 1.0.

Honest reading. This is **n=1 per cell**, and 2012-02-04 carries a measured 14.80% spread
through the node, so its -0.3% means nothing on its own. The two large gains, -17.7% and
-15.0%, are outside any spread recorded for those sequences. The consistent direction
across six sequences is worth more than any single row.

**What this does and does not justify.** It is strong evidence for recommending
`platform.profile: ground_robot` to anyone running a wheeled robot with encoders, and the
profile README can now say six sequences rather than three. It is **not** on its own a
reason to change the library default from `general`: `fusioncore_core` still has to be
correct for a platform that leaves the ground plane, and the scope statement shipped
today says the library runs on those with nothing frozen. Flipping that default is a
separate decision with a separate blast radius.

---

## 2026-10-18: the WZ/bias pair hypothesis FAILS, and my column label caused it

Predictions were written down before running and are kept in
`tools/repro/wz_bias_predictions.md`. The hypothesis was that `P_angvel = 0.2417` means
the filter cannot pin its rotation rate to better than 28 deg/s while a gyro measures it
at 100 Hz, which is only consistent with the WZ / B_GZ pair at r = -0.998: each huge
individually, their SUM pinned by the gyro, and heading integrating WZ alone.

2013-04-05, t = 100 to 140 s, all four arms:

```
  arm               t_rel   yaw_sig        var_WZ       var_BGZ       var_SUM     r_wb   sum/wz
  flagOFF_free     118.15   22.1178  2.903199e-02  1.718904e-03  2.941712e-02  -0.0944    1.013
  flagOFF_frozen   118.15   10.4647  2.836724e-02  1.310965e-04  2.834013e-02  -0.0410    0.999
  flagON_free      118.15   34.5746  2.888260e-02  1.573027e-03  2.941244e-02  -0.0774    1.018
  flagON_frozen    118.15   11.3186  2.835584e-02  1.206296e-04  2.833968e-02  -0.0370    0.999
```

**P1 FAILS.** `var(WZ + B_GZ)` was predicted to be of order the gyro noise variance,
about 2.5e-5, against a `var(WZ)` of 0.24. Measured, the sum is **2.94e-2 against a WZ
variance of 2.90e-2**, a ratio of **1.013**. The sum is not small. It is the same size as
var(WZ).

**P2 FAILS.** `r(WZ, B_GZ)` was predicted near -1. Measured it runs **-0.6256 down to
-0.0370**, and it is near -0.04 to -0.10 for most of the window. The -0.998 on record was
not measured here and does not describe this run.

**P3 PARTLY HOLDS.** Freezing was predicted to give single-digit yaw sigma. It roughly
HALVES it, 37.7883 to 15.4331 at t = 101 and 22.1178 to 10.4647 at t = 118, but it lands
at 10 to 15 degrees rather than under 10.

**P4 HOLDS.** The pattern is the same with the attitude flag off and on.

**The failure is traceable to my own column label, and that is the fifth time this week.**
My Block 1 table printed a column headed `P_angvel` with the value 0.2417. That column is
`blk(WX,WZ)`, the **sum of the three angular-velocity variances**, not the variance of
WZ. Measured here, `var(WZ)` alone is 0.029. A reader given a column called `P_angvel`
reasonably treats it as a variance, builds a 28 deg/s argument on it, and the argument
inherits my error. Labelling a trace as if it were a variance is the same class of
mistake as reporting a median against an RMSE target.

**What is actually true, and it still points the same way.** `var(WZ)` of 0.029 is a
standard deviation of 0.17 rad/s, about 10 deg/s. Integrated over the seconds between
useful corrections that is tens of degrees of yaw uncertainty, which is the observed 20
to 40. So yaw uncertainty does grow out of angular-rate uncertainty, exactly as the
hypothesis said. What is NOT happening is a precise sum hiding behind two huge
anti-correlated terms. The bias variance contributes directly: freezing it drops
`var(B_GZ)` from 1.7e-3 to 1.3e-4, a factor of 13, and halves yaw sigma.

**That still supports IMU-as-input**, but for a plainer reason than the one proposed.
Integrating `gyro - bias` directly would remove WZ as a free state whose variance grows
between corrections, rather than removing a cancelling pair that is not there.

---

## 2026-10-18, PHASE 1: adapt_R inflates the gyro R by up to 1723x. Two defects found.

Prediction written before measuring, kept in `tools/repro/phase1_predictions.md`: right
after an IMU update, `var(WZ + B_GZ)` should be about `R_gz`. The NCLT config sets
`imu.gyro_noise: 0.003 rad/s`, so **R_gz = 9.000e-06 rad^2/s^2**.

2013-04-05, t = 115 to 125 s, attitude flag off, bias free:

```
  q_angular_vel  adaptive   var(WZ) [rad^2/s^2]  var(WZ+B_GZ)   var(sum)/R_gz   yaw sigma [deg]
  0.1            ON               1.723080e-02   1.551077e-02          1723.4           25.5440
  0.1            OFF              1.089873e-03   8.912238e-06             1.0           26.7427
  0.01           ON               4.200724e-03   2.797061e-03           310.8           24.7926
  0.01           OFF              1.226315e-03   8.905351e-06             1.0           31.3261
  0.001          ON               2.007583e-03   9.173582e-04           101.9           16.1546
  0.001          OFF              2.259952e-03   1.423805e-03           158.2           22.5421
```

**The prediction is confirmed exactly, with adaptive noise OFF: `var(sum)/R_gz = 1.0`.**
8.912238e-06 against a configured 9.000e-06. So the gyro IS being fused, the measurement
model maps WZ correctly, and there is no axis, frame or units bug. That rules out the
third Phase 1 outcome cleanly.

### Defect 1: `adapt_R` has a floor and no ceiling

With adaptive noise ON the same quantity is **1.551077e-02, which is 1723x the
configured R**. `FusionCore::adapt_R` moves R toward the measured innovation covariance
with an exponential average and clamps only from below:

```cpp
  R = (1.0 - config_.adaptive_alpha) * R + config_.adaptive_alpha * C_hat;
  for (int i = 0; i < z_dim; ++i)
    if (R(i,i) < R_floor(i,i)) R(i,i) = R_floor(i,i);   // floor only
```

**That is a positive feedback loop.** The filter is wrong, so innovations are large, so R
grows toward them, so the gyro update carries less weight and the chi-squared gate
(normalised by `S = HPH' + R`) opens wider, so the filter gets wronger. It is bounded
below and unbounded above, and the comment explains the floor while the ceiling was never
considered.

This is the same self-reinforcing shape found from the other direction in the gate
experiment, and it explains why it is self-reinforcing: **a wrong filter manufactures its
own permission to stay wrong.**

### Defect 2: `q_angular_vel` is per STEP and is enormous at 100 Hz

`ukf.hpp` documents `q_angular_vel = 0.1 (rad/s)^2/step` and Q is never scaled by dt. At
100 Hz that injects **10 (rad/s)^2 per second** of angular-rate uncertainty against a
gyro whose own variance is 9e-6.

Even with adaptive noise off and R correct, `var(WZ)` sits at **1.089873e-03**, which is
a standard deviation of 0.033 rad/s or 1.9 deg/s. Integrated over the seconds between
effective corrections that is the 20 to 30 degrees of yaw sigma observed, and the table
shows yaw sigma barely moves when R is fixed (26.7427 at adaptive off against 25.5440
at adaptive on).

**So the two defects are independent.** Fixing R removes the 1723x inflation and the
feedback loop. It does NOT by itself fix yaw sigma, because the per-step process noise
sets a floor on `var(WZ)` regardless.

### Consequence for the plan

The stop rule says: if Phase 1 or 2 finds a large noise bug, fix it and rerun Phase 3
before building anything in Phase 5. **Two large noise bugs were found.** The architecture
work (IMU as input, USQUE) is now explicitly not the next thing to build.

---

## 2026-10-18, PHASE 1 CONCLUDED: two real defects found and fixed, NEITHER causes the losses

Both fixes are implemented behind flags that default off, so the shipped path is byte
identical. 395 tests pass.

### What was confirmed

`var(WZ + B_GZ) / R_gz = 1.0` with adaptation off: 8.912238e-06 against a configured
9.000e-06. **The gyro is fused correctly and the measurement model maps WZ properly.**
No axis, frame or units bug. The third Phase 1 outcome is ruled out.

### Defect 1: `adapt_R` is unbounded above and double counts H*P*H'

Real and measured at **1723x** nominal. Two separate faults in one function: no ceiling,
and `estimate_covariance()` returns `E[nu*nu'] = H*P*H' + R` which is assigned straight
into R, so the filter's own uncertainty is counted as sensor noise. Both now fixable via
`adaptive_r_max_scale` and `adaptive_subtract_hph`.

**But it is not what loses the NCLT sequences.** Turning IMU adaptation off entirely:

```
  sequence      baseline RMSE   imu_adapt_OFF RMSE   median base   median off
  2013-04-05          16.677               16.793         6.960        6.971
  2012-08-20         171.393              174.558        35.436       35.628
  2012-06-15          78.139               78.882        29.978       29.659
```

Under 2% on all three, and slightly worse on two. **The encoder picks up the yaw-rate
slack**, so starving the gyro update of weight costs almost nothing here.

**A global R ceiling is actively harmful.** Setting `adaptive_r_max_scale = 10` across
all sensors sent 2012-08-20 to **16318370.904 m**. GNSS genuinely needs adaptation
headroom, because NCLT fixes really are far worse than their declared covariance, which
is the separate open defect filed above. The ceiling must be per sensor: tight on the
IMU, loose on GNSS.

### Defect 2: Q is per step and never scaled by dt

Real, documented in `ukf.hpp` as `(rad/s)^2/step`, and confirmed numerically: the
predicted steady state `sqrt(q*R)` is sqrt(0.1 * 9e-6) = 9.5e-4 against a measured
1.089873e-03, within 15%. Now fixable via `ukf.q_time_scaled`.

**At an equivalent q it is behaviour preserving**, which is what a correctness fix should
look like: 2013-04-05 goes 16.677 to 16.964 RMSE, 6.960 to 7.005 median. Larger q makes
it worse (24.398 at 3x, 27.674 at 10x).

**The rate-invariance test is INCONCLUSIVE, and not in the predicted direction.**
Decimating the IMU from 100 Hz to 50 Hz:

```
  arm            rate      RMSE     median   path_ratio
  per-step      100Hz    16.677      6.960        1.019
  per-step       50Hz    16.679      7.094        1.027
  dt-scaled     100Hz    16.964      7.005        0.984
  dt-scaled      50Hz    14.392      6.804        0.989
```

**Per-step is already rate invariant here, 16.677 against 16.679.** The prediction was
that it would not be. Two reasons it is a weak test on this data: the encoder runs at
roughly twice the IMU rate and drives most predicts, so halving the IMU removes only
about 13% of predict calls; and the 15% movement in the dt-scaled arm is comparable to
the healthy-band variation already documented on this sequence. A fair rate-invariance
test needs the encoder decimated too, and n greater than 1.

### Conclusion, and it matters for what happens next

**Two genuine correctness defects were found, and neither explains why FusionCore loses
on NCLT.** They are worth fixing because a filter that lets a wrong estimate inflate its
own sensor noise, and one whose behaviour depends on message rate rather than elapsed
time, are both wrong regardless of what they cost on a benchmark.

The stop rule said a large noise bug would mean the architecture work is not needed.
**Two were found and the architecture question is back open**, because neither moved the
result. That is the opposite of the hoped-for outcome and it is the honest one.

---

## 2026-10-18: RAW GNSS BEATS THE FILTER ON ALL SIX SEQUENCES

The dumb baseline nobody had run. Take the GNSS fixes straight out of the exported log,
score them against ground truth as a trajectory, and compare against the fused output.

**First attempt was unfair and the caveat matters.** GNSS-only associates on about 2.9%
of the points the filter does (3947 against 134368 on 2012-01-08), because it only exists
at fix times and is therefore never scored during the blackouts where the filter's error
explodes. Scoring them that way flatters GNSS.

Corrected with `tools/subsample_to_fix_times.py`, which keeps only the filter poses within
50 ms of a GNSS fix, so both are scored on identical points:

```
  sequence      GNSS RMSE   GNSS median |   FC RMSE   FC median |   pairs
  2012-01-08        9.523         7.024 |    20.606       7.116 |    3947
  2012-02-04       58.672        13.817 |   144.903      35.104 |    4970
  2012-06-15        9.046         4.937 |    10.945       4.957 |    3400
  2012-08-20       32.238         7.337 |   176.805      39.923 |    5236
  2012-09-28        8.983         6.289 |    23.956       7.180 |    3990
  2013-04-05        8.562         6.297 |    10.149       6.419 |    3199
```

**Raw GNSS has lower RMSE than the filter on all six, scored on the same points.**

**The medians say something more precise, and more useful.** On four of the six the
medians are within 2%: 7.024 against 7.116, 4.937 against 4.957, 6.289 against 7.180,
6.297 against 6.419. **Typical-case accuracy is the same as doing no filtering at all.**
The RMSE gap is therefore excursions: the filter matches GNSS most of the time and then
departs badly. On 2012-02-04 and 2012-08-20 it is worse even in the median, 13.817
against 35.104 and 7.337 against 39.923.

### What this reframes

**The sequences FusionCore "wins" are not wins.** 2012-09-28 and 2013-04-05, its two best
results at 8.872 and 9.193 RMSE, are matching a raw GNSS stream that scores 8.983 and
8.562 on its own. Fusion is adding nothing there.

**The split is not GNSS quality.** 2012-01-08 and 2013-04-05 have almost identical GNSS
error, 9.523 and 8.562 RMSE, 7.024 and 6.297 median. FusionCore scores 185.195 on the
first and 9.193 on the second. **Same input quality, 20x different output.** So the
hypothesis that the losses come from those sequences' GNSS being worse is falsified: the
split is created by the filter.

**The published benchmark omits the baseline that beats both filters.** The table compares
FusionCore against robot_localization and reports who wins. On this evidence raw GNSS,
interpolated and unfiltered, has lower RMSE than both on most of these sequences. Any
public correction has to carry that, because it is the first thing a careful reader would
check and it is not currently anywhere in the repository.

### The remaining honest caveat

This scores only where GNSS exists. It says nothing about the blackouts, which is the case
fusion is actually for and where raw GNSS has no answer at all. So the correct statement
is narrow and still damaging: **where GNSS is available, fusion is not improving on it,
and is adding excursions.** Quantifying the blackout case needs a different experiment,
scoring only inside the gaps, and that has not been run.

---

## 2026-10-18, CORRECTION: the 20x claim was wrong, and 2012-01-08's replay data is invalid

Two errors in the section above, both mine, found by a label check.

### Error 1: the "20x" mixed three different things

I wrote that 2012-01-08 and 2013-04-05 have near-identical GNSS quality while FusionCore
scores "185.195 and 9.193, same input quality, 20x different output". Those two numbers
are not comparable to each other or to the table they sat next to:

```
  185.195   node run, full12 2026-10-12, FULL trajectory, shipped config
    9.193   node run, frozen-config A/B, FULL trajectory, ground_robot profile
   20.606   replay, AT GNSS FIX TIMES, free arm      <- what the table actually showed
   10.149   replay, AT GNSS FIX TIMES, free arm
```

Different pipelines, different configs, different scoring points. **At fix times in one
pipeline the ratio is 2.0x, not 20x.**

### Error 2: 2012-01-08's exported log is TRUNCATED

It was exported from `profile4_20261017_1420`, which is the run stopped partway through on
request. Measured span against the sequence duration:

```
  sequence      log span   expected   coverage
  2012-01-08        3740       5640        66%    <- INVALID
  2012-02-04        4711       4740        99%
  2012-06-15        3308       3300       100%
  2012-08-20        5030       5040       100%
  2012-09-28        4681       4680       100%
  2013-04-05        4181       4200       100%
```

**Every 2012-01-08 replay number reported today is on two thirds of the sequence** and is
withdrawn: the free/frozen row (19.974 and 19.423) and the GNSS-only row. No complete
2012-01-08 input bag exists, because input recording postdates that run, so it cannot be
re-measured without a node run. The other five sequences are 99 to 100% and stand.

### The finding survives, at 4.7x, without 2012-01-08

Like for like, replay pipeline, free arm, full trajectory:

```
  sequence      GNSS RMSE      FC RMSE
  2012-06-15        9.046       78.139
  2013-04-05        8.562       16.677
```

**GNSS quality within 6% of each other, FusionCore 4.7x apart.** The conclusion is
unchanged and the magnitude is smaller: the split is created by the filter, not inherited
from the input.

### A fairer reading of "median equals raw GNSS"

Consumer GNSS error is strongly time correlated, multipath and slow biases rather than
white noise, and no amount of odometry averages away a slow bias without a second
absolute reference. **So matching raw GNSS in the median may be the ceiling, not a
failure.** Stated as three separable jobs:

1. **Do not make it worse where GNSS is good.** Medians pass on four of five valid
   sequences.
2. **Reject outliers without creating excursions of your own.** FAILS: RMSE is above raw
   GNSS on every sequence while the median is not.
3. **Bound drift during blackouts.** NOT MEASURED. This is the actual value proposition
   and there is still no number for it.

That is a narrower and more useful statement than "fusion adds nothing", and it is what
the public correction should say.

---

## 2026-10-18, CORRECTED AGAIN: two failure types, and the split is NOT created at fix times

The 4.7x in the section above mixed scoring points a second time: it put a **fix-times**
GNSS number beside a **full-trajectory** FusionCore number. Raw GNSS only exists at fix
times, so a full-trajectory GNSS score is not defined at all. Like for like:

```
  sequence      GNSS @fix    FC @fix    FC full_traj
  2012-06-15        9.046     10.945          78.139
  2013-04-05        8.562     10.149          16.677
```

**At fix times FusionCore is nearly identical on those two sequences, 10.945 against
10.149.** So on 2012-06-15 the filter is NOT creating the split while GNSS is present.
The 78.139 against 16.677 gap has to come from the time when GNSS is absent. **It is
losing it in the gaps.**

That withdraws "the split is created by the filter while GNSS is present" as a general
claim. It splits into two distinct failures:

**Type A, blackout drift. 2012-06-15.** Fine at fix times (10.945 against a GNSS 9.046),
bad over the full trajectory (78.139). The error accumulates inside the gaps.

**Type B, excursions with GNSS present. 2012-08-20 and 2012-02-04.** Bad even at fix
times: 176.805 against a GNSS 32.238, and 144.903 against 58.672. This is the shape
predicted for failed re-acquisition: the filter drifts during a gap, then the returning
fixes are far from its prediction, so either the gate rejects them or an inflated R pulls
it back slowly, and the excursion lasts until it reconverges.

The two need different fixes. Type A needs better coasting. Type B needs re-acquisition
and re-anchoring. Neither is an attitude representation problem.

### The blackout experiment, with predictions written first

For every GNSS gap of 10 s or more, record error at gap start, peak error, error at gap
end, the drift curve against time since the last fix, and **re-convergence time**, the
seconds after the gap before error returns to GNSS-level. Same for four dumb baselines:
hold last fix, constant-velocity extrapolation, encoder plus gyro dead reckoning, and
robot_localization.

```
  P1  On 2012-06-15 most of FusionCore's full-trajectory error sits INSIDE the gaps.
  P2  On 2012-08-20 most of it sits in the RE-CONVERGENCE windows just after the gaps.
  P3  If FusionCore does not clearly beat encoder-plus-gyro dead reckoning inside the
      gaps, the inertial coast mode is adding nothing.
```

### Process: every number now carries its provenance

`tools/provenance.py`. A measurement carries four fields and two of them refuse to be
compared unless all four match: **pipeline** (node, replay, sim), **config**, **scoring
points** (full_traj, fix_times, gaps, window), and **coverage** as a fraction of the
sequence.

Seven comparisons were published wrong this week and **not one was a wrong number**.
Every one was a correct number compared against a number from somewhere else. A
raw-numbers rule cannot catch that, because the numbers were raw and correct. Replaying
the structural failures through the rule:

```
  "185.195 vs 9.193, 20x"   CAUGHT: config full12_shipped vs frozen_profile
  "GNSS 9.046 vs FC 78.139" CAUGHT: scoring points fix_times vs full_traj
  truncated 2012-01-08      CAUGHT: covers only 66% of its sequence
  GNSS 9.046 vs FC 10.945   ALLOWED, ratio 0.826
```

Coverage is a field because of the truncated log: anything under 99% refuses to be
compared at all.
