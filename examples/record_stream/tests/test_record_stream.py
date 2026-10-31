#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""record_stream against the committed captures and a loopback sender.

The per-connector recorders are the independent oracle: their `--export-trace`
replays source frames through the same trace writer with no intake. So every
line a recorder writes must appear in the take, in order, and what the take adds
must be what the frame path and the intake are responsible for.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import socket
import subprocess
import tempfile

VMC = "libs/motionConnectorVmc/tests/corpus"
MOCOPI = "libs/motionConnectorMocopi/tests/corpus"
WEBSOCKET = "libs/motionConnectorWebSocket/tests/corpus/messages"


def run(*command, expect=0):
    result = subprocess.run([str(part) for part in command], text=True, encoding="utf-8",
                            errors="replace", capture_output=True, timeout=30)
    assert result.returncode == expect, (command, result.stdout, result.stderr)
    return result


def record(args, source, capture, output):
    return run(args.tool, "--source", source, "--capture", args.root / capture,
               "--output", output)


def field(stdout, name):
    return int(re.search(rf"^{name}: (\d+)$", stdout, re.M)[1])


def stat(stdout, name):
    return int(re.search(rf"\b{name}=(\d+)", stdout)[1])


def lines(trace):
    return trace.read_text(encoding="utf-8").splitlines()


def frames(trace):
    return sum(line.startswith("t ") for line in lines(trace))


def check_against_oracle(take, oracle, held=0):
    """Compare a take with a recorder's export of the same session.

    A take adds each sample's stamp, which the connector's frame carries, and the
    root velocity and held joints the intake derived. A recorder adds the
    operator's `provider` and `sourceId` from the capture's header, which are not
    on the wire.
    """
    added = ("sourceTime ", "root vel ")
    header_only = ("provider ", "sourceId ")
    kept = [line for line in lines(take) if not line.startswith(added)]
    wanted = [line for line in lines(oracle) if not line.startswith(added + header_only)]
    remaining = iter(kept)
    missing = [line for line in wanted if line not in remaining]
    assert not missing, (take.name, missing[:5])
    bones = [line for line in kept if line.startswith("b ")]
    assert len(bones) == sum(line.startswith("b ") for line in wanted) + held, take.name
    others = [line for line in kept if not line.startswith(("b ",) + header_only)]
    assert others == [line for line in wanted if not line.startswith("b ")], take.name


def replay(args, work):
    for source, recorder, capture in (
            ("vmc", args.vmc_record, f"{VMC}/arm-raise-30hz.vmcpackets"),
            ("mocopi", args.mocopi_record, f"{MOCOPI}/neutral-standing-60hz.mocopipackets"),
            ("mocopi", args.mocopi_record, f"{MOCOPI}/arms-lowered-60hz.mocopipackets")):
        name = pathlib.Path(capture).stem
        first, second = work / f"{name}-first", work / f"{name}-second"
        result = record(args, source, capture, first)
        record(args, source, capture, second)
        assert field(result.stdout, "dropped") == 0, result.stdout
        trace = first / "actor-0.take-1.trace"
        assert sorted(path.name for path in first.iterdir()) == [trace.name]
        # Two runs of one pipeline diff cleanly.
        assert trace.read_bytes() == (second / trace.name).read_bytes(), name
        assert frames(trace) == field(result.stdout, "frames") > 0, result.stdout
        # Every pose is evaluated where it landed, so nothing was held or invented.
        assert stat(result.stdout, "sampled") == frames(trace), result.stdout
        assert stat(result.stdout, "unavailable") == stat(result.stdout, "refused") == 0

        oracle = work / f"{name}-oracle.trace"
        run(recorder, "--inspect", args.root / capture, "--export-trace", oracle)
        check_against_oracle(trace, oracle)
        # The per-sample stamp survives the intake and reaches the file.
        assert sum(line.startswith("sourceTime ") for line in lines(trace)) == frames(trace)

    # A relay of frames has no recorder of its own, and its actors stay apart.
    result = record(args, "websocket", f"{WEBSOCKET}/steady-60hz.websocketpackets",
                    work / "websocket")
    assert frames(work / "websocket" / "actor-0.take-1.trace") == 10, result.stdout
    result = record(args, "websocket", f"{WEBSOCKET}/timestamp-regression.websocketpackets",
                    work / "actors")
    assert sorted(path.name for path in (work / "actors").iterdir()) == [
        "actor-0.take-1.trace", "actor-1.take-1.trace"], result.stdout
    assert 'id="0"' in result.stdout and 'id="1"' in result.stdout, result.stdout


