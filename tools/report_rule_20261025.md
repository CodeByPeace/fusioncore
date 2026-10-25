# Reporting rule for the 2026-10-25 twelve-sequence evaluation, committed BEFORE it runs

This is pure evaluation. Nothing is tuned, selected or re-run to improve a number. The run
exists to replace two README sections (raw GPS against FusionCore, and FusionCore against
robot_localization) with one consistent, current measurement.

## What runs

All twelve NCLT sequences, the corrected player (`nclt_player.py` publishes the IMU
orientation with `orientation_covariance[0] = -1`, so neither filter fuses the
accelerometer-derived tilt), one sequence at a time, nothing else on the machine.

| Arm | Pipeline | Configuration |
|---|---|---|
| `fc_node` | ROS node, 1x playback (`tools/run_nclt.sh`) | shipped `fusioncore_datasets/config/nclt_fusioncore.yaml` |
| `rl_node` | ROS node, same playback as `fc_node` | `fusioncore_datasets/config/rl_ekf.yaml` + `navsat_transform.yaml`, copied with their sha256 into the run directory |
| `fc_replay` | deterministic replay (`hardware/replay.cpp`) | shipped config, no orientation fused |
| `fc_reanchor` | deterministic replay | shipped config plus `gnss.reanchor_enabled: true`, every other key at its default |

The re-anchoring arm runs through replay because a second node playback per sequence would
take about 33 hours. With the corrected player the node fuses no orientation, so plain replay
is the node-faithful pipeline. That is checked, not assumed: `fc_node` and `fc_replay` are the
same configuration on the same input, and their agreement is reported per sequence.

## What is reported

Per sequence and per metric, every row tagged by `tools/score_nclt_suite.py` with its content
hash, pipeline, scoring points and coverage:

- full trajectory: ATE RMSE 3D, median, truth-rate path ratio, output-rate path ratio (jitter)
- at GPS fix times: ATE RMSE 3D and median, with raw GPS, all columns on identical timestamps

## What is NOT reported

- **No aggregate and no "winner".** No mean across sequences, no win count, no "better
  overall", no "more robust". The table is per sequence and per metric, and the reader can see
  where each filter does better and worse.
- No sequence is dropped for its result. A sequence is excluded only if a run fails its
  coverage check (under 99% of the sequence's GPS-fix span) or its node run is starved (the
  harness's own "filter rate" verdict). Every exclusion is stated with its reason.
- No re-run of a sequence because its number looks wrong. A run is repeated only if it failed
  a mechanical check above, and then the repeat replaces it with the failure noted.

## When the README changes

Only after all twelve sequences have run, from this run alone, with the scorer version and
pipeline stated beside the tables.
