# Platform profiles

A profile is a measured answer to one question: **what can this platform actually
observe?** Estimating a state you cannot observe is worse than not estimating it, and
on NCLT that is worth up to 61%.

These are parameter bundles, not code. Copy the block into your own config, or load
the file and override individual keys after it. Nothing here changes a library
default, because the right answer genuinely differs by platform.

## Why this exists

FusionCore carries 23 states. The textbook GNSS/INS filter carries 15. The extra
eight are where three of its open defects live:

| group | states | problem |
|---|---|---|
| angular velocity `WX,WY,WZ` | 3 | gyro measures `WZ + B_GZ`: one equation, two unknowns |
| acceleration `AX,AY,AZ` | 3 | accel measures `AX + B_AX + gravity`: one equation, three unknowns |
| encoder yaw bias `B_EWZ` | 1 | a third unknown on an equation that already had two |

Measured on FusionCore: `r(WZ, B_GZ) = -0.998`. The pair slides along an unobservable
line at zero measurement cost, and anything integrating `WZ` alone inherits the error.
That is the 81.6% yaw and 80.7% position figures in issue #150.

The literature says the same thing independently. Accelerometer bias is completely
unobservable in straight motion, and for land vehicles the azimuth and its gyro bias
are unobservable in straight motion and under constant acceleration.

## What the profiles are worth

Measured on NCLT at 1x, every robot_localization control holding inside 0.5%, so every
change is attributable:

| sequence | shipped | `ground_robot` | change | verdict was | verdict now |
|---|---|---|---|---|---|
| 2012-06-15 | 82.011 | **32.149** | **-60.80%** | RL +44% | **FC +30%** |
| 2013-04-05 | 14.058 | **9.193** | **-34.61%** | FC +79% | **FC +86%** |
| 2012-08-20 | 155.618 | 157.591 | +1.27% | RL +93% | RL +93% |

2012-06-15 crosses from a loss to a win. 2012-08-20 is about 10x its own 0.13% spread
worse, which is small and real; the profile is not uniformly better and this file is
not going to pretend otherwise.

## Choosing one

- **`ground_robot.yaml`** needs wheel odometry. It freezes the acceleration and bias
  states, which is only safe because the encoder constrains yaw rate directly. Tested
  against a 20 deg/s gyro bias and a 29.7% encoder scale error in simulation and it
  held, but do not use it on a platform without encoders.
- **`ground_robot_flat.yaml`** adds a Z-position constraint for campus, warehouse,
  parking-lot and field terrain. Do not use it where the robot genuinely climbs.
- **No profile** is the right choice for drones, boats, and anything that leaves the
  ground plane. The library defaults stay general on purpose.

## Before you trust it on your robot

Run `tools/bag_report.py` on one of your bags first. If it says your IMU and wheels
disagree about turn direction, or that your bag contains no manoeuvre that makes
heading observable, fix that before changing any of this. A profile cannot rescue a
sensor that is wired backwards.