def restart(args, work):
    # A sender restart takes the clock back, so one capture becomes two takes,
    # each equal to the session its recorder exports under the same number.
    capture = f"{VMC}/sender-restart-30hz.vmcpackets"
    result = record(args, "vmc", capture, work / "vmc")
    traces = sorted((work / "vmc").iterdir())
    assert [path.name for path in traces] == ["actor-0.take-1.trace", "actor-0.take-2.trace"]
    assert sum(map(frames, traces)) == field(result.stdout, "frames"), result.stdout
    assert "VMC_SOURCE_RESTARTED" in result.stderr, result.stderr
    # The one incomplete frame is in the first session, and the intake fills it.
    held = stat(result.stdout, "jointsHeld")
    assert held == 15, result.stdout
    for number, (trace, session_held) in enumerate(zip(traces, (held, 0)), 1):
        oracle = work / f"oracle-{number}.trace"
        run(args.vmc_record, "--inspect", args.root / capture, "--export-trace", oracle,
            "--sender-session", number)
        check_against_oracle(trace, oracle, session_held)

    capture = f"{MOCOPI}/session-restart-60hz.mocopipackets"
    result = record(args, "mocopi", capture, work / "mocopi")
    assert stat(result.stdout, "takes") == 2, result.stdout
    assert "MOCOPI_SOURCE_RESTARTED" in result.stderr, result.stderr

    capture = f"{WEBSOCKET}/sender-restart.websocketpackets"
    record(args, "websocket", capture, work / "websocket")
    assert [frames(path) for path in sorted((work / "websocket").iterdir())] == [5, 3]

    # A reconnect whose clock advances is still reported, and the take continues:
    # that is the stated policy, and the trace stays strictly increasing.
    capture = f"{WEBSOCKET}/reconnect.websocketpackets"
    result = record(args, "websocket", capture, work / "reconnect")
    assert "WEBSOCKET_SOURCE_RESTARTED" in result.stderr, result.stderr
    assert stat(result.stdout, "takes") == 1 and field(result.stdout, "frames") == 6


def intake(args, work):
    # A position with no velocity gains one from the intake: the oracle, which
    # writes acquisition's values, has no such line for this capture.
    capture = f"{MOCOPI}/arms-lowered-60hz.mocopipackets"
    result = record(args, "mocopi", capture, work / "root")
    trace = work / "root" / "actor-0.take-1.trace"
    derived = stat(result.stdout, "velocitiesDerived")
    assert derived == frames(trace) - 1 > 0, result.stdout
    assert sum(line.startswith("root vel ") for line in lines(trace)) == derived, result.stdout
    oracle = work / "oracle.trace"
    run(args.mocopi_record, "--inspect", args.root / capture, "--export-trace", oracle)
    assert not any(line.startswith("root vel ") for line in lines(oracle))


def observations(args, work):
    # Tracker observations are counted, never turned into a pose: only the actor
    # that sent one is recorded.
    output = work / "trackers"
    result = record(args, "websocket", f"{WEBSOCKET}/trackers-any-profile.websocketpackets",
                    output)
    assert field(result.stdout, "frames") == 4, result.stdout
    assert field(result.stdout, "observation-only actors") == 6, result.stdout
    assert [path.name for path in output.iterdir()] == ["actor-0.take-1.trace"], result.stdout
    assert frames(output / "actor-0.take-1.trace") == 1, result.stdout

    # A session with no pose says so and leaves nothing behind.
    silent = work / "silent"
    result = run(args.tool, "--source", "vmc", "--port", "0", "--duration", "0.05",
                 "--output", silent, expect=1)
    assert "no pose" in result.stderr, result.stderr
    assert not list(silent.iterdir())


