#!/usr/bin/env python3
"""Platform profiles must only set parameters the node actually declares.

A profile is a YAML bundle a user copies into their config. If a key in it is
misspelled or renamed out from under it, ROS ignores the key ENTIRELY and silently
falls back to the library default. The user sees a profile that loaded without error
and a filter that is not configured the way the profile says it is.

That failure mode is the most repeated bug in this repo: PR #129 parameterised a dead
branch, replay.cpp accepted 43 parameters that never reached the filter, and a rover
params file keyed to the wrong node name silently restored the two worst settings ever
measured on that hardware. A profile is the same shape of risk with a friendlier name.
"""
import os
import pathlib
import sys
import unittest

import yaml

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_config_params as ccp

PROFILES = pathlib.Path(__file__).resolve().parent.parent / \
    "fusioncore_datasets" / "config" / "profiles"


def profile_files():
    return sorted(PROFILES.glob("*.yaml"))


def params(path):
    doc = yaml.safe_load(path.read_text())
    return doc["fusioncore"]["ros__parameters"]


class Profiles(unittest.TestCase):

    def test_there_are_profiles_to_check(self):
        # Guards against the glob silently matching nothing, which would make every
        # other test here pass by doing no work.
        self.assertGreaterEqual(len(profile_files()), 2, f"no profiles found in {PROFILES}")

    def test_every_profile_is_valid_yaml_keyed_to_the_node(self):
        for f in profile_files():
            with self.subTest(profile=f.name):
                doc = yaml.safe_load(f.read_text())
                self.assertIn("fusioncore", doc,
                              "a profile keyed to the wrong node name is ignored entirely")
                self.assertIn("ros__parameters", doc["fusioncore"])

    def test_every_parameter_is_declared_by_the_node(self):
        declared = ccp.declared_parameters(ccp.NODE)
        self.assertGreater(len(declared), 50, "could not read the node's declarations")
        for f in profile_files():
            with self.subTest(profile=f.name):
                undeclared = sorted(k for k in params(f) if k not in declared)
                self.assertEqual([], undeclared,
                                 f"{f.name} sets parameters the node does not declare, "
                                 f"so ROS will ignore them silently: {undeclared}")

    def test_flat_is_a_superset_of_ground_robot(self):
        # The flat profile is documented as "ground_robot plus a Z constraint". If it
        # ever drifts from that, the documentation is lying rather than merely stale.
        base = params(PROFILES / "ground_robot.yaml")
        flat = params(PROFILES / "ground_robot_flat.yaml")
        for k, v in base.items():
            self.assertIn(k, flat, f"ground_robot_flat dropped {k}")
            self.assertEqual(v, flat[k], f"ground_robot_flat changed {k}")
        extra = set(flat) - set(base)
        self.assertEqual({"ground_constraint.z_position_sigma"}, extra,
                         "flat should add exactly the Z constraint and nothing else")

    def test_the_frozen_states_are_actually_frozen(self):
        # The whole point of ground_robot is that these four stop being driven. A
        # non-zero here would make the profile a no-op while still reading like one.
        p = params(PROFILES / "ground_robot.yaml")
        for k in ("ukf.q_acceleration", "ukf.q_accel_bias",
                  "ukf.q_gyro_bias", "ukf.q_encoder_wz_bias"):
            self.assertEqual(0.0, float(p[k]), f"{k} must be 0 for the profile to mean anything")
        # and the priors pinned, or a measurement can still move a q=0 state
        for k in ("imu.gyro_bias_initial_sigma", "imu.accel_bias_initial_sigma",
                  "encoder.wz_scale_initial_sigma"):
            self.assertLess(float(p[k]), 1e-4, f"{k} must be pinned, not merely small")


if __name__ == "__main__":
    unittest.main()
