#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check the shared-contract connector inventory and capture replay CLI."""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys


EXPECTED = (
    ("vmc", "vmc.v1", "body, hands, face, root-motion, source-timestamps"),
    ("mocopi", "mocopi.body.v1", "body, root-motion, source-timestamps"),
    ("vrchat-osc", "vrchat-osc.trackers.v1", "trackers"),
    ("websocket", "(the sender's)",
     "body, hands, face, eyes, root-motion, trackers, source-timestamps, confidence, multiple-actors"),
)


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def run_tool(tool: pathlib.Path, *arguments: str) -> str:
    result = subprocess.run(
        [str(tool), *arguments],
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
    )
    if result.returncode != 0:
        fail(f"motion_connect {' '.join(arguments)} exited {result.returncode}: "
             f"{result.stderr}")
    return result.stdout


def check_list(tool: pathlib.Path) -> None:
    lines = [line.strip() for line in run_tool(tool, "list").splitlines() if line.strip()]
    expected_lines = ["connectors:"]
    for name, profile, capabilities in EXPECTED:
        expected_lines.extend(
            [name, f"profile: {profile}", f"capabilities: {capabilities}"]
        )
    if lines != expected_lines:
        fail(f"unexpected inventory:\n{' '.join(lines)}")
    print("motion_connect list: connector profiles and capabilities are stable")


def check_inspect(tool: pathlib.Path, source: str, capture: pathlib.Path,
                  profile: str) -> None:
    output = run_tool(tool, "inspect", "--source", source, "--capture", str(capture))
    if f"source: {profile}" not in output:
        fail(f"{source}: inspect did not report {profile}:\n{output}")
    if not any(line.startswith("frames: ") and int(line.split(":", 1)[1]) > 0
               for line in output.splitlines()):
        fail(f"{source}: inspect did not emit a frame:\n{output}")
    print(f"motion_connect inspect: {source} capture replayed through MotionFrame")


def check_dump(tool: pathlib.Path, source: str) -> None:
    output = run_tool(tool, "dump", "--source", source, "--port", "0",
                      "--duration", "0.01")
    if f"source: " not in output or "frames: 0" not in output:
        fail(f"{source}: dump did not open and stop cleanly:\n{output}")
    if source == "websocket" and "listen: 127.0.0.1:" not in output:
        fail(f"websocket: dump did not print the bound endpoint:\n{output}")
    print(f"motion_connect dump: {source} opened a loopback receiver")


def check_arguments(tool: pathlib.Path) -> None:
    bridge = ["bridge", "--source", "vmc"]
    cases = [
        (["record", "--output", "fixture"], "--source is required"),
        (["record", "--source", "vmc"], "--output is required"),
        (["record", "--source", "vmc", "--output", ""], "--output is required"),
        (["record", "--source", "websocket", "--output", "fixture"], "--port is required"),
        (["record", "--source", "vmc", "--output", "fixture", "--capture", "input"],
         "only valid for inspect and bridge"),
        (["record", "--source", "vmc", "--output", "fixture", "--ws-port", "0"],
         "only valid for bridge"),
        (["dump", "--source", "vmc", "--output", "fixture"], "only valid for bridge and record"),
        (["dump", "--source", "vmc", "--max-frames", "-1"], "whole number"),
        (bridge, "--ws-port is required"),
        (bridge + ["--ws-port", "0", "--ws-listen", "127.0.0.1",
                   "--ws-connect", "127.0.0.1"], "cannot be combined"),
        (bridge + ["--ws-connect", "127.0.0.1", "--ws-port", "0"], "nonzero"),
        (bridge + ["--ws-port", "0", "--output", "udp"], "expects websocket"),
        (bridge + ["--ws-port", "0", "--capture", "fixture", "--listen", "127.0.0.1"],
         "cannot be combined"),
        (bridge + ["--ws-port", "0", "--capture", "fixture", "--port", "0"],
         "cannot be combined"),
        (["dump", "--source", "websocket"], "--port is required"),
        (["bridge", "--source", "websocket", "--ws-port", "0"], "--port is required"),
        (["dump", "--source", "vmc", "--ws-port", "0"], "only valid for bridge"),
        (bridge + ["--ws-port", "65536"], "between 0 and 65535"),
        (bridge + ["--ws-port", "0", "--duration", "nan"], "non-negative"),
    ]
    for arguments, error in cases:
        result = subprocess.run([str(tool), *arguments], text=True, encoding="utf-8",
                                errors="replace", capture_output=True, timeout=10)
        if result.returncode != 2 or error not in result.stderr:
            fail(f"arguments {arguments} were not rejected with {error}:\n"
                 f"exit={result.returncode}\n{result.stderr}")
    print("motion_connect arguments: bridge and source refusals are stable")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", type=pathlib.Path, required=True)
    parser.add_argument("--mode", choices=("list", "inspect", "dump", "arguments"),
                        required=True)
    parser.add_argument("--source")
    parser.add_argument("--capture", type=pathlib.Path)
    parser.add_argument("--profile")
    arguments = parser.parse_args()

    if arguments.mode == "list":
        check_list(arguments.tool)
    elif arguments.mode == "inspect":
        if not arguments.source or not arguments.capture or not arguments.profile:
            fail("inspect requires --source, --capture, and --profile")
        check_inspect(arguments.tool, arguments.source, arguments.capture,
                      arguments.profile)
    elif arguments.mode == "dump":
        if not arguments.source:
            fail("dump requires --source")
        check_dump(arguments.tool, arguments.source)
    else:
        check_arguments(arguments.tool)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
