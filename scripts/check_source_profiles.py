#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Validate the installed source profiles owned by the three connectors."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys

PROFILE_ROOT = pathlib.Path(__file__).resolve().parents[1]
PROFILE_FILES = {
    "vmc.v1": PROFILE_ROOT / "libs/motionConnectorVmc/profiles/vmc.v1.json",
    "mocopi.body.v1": PROFILE_ROOT / "libs/motionConnectorMocopi/profiles/mocopi.body.v1.json",
    "vrchat-osc.trackers.v1": PROFILE_ROOT / "libs/motionConnectorVrchatOsc/profiles/vrchat-osc.trackers.v1.json",
}

VMC_SOURCE_JOINTS = [
    "Hips", "Spine", "Chest", "UpperChest", "Neck", "Head", "LeftEye",
    "RightEye", "Jaw", "LeftUpperLeg", "LeftLowerLeg", "LeftFoot",
    "LeftToes", "RightUpperLeg", "RightLowerLeg", "RightFoot", "RightToes",
    "LeftShoulder", "LeftUpperArm", "LeftLowerArm", "LeftHand",
    "RightShoulder", "RightUpperArm", "RightLowerArm", "RightHand",
    "LeftThumbProximal", "LeftThumbIntermediate", "LeftThumbDistal",
    "LeftIndexProximal", "LeftIndexIntermediate", "LeftIndexDistal",
    "LeftMiddleProximal", "LeftMiddleIntermediate", "LeftMiddleDistal",
    "LeftRingProximal", "LeftRingIntermediate", "LeftRingDistal",
    "LeftLittleProximal", "LeftLittleIntermediate", "LeftLittleDistal",
    "RightThumbProximal", "RightThumbIntermediate", "RightThumbDistal",
    "RightIndexProximal", "RightIndexIntermediate", "RightIndexDistal",
    "RightMiddleProximal", "RightMiddleIntermediate", "RightMiddleDistal",
    "RightRingProximal", "RightRingIntermediate", "RightRingDistal",
    "RightLittleProximal", "RightLittleIntermediate", "RightLittleDistal",
]

MOCOPI_UNMAPPED = {"bnid:1", "bnid:3", "bnid:5", "bnid:6", "bnid:9"}
VRCHAT_TRACKERS = {str(index) for index in range(1, 9)} | {"head"}
PROFILE_ID_PATTERN = re.compile(r"^[a-z0-9-]+(?:\.[a-z0-9-]+)*\.v[0-9]+$")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def display_path(path: pathlib.Path) -> str:
    try:
        return str(path.relative_to(PROFILE_ROOT))
    except ValueError:
        return str(path)


def load_profile(profile_id: str, path: pathlib.Path) -> dict:
    require(path.is_file(), f"{profile_id}: missing {display_path(path)}")
    try:
        profile = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"{profile_id}: invalid JSON: {error}") from error
    require(isinstance(profile, dict), f"{profile_id}: root must be an object")
    require(profile.get("schema") == "openstrata.motion.source-profile/v1",
            f"{profile_id}: unexpected schema")
    require(profile.get("id") == profile_id, f"{profile_id}: id does not match the file contract")
    require(PROFILE_ID_PATTERN.fullmatch(profile_id) is not None,
            f"{profile_id}: invalid profile identifier")
    for field in ("source", "connector", "part", "observationKind", "jointSet",
                  "basis", "confidence", "capabilities", "clock"):
        require(field in profile, f"{profile_id}: missing {field}")
    require(isinstance(profile["jointSet"], list), f"{profile_id}: jointSet must be an array")
    require(isinstance(profile["capabilities"], list), f"{profile_id}: capabilities must be an array")
    require(len(profile["capabilities"]) == len(set(profile["capabilities"])),
            f"{profile_id}: duplicate capability")
    basis = profile["basis"]
    require(isinstance(basis, dict), f"{profile_id}: basis must be an object")
    for field in ("handedness", "upAxis", "forwardAxis", "lengthUnit",
                  "rotation", "transformSpace", "evidence"):
        require(field in basis, f"{profile_id}: basis missing {field}")
    require(basis["evidence"] in {"measured", "documented", "assumed"},
            f"{profile_id}: invalid evidence value")
    confidence = profile["confidence"]
    require(confidence == {"kind": "none"},
            f"{profile_id}: imported source confidence must be explicitly none")
    return profile


def check_hierarchy(profile_id: str, joints: list[dict]) -> None:
    sources = [joint.get("source") for joint in joints]
    require(all(isinstance(source, str) and source for source in sources),
            f"{profile_id}: every joint needs a source identifier")
    require(len(sources) == len(set(sources)), f"{profile_id}: duplicate source joint")
    positions = {source: index for index, source in enumerate(sources)}
    for index, joint in enumerate(joints):
        parent = joint.get("parent")
        if parent is not None:
            require(parent in positions, f"{profile_id}: unknown parent {parent}")
            require(positions[parent] < index,
                    f"{profile_id}: parent {parent} must precede {joint['source']}")


