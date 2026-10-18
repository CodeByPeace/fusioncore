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