def arguments(args, work):
    capture = args.root / VMC / "neutral-standing-30hz.vmcpackets"
    output = work / "output"
    for bad in (["--source", "vmc"],
                ["--output", output],
                ["--source", "vrchat-osc", "--output", output],
                ["--source", "vmc", "--output"],
                ["--source", "vmc", "--output", output, "--unknown", "1"],
                ["--source", "websocket", "--output", output],
                ["--source", "vmc", "--output", output, "--port", "70000"],
                ["--source", "vmc", "--output", output, "--port", "-1"],
                ["--source", "vmc", "--output", output, "--duration", "-1"],
                ["--source", "vmc", "--output", output, "--duration", "nan"],
                ["--source", "vmc", "--output", output, "--max-frames", "x"],
                ["--source", "vmc", "--output", output, "--capture", capture, "--port", "0"]):
        run(args.tool, *bad, expect=2)
    assert not output.exists(), "a refused command must not create its output"
    assert "usage:" in run(args.tool, "--help").stdout

    # A take is never lost to, or mixed with, what an earlier session left.
    occupied = work / "occupied"
    occupied.mkdir()
    (occupied / "earlier.trace").write_text("kept", encoding="utf-8")
    result = run(args.tool, "--source", "vmc", "--capture", capture, "--output", occupied,
                 expect=1)
    assert "empty or new" in result.stderr, result.stderr
    assert sorted(path.name for path in occupied.iterdir()) == ["earlier.trace"]

    result = run(args.tool, "--source", "vmc", "--capture", work / "absent", "--output",
                 work / "unread", expect=1)
    assert "absent" in result.stderr, result.stderr


def read_capture(path):
    """The datagrams of a packet capture, in order."""
    records, data = [], None
    for line in lines(path):
        if line.startswith("d "):
            data = bytearray()
            records.append(data)
        elif line.startswith("  ") and data is not None:
            data.extend(bytes.fromhex(line.split("|", 1)[0]))
    return [bytes(record) for record in records]


def loopback(args, work):
    # The live path reaches the same intake: frames sent over a socket are recorded
    # with the same poses as their replay. A live session has no end marker, so the
    # frame still open when the sender stops is not delivered.
    capture = args.root / VMC / "arm-raise-30hz.vmcpackets"
    replayed = work / "replayed"
    expected = field(run(args.tool, "--source", "vmc", "--capture", capture,
                         "--output", replayed).stdout, "frames") - 1
    live = work / "live"
    child = subprocess.Popen(
        [str(args.tool), "--source", "vmc", "--port", "0", "--output", str(live),
         "--max-frames", str(expected), "--duration", "15"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8",
        errors="replace")
    try:
        assert child.stdout.readline().startswith("source: vmc")
        port = int(child.stdout.readline().rsplit(":", 1)[1])
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
            for datagram in read_capture(capture):
                sender.sendto(datagram, ("127.0.0.1", port))
        stdout, stderr = child.communicate(timeout=20)
    finally:
        child.kill()
    assert child.returncode == 0, stderr
    assert field(stdout, "frames") == expected, stdout + stderr
    assert field(stdout, "dropped") == 0, stdout
    trace = live / "actor-0.take-1.trace"
    assert frames(trace) == expected, stdout

    def poses(path):
        # From the first frame on, for `expected` frames: the header's measured
        # rate differs by the frame the live session did not deliver.
        text = [line for line in lines(path) if line]
        opening = [index for index, line in enumerate(text) if line.startswith("t ")]
        return text[opening[0]:opening[expected] if expected < len(opening) else None]

    assert poses(trace) == poses(replayed / trace.name), stdout


MODES = {"replay": replay, "restart": restart, "intake": intake,
         "observations": observations, "arguments": arguments, "loopback": loopback}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--tool", required=True, type=pathlib.Path)
    parser.add_argument("--vmc-record", required=True, type=pathlib.Path)
    parser.add_argument("--mocopi-record", required=True, type=pathlib.Path)
    parser.add_argument("--root", required=True, type=pathlib.Path)
    parser.add_argument("--mode", required=True, choices=sorted(MODES))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="record-stream-") as directory:
        MODES[args.mode](args, pathlib.Path(directory))
    print(f"ok  record_stream {args.mode}")


if __name__ == "__main__":
    main()
