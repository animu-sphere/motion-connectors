#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Replay the device sessions the recorded manifests describe.

A session that cannot be redistributed leaves no bytes in the repository,
only a manifest row of measured facts (docs/architecture/WORKSPACE.md §5).
An operator who kept the bytes can point this script at them, and it checks
that the tools in this tree still read every one the way its row says:

  1. each row's capture is found under --sessions by its sha256, so the
     directory's layout and file names do not matter;
  2. the connector's record tool, run with --inspect, reproduces the row's
     receive statistics, its length census and, for VRChat OSC, its
     per-address message counts; an operator's saved `<capture>.inspect.txt`
     beside the capture must match the new output byte for byte;
  3. the diagnostic codes the replay raises are exactly the row's
     expectedDiagnostics;
  4. for mocopi, --export-trace reproduces the delivered frames, the sender
     rate and the hips path of each session in the capture;
  5. `motion_connect inspect` delivers through the shared connector contract
     as many frames as the record tool did (mocopi), or one or more frames on
     the row's profile (VRChat OSC).

A row whose bytes are not under --sessions is reported and skipped, unless
--require-all is given. Nothing here runs in CI, because CI has no bytes.

  check_recorded_sessions.py --sessions DIR --mocopi-record PATH
      --vrchat-osc-record PATH --motion-connect PATH [--require-all]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parents[1]
CONNECTORS = {
    "mocopi": REPO / "libs" / "motionConnectorMocopi" / "tests" / "corpus" / "recorded",
    "vrchat-osc": REPO / "libs" / "motionConnectorVrchatOsc" / "tests" / "corpus" / "recorded",
}
PROFILES = {"mocopi": "mocopi.body.v1", "vrchat-osc": "vrchat-osc.trackers.v1"}

RECEIVED = re.compile(r"^received:\s+(\d+) datagram\(s\), (\d+) byte\(s\) over (\S+) s$")
PEERS = re.compile(r"^peers:\s+(\d+)")
ARRIVAL = re.compile(r"^arrival:\s+(\S+) Hz mean, interval (\S+)-(\S+) s")
ADDRESS = re.compile(r"^  (/\S+) \S+\s+(\d+) message\(s\)")
DIAGNOSTIC = re.compile(r"\[([A-Z][A-Z0-9_]+)\]")
WROTE = re.compile(r"wrote (\d+) delivered frame\(s\)(?: of session \d+ of \d+)? "
                   r"over (\S+) s at (\S+) Hz")
HIPS = re.compile(r"carries (\S+) m of hips path \((\S+) m net\)")
FRAMES = re.compile(r"^frames: (\d+)$", re.MULTILINE)


def run(command: list) -> subprocess.CompletedProcess:
    return subprocess.run([str(c) for c in command], text=True, encoding="utf-8",
                          errors="replace", stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE)


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def index_sessions(root: pathlib.Path) -> dict[str, pathlib.Path]:
    found: dict[str, pathlib.Path] = {}
    for path in sorted(root.rglob("*packets")):
        if path.is_file():
            found.setdefault(sha256(path), path)
    return found


def manifests() -> list[tuple[str, pathlib.Path]]:
    listed = []
    for connector, recorded in CONNECTORS.items():
        for path in sorted([*recorded.glob("manifest.json"),
                            *recorded.glob("manifests/*.json")]):
            listed.append((connector, path))
    return listed


class Row:
    def __init__(self, label: str):
        self.label = label
        self.errors: list[str] = []

    def expect(self, field: str, want, got) -> None:
        if isinstance(want, float) or isinstance(got, float):
            same = got is not None and float(want) == float(got)
        else:
            same = want == got
        if not same:
            self.errors.append(f"{field}: manifest {want!r}, replay {got!r}")


def check_inspect_output(row: Row, measured: dict, output: str) -> None:
    lines = output.splitlines()
    for pattern in (RECEIVED, ARRIVAL):
        if not any(pattern.match(line) for line in lines):
            row.errors.append(f"--inspect printed no line matching {pattern.pattern}")
    for line in lines:
        if match := RECEIVED.match(line):
            row.expect("datagrams", measured["datagrams"], int(match[1]))
            row.expect("payloadBytes", measured["payloadBytes"], int(match[2]))
            row.expect("receiveSeconds", measured["receiveSeconds"], float(match[3]))
        elif match := PEERS.match(line):
            want = measured.get("peersOnReplay", measured.get("peers"))
            if want is not None:
                row.expect("peers", want, int(match[1]))
        elif match := ARRIVAL.match(line):
            row.expect("arrivalHzMean", measured["arrivalHzMean"], float(match[1]))
            row.expect("arrivalIntervalSeconds", measured["arrivalIntervalSeconds"],
                       [float(match[2]), float(match[3])])
    if "lengthCensus" in measured:
        census = [line.strip() for line in lines
                  if re.fullmatch(r"\s+\d+ of \d+ byte\(s\).*", line)]
        row.expect("lengthCensus", measured["lengthCensus"],
                   census[0] if census else None)
    if "messagesPerAddress" in measured:
        counts = {match[1]: int(match[2]) for line in lines
                  if (match := ADDRESS.match(line))}
        row.expect("messagesPerAddress", measured["messagesPerAddress"], counts)


