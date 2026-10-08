#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Raw capture evidence against independent loopback senders and corpus replay."""

from __future__ import annotations

import argparse
import pathlib
import socket
import struct
import subprocess
import tempfile
import time

from test_bridge import Client, endpoint, inspect, process, signature


def read_capture(path):
    records, peer, expected, data, timestamp = [], "", None, bytearray(), None
    lines = path.read_text(encoding="utf-8").splitlines()
    for line in lines:
        if line.startswith("p "):
            peer = line[2:] if line[2:] != "-" else ""
        elif line.startswith("d "):
            if timestamp is not None:
                assert len(data) == expected
                records.append((timestamp, saved_peer, bytes(data)))
            _, stamp, count = line.split()
            timestamp, expected, saved_peer = float(stamp), int(count), peer
            data.clear()
        elif line.startswith("  "):
            data.extend(bytes.fromhex(line.split("|", 1)[0]))
    if timestamp is not None:
        assert len(data) == expected
        records.append((timestamp, saved_peer, bytes(data)))
    return lines[0], records


def send_text(client, data):
    # Keep exact bytes: the client used by bridge tests serializes JSON.
    mask = b"test"
    head = bytes([0x81, 0x80 | len(data)]) if len(data) < 126 else (
        bytes([0x81, 0xfe]) + struct.pack("!H", len(data)))
    client.socket.sendall(head + mask + bytes(value ^ mask[i % 4]
                                             for i, value in enumerate(data)))


def record(tool, source, fixture):
    magic, originals = read_capture(fixture)
    # Refused/non-frame traffic must survive too. Split the prelude across
    # two UDP senders to prove p lines preserve peer changes.
    prelude = [b"", b"not a motion packet"]
    payloads = prelude + [record[2] for record in originals]
    with tempfile.TemporaryDirectory() as directory:
        saved = pathlib.Path(directory) / "recorded.packets"
        with process(tool, "record", "--source", source, "--port", "0",
                     "--output", saved, "--duration", "1.5") as session:
            child, lines, output, errors = session
            port = endpoint(lines, "listen:")
            peers = []
            if source == "websocket":
                client = Client(port)
                try:
                    peer = f"127.0.0.1:{client.socket.getsockname()[1]}"
                    for data in payloads:
                        send_text(client, data)
                        peers.append(peer)
                        time.sleep(0.006)
                finally:
                    client.close()
            else:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as first, \
                        socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as second:
                    first.bind(("127.0.0.1", 0))
                    second.bind(("127.0.0.1", 0))
                    previous_time = None
                    for index, data in enumerate(payloads):
                        if index >= len(prelude):
                            timestamp = originals[index - len(prelude)][0]
                            if previous_time is None or timestamp - previous_time > 0.003:
                                # Preserve bursts: VRChat pairs position/rotation
                                # inside a short receive-time assembly window.
                                time.sleep(0.03)
                            previous_time = timestamp
                        sender = first if index == 0 else second
                        sender.sendto(data, ("127.0.0.1", port))
                        peers.append(f"127.0.0.1:{sender.getsockname()[1]}")
            assert child.wait(timeout=8) == 0, "".join(errors)
        text = "".join(output)
        saved_magic, records = read_capture(saved)
        assert saved_magic == magic, saved_magic
        assert [record[2] for record in records] == payloads, len(records)
        assert [record[1] for record in records] == peers
        times = [record[0] for record in records]
        assert times == sorted(times) and times[0] >= 0 and times[-1] > times[0]
        assert f"listen 127.0.0.1:{port}" in saved.read_text()
        assert f"records: {len(payloads)}\n" in text, text
        assert not any(line.startswith("frame ") for line in output), text
        actual = inspect(tool, source, saved)
        expected = inspect(tool, source, fixture)
        assert list(map(signature, actual)) == list(map(signature, expected)), (actual, expected)
        assert "[" in "".join(errors), "refused input produced no diagnostic"
    print(f"record {source}: verbatim input, peers, times, refused payloads and replay parity")


def silent(tool, source):
    with tempfile.TemporaryDirectory() as directory:
        saved = pathlib.Path(directory) / "empty.packets"
        result = subprocess.run([str(tool), "record", "--source", source, "--port", "0",
                                 "--output", str(saved), "--duration", "0.01"],
                                capture_output=True, text=True, encoding="utf-8", timeout=5)
        assert result.returncode == 0, result.stderr
        assert "records: 0\n" in result.stdout and "frames: 0\n" in result.stdout
        assert read_capture(saved)[1] == []
        # The existing strict format reader refuses a file without records.
        result = subprocess.run([str(tool), "inspect", "--source", source,
                                 "--capture", str(saved)], capture_output=True, text=True,
                                encoding="utf-8", timeout=5)
        assert result.returncode == 1 and "carries no datagrams" in result.stderr
    print(f"record {source}: silent duration expiry saves a header with zero records")


def limits(tool, fixture):
    _, originals = read_capture(fixture)
    with tempfile.TemporaryDirectory() as directory:
        saved = pathlib.Path(directory) / "limited.packets"
        with process(tool, "record", "--source", "mocopi", "--port", "0",
                     "--output", saved, "--max-frames", "1", "--duration", "5") as session:
            child, lines, output, errors = session
            port = endpoint(lines, "listen:")
            started = time.monotonic()
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
                for _, _, data in originals:
                    sender.sendto(data, ("127.0.0.1", port))
                    time.sleep(0.005)
                    if child.poll() is not None:
                        break
            assert child.wait(timeout=5) == 0, "".join(errors)
            assert time.monotonic() - started < 3, "max-frames did not stop recording"
        assert "frames: 1\n" in "".join(output), output
        assert len(inspect(tool, "mocopi", saved)) == 1
    print("record: frame limit stops promptly and saves the raw input")


def errors(tool):
    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind(("127.0.0.1", 0))
            saved = root / "untouched.packets"
            saved.write_bytes(b"previous capture")
            result = subprocess.run([str(tool), "record", "--source", "vmc", "--port",
                                     str(reservation.getsockname()[1]), "--output", str(saved)],
                                    capture_output=True, timeout=5)
            assert result.returncode == 1 and saved.read_bytes() == b"previous capture"
        for destination in (root, root / "missing" / "capture.packets"):
            result = subprocess.run([str(tool), "record", "--source", "vmc", "--port", "0",
                                     "--output", str(destination)], capture_output=True, timeout=5)
            assert result.returncode == 1 and b"could not open packet capture" in result.stderr
    print("record: source open and output errors fail before waiting for live input")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", type=pathlib.Path, required=True)
    parser.add_argument("--mode", choices=("record", "silent", "limits", "errors"), required=True)
    parser.add_argument("--source")
    parser.add_argument("--capture", type=pathlib.Path)
    args = parser.parse_args()
    if args.mode == "record":
        record(args.tool, args.source, args.capture)
    elif args.mode == "silent":
        silent(args.tool, args.source)
    elif args.mode == "limits":
        limits(args.tool, args.capture)
    else:
        errors(args.tool)


if __name__ == "__main__":
    main()