def check_vmc(profile: dict) -> None:
    profile_id = profile["id"]
    joints = profile["jointSet"]
    require(profile["connector"] == "motionConnectorVmc", f"{profile_id}: connector mismatch")
    require(profile["observationKind"] == "pose", f"{profile_id}: observation kind mismatch")
    require([joint.get("source") for joint in joints] == VMC_SOURCE_JOINTS,
            f"{profile_id}: source joint order does not match VMC HumanBodyBones")
    check_hierarchy(profile_id, joints)
    require(sum(joint.get("humanJoint") is not None for joint in joints) == 55,
            f"{profile_id}: expected 55 mapped joints")
    require(joints[25]["humanJoint"] == "leftThumbMetacarpal" and
            joints[26]["humanJoint"] == "leftThumbProximal" and
            joints[40]["humanJoint"] == "rightThumbMetacarpal" and
            joints[41]["humanJoint"] == "rightThumbProximal",
            f"{profile_id}: thumb mapping does not preserve the source exception")
    require(set(profile["capabilities"]) ==
            {"body", "hands", "face", "root-motion", "source-timestamps"},
            f"{profile_id}: capability set mismatch")
    require(profile["basis"]["handedness"] == "left" and
            profile["basis"]["rotation"]["componentOrder"] == "x,y,z,w",
            f"{profile_id}: basis does not describe VMC")
    require(profile["clock"]["timestamp"] == "/VMC/Ext/T" and
            profile["clock"]["sequence"] is None,
            f"{profile_id}: clock does not describe the VMC source clock")
    channels = profile.get("channels")
    require(isinstance(channels, dict) and channels.get("namespace") == "vmc",
            f"{profile_id}: blend-shape channel namespace is missing")


def check_mocopi(profile: dict) -> None:
    profile_id = profile["id"]
    joints = profile["jointSet"]
    require(profile["connector"] == "motionConnectorMocopi", f"{profile_id}: connector mismatch")
    require(profile["observationKind"] == "pose", f"{profile_id}: observation kind mismatch")
    require(len(joints) == 27, f"{profile_id}: expected 27 measured joints")
    require([joint.get("sourceIndex") for joint in joints] == list(range(27)),
            f"{profile_id}: source indexes must be bnid 0..26")
    check_hierarchy(profile_id, joints)
    require({joint["source"] for joint in joints if joint.get("humanJoint") is None} ==
            MOCOPI_UNMAPPED, f"{profile_id}: unmapped joint set mismatch")
    require(sum(joint.get("humanJoint") is not None for joint in joints) == 22,
            f"{profile_id}: expected 22 mapped joints")
    require(set(profile["capabilities"]) == {"body", "root-motion", "source-timestamps"},
            f"{profile_id}: capability set mismatch")
    require(profile["basis"]["handedness"] == "right" and
            profile["basis"]["rotation"]["componentOrder"] == "x,y,z,w",
            f"{profile_id}: basis does not describe mocopi")
    require(profile["clock"]["timestamp"] == "fram.time" and
            profile["clock"]["sequence"] == "fram.fnum",
            f"{profile_id}: clock does not describe mocopi")


def check_vrchat(profile: dict) -> None:
    profile_id = profile["id"]
    require(profile["connector"] == "motionConnectorVrchatOsc", f"{profile_id}: connector mismatch")
    require(profile["observationKind"] == "trackers", f"{profile_id}: observation kind mismatch")
    require(profile["assignment"] == "external", f"{profile_id}: tracker assignment must remain external")
    require(profile["jointSet"] == [], f"{profile_id}: tracker profile must not invent joints")
    trackers = profile.get("trackers")
    require(isinstance(trackers, list), f"{profile_id}: trackers must be an array")
    require({tracker.get("source") for tracker in trackers} == VRCHAT_TRACKERS,
            f"{profile_id}: tracker identity set mismatch")
    require(all(tracker.get("channels") == ["position", "rotation"] for tracker in trackers),
            f"{profile_id}: every tracker must declare both channels")
    require(set(profile["capabilities"]) == {"trackers"},
            f"{profile_id}: capability set mismatch")
    require(profile["basis"]["rotation"].get("kind") == "euler" and
            profile["basis"]["rotation"].get("order") == "ZXY" and
            profile["basis"]["rotation"].get("unit") == "degrees",
            f"{profile_id}: Euler basis does not describe VRChat OSC")
    require(profile["clock"]["domain"] == "None" and
            profile["clock"]["timestamp"] is None,
            f"{profile_id}: tracker source must declare no source clock")


def check_profiles(prefix: pathlib.Path | None) -> None:
    for profile_id, source_path in PROFILE_FILES.items():
        profile_path = source_path if prefix is None else prefix / "share/motion-connectors/profiles" / source_path.name
        profile = load_profile(profile_id, profile_path)
        if profile_id == "vmc.v1":
            check_vmc(profile)
        elif profile_id == "mocopi.body.v1":
            check_mocopi(profile)
        else:
            check_vrchat(profile)
        print(f"{profile_id}: profile contract passed")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--prefix", type=pathlib.Path,
                        help="validate the files in an installed prefix")
    arguments = parser.parse_args()
    try:
        check_profiles(arguments.prefix.resolve() if arguments.prefix else None)
    except (OSError, ValueError, KeyError) as error:
        print(f"profile check failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