def check_mocopi(row: Row, entry: dict, capture: pathlib.Path,
                 tools: argparse.Namespace, work: pathlib.Path) -> int | None:
    measured = entry["measured"]
    sessions = measured.get("perSession") or [{
        "session": None, "frames": measured["framesDelivered"],
        "senderHz": measured.get("senderHz"),
        "exportSpanSeconds": measured.get("exportSpanSeconds"),
        "hipsPathMetres": measured.get("hipsPathMetres"),
        "hipsNetMetres": measured.get("hipsNetMetres"),
    }]
    codes: set[str] = set()
    for session in sessions:
        command = [tools.mocopi_record, "--inspect", capture,
                   "--export-trace", work / "session.trace"]
        name = "export"
        if session["session"] is not None:
            command += ["--source-session", session["session"]]
            name = f"session {session['session']}"
        result = run(command)
        if result.returncode != 0:
            row.errors.append(f"{name}: mocopi_record exited {result.returncode}: "
                              f"{result.stderr.strip()}")
            continue
        check_inspect_output(row, measured, result.stdout)
        codes.update(DIAGNOSTIC.findall(result.stderr))
        wrote = WROTE.search(result.stderr)
        hips = HIPS.search(result.stderr)
        row.expect(f"{name} frames", session["frames"], int(wrote[1]) if wrote else None)
        for field, value in (("exportSpanSeconds", wrote and wrote[2]),
                             ("senderHz", wrote and wrote[3]),
                             ("hipsPathMetres", hips and hips[1]),
                             ("hipsNetMetres", hips and hips[2])):
            if session.get(field) is not None:
                row.expect(f"{name} {field}", session[field],
                           float(value) if value else None)
    row.expect("expectedDiagnostics", sorted(entry["expectedDiagnostics"]),
               sorted(codes))
    return measured["framesDelivered"]


def check_vrchat_osc(row: Row, entry: dict, capture: pathlib.Path,
                     tools: argparse.Namespace) -> None:
    result = run([tools.vrchat_osc_record, "--inspect", capture])
    if result.returncode != 0:
        row.errors.append(f"vrchat_osc_record exited {result.returncode}: "
                          f"{result.stderr.strip()}")
        return
    check_inspect_output(row, entry["measured"], result.stdout)
    row.expect("expectedDiagnostics", sorted(entry["expectedDiagnostics"]),
               sorted(set(DIAGNOSTIC.findall(result.stderr))))
    for saved in (capture.with_name(capture.name + ".inspect.txt"),
                  capture.with_suffix(".inspect.txt")):
        if saved.is_file():
            if saved.read_text(encoding="utf-8").replace("\r\n", "\n") != \
                    result.stdout.replace("\r\n", "\n"):
                row.errors.append(f"--inspect output differs from {saved.name}")
            break


def check_motion_connect(row: Row, connector: str, capture: pathlib.Path,
                         tools: argparse.Namespace, frames: int | None) -> None:
    result = run([tools.motion_connect, "inspect", "--source", connector,
                  "--capture", capture])
    if result.returncode != 0:
        row.errors.append(f"motion_connect inspect exited {result.returncode}: "
                          f"{result.stderr.strip()}")
        return
    match = FRAMES.search(result.stdout)
    delivered = int(match[1]) if match else None
    if frames is not None:
        row.expect("motion_connect frames", frames, delivered)
    elif not delivered:
        row.errors.append("motion_connect inspect delivered no frame")
    if f"profile={PROFILES[connector]}" not in result.stdout and delivered:
        row.errors.append(f"motion_connect frames do not carry {PROFILES[connector]}")


def main() -> int:
    sys.stdout.reconfigure(errors="backslashreplace")
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--sessions", required=True, type=pathlib.Path,
                        help="the operator's directory holding the captures")
    parser.add_argument("--mocopi-record", required=True, type=pathlib.Path)
    parser.add_argument("--vrchat-osc-record", required=True, type=pathlib.Path)
    parser.add_argument("--motion-connect", required=True, type=pathlib.Path)
    parser.add_argument("--require-all", action="store_true",
                        help="fail when a manifest row's capture is not found")
    tools = parser.parse_args()

    if not tools.sessions.is_dir():
        print(f"{tools.sessions} is not a directory", file=sys.stderr)
        return 1
    found = index_sessions(tools.sessions)
    checked = skipped = failed = 0
    with tempfile.TemporaryDirectory(prefix="motionconnectors-sessions-") as scratch:
        work = pathlib.Path(scratch)
        for connector, manifest in manifests():
            rows = json.loads(manifest.read_text(encoding="utf-8"))["sessions"]
            for entry in rows:
                row = Row(f"{connector} {entry['file']}")
                capture = found.get(entry["sha256"])
                if capture is None:
                    print(f"skip  {row.label}: no capture with sha256 "
                          f"{entry['sha256'][:12]}… under --sessions")
                    skipped += 1
                    continue
                if connector == "mocopi":
                    frames = check_mocopi(row, entry, capture, tools, work)
                else:
                    check_vrchat_osc(row, entry, capture, tools)
                    frames = None
                check_motion_connect(row, connector, capture, tools, frames)
                checked += 1
                if row.errors:
                    failed += 1
                    print(f"FAIL  {row.label}")
                    for error in row.errors:
                        print(f"        {error}")
                else:
                    print(f"ok    {row.label}")
    print(f"{checked} session(s) replayed, {failed} failed, {skipped} skipped")
    if failed or (tools.require_all and skipped) or checked == 0:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
