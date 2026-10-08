#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Loopback bridge evidence, including an independent RFC 6455 client."""

from __future__ import annotations

import argparse
import base64
from contextlib import contextmanager
import hashlib
import json
import pathlib
import queue
import re
import socket
import struct
import subprocess
import threading
import time


@contextmanager
def process(tool, *arguments):
    child = subprocess.Popen([str(tool), *map(str, arguments)], text=True,
                             encoding="utf-8", stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, bufsize=1)
    lines = queue.Queue()
    output, errors = [], []

    def read_stdout():
        for line in child.stdout:
            output.append(line)
            lines.put(line)

    def read_stderr():
        errors.extend(child.stderr)

    readers = [threading.Thread(target=read_stdout), threading.Thread(target=read_stderr)]
    for reader in readers:
        reader.start()
    try:
        yield child, lines, output, errors
    finally:
        if child.poll() is None:
            child.kill()
        child.wait(timeout=5)
        for reader in readers:
            reader.join(timeout=5)
        child.stdout.close()
        child.stderr.close()


def endpoint(lines, prefix):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        line = lines.get(timeout=max(0.01, deadline - time.monotonic()))
        if line.startswith(prefix):
            match = re.search(r"127\.0\.0\.1:(\d+)", line)
            assert match, line
            return int(match[1])
    raise AssertionError(f"no {prefix} endpoint")


def finish(child, output, errors, frames):
    assert child.wait(timeout=10) == 0, "".join(errors)
    # Readers may still be consuming the final lines after wait().
    deadline = time.monotonic() + 2
    while f"refused: 0\n" not in output and time.monotonic() < deadline:
        time.sleep(0.005)
    text = "".join(output)
    assert f"frames: {frames}\n" in text and "refused: 0\n" in text, text
    assert "peer: + " in text, text
    assert not any(line.startswith("frame ") for line in output), text


def inspect(tool, source, capture):
    result = subprocess.run([str(tool), "inspect", "--source", source,
                             "--capture", str(capture)], text=True,
                            encoding="utf-8", errors="replace",
                            capture_output=True, timeout=10, check=True)
    return [line for line in result.stdout.splitlines() if line.startswith("frame ")]


def signature(line):
    return re.sub(r" receive=\S+", "", line)


def replay(tool, source, capture):
    expected = inspect(tool, source, capture)
    assert expected
    with process(tool, "dump", "--source", "websocket", "--port", "0",
                 "--max-frames", len(expected), "--duration", "8") as receiver:
        child, lines, output, errors = receiver
        port = endpoint(lines, "listen:")
        result = subprocess.run([str(tool), "bridge", "--source", source,
                                 "--capture", str(capture), "--ws-connect", "127.0.0.1",
                                 "--ws-port", str(port), "--duration", "6"],
                                text=True, encoding="utf-8", errors="replace",
                                capture_output=True, timeout=10)
        assert result.returncode == 0, result.stderr
        assert f"frames: {len(expected)}\n" in result.stdout, result.stdout
        assert "refused: 0\n" in result.stdout, result.stdout
        assert child.wait(timeout=10) == 0, "".join(errors)
        deadline = time.monotonic() + 2
        while not any(line.startswith("frames:") for line in output) and time.monotonic() < deadline:
            time.sleep(0.005)
        actual = [line.rstrip() for line in output if line.startswith("frame ")]
        assert list(map(signature, actual)) == list(map(signature, expected)), (actual, expected)
    print(f"bridge {source}: dump received all {len(expected)} inspect frames")


class Client:
    def __init__(self, port, path="/", origin=None, status=101):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.socket.settimeout(5)
        self.buffer = bytearray()
        key = base64.b64encode(b"bridge-test-key!").decode()
        headers = [f"GET {path} HTTP/1.1", f"Host: 127.0.0.1:{port}",
                   "Upgrade: websocket", "Connection: Upgrade",
                   f"Sec-WebSocket-Key: {key}", "Sec-WebSocket-Version: 13",
                   "Sec-WebSocket-Protocol: openstrata.motion.frame.v1"]
        if origin is not None:
            headers.append(f"Origin: {origin}")
        self.socket.sendall(("\r\n".join(headers) + "\r\n\r\n").encode())
        while b"\r\n\r\n" not in self.buffer:
            data = self.socket.recv(4096)
            assert data, "EOF during handshake"
            self.buffer.extend(data)
        head, body = bytes(self.buffer).split(b"\r\n\r\n", 1)
        self.buffer = bytearray(body)
        assert head.startswith(f"HTTP/1.1 {status}".encode()), head
        if status == 101:
            fields = dict(line.split(":", 1) for line in head.decode().split("\r\n")[1:])
            fields = {name.lower(): value.strip() for name, value in fields.items()}
            accept = base64.b64encode(hashlib.sha1(
                (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
            assert fields["sec-websocket-accept"] == accept, head
            assert fields["sec-websocket-protocol"] == "openstrata.motion.frame.v1", head

    def close(self):
        self.socket.close()

    def read(self, size):
        while len(self.buffer) < size:
            data = self.socket.recv(65536)
            assert data, "EOF before WebSocket close"
            self.buffer.extend(data)
        result = bytes(self.buffer[:size])
        del self.buffer[:size]
        return result

    def frame(self):
        first, second = self.read(2)
        assert first & 0x80 and not first & 0x70, first
        assert not second & 0x80, "server masked its frame"
        size = second & 0x7f
        if size == 126:
            size = struct.unpack("!H", self.read(2))[0]
        elif size == 127:
            size = struct.unpack("!Q", self.read(8))[0]
        return first & 0x0f, self.read(size)

    def send(self, message):
        data = json.dumps(message, separators=(",", ":")).encode()
        mask = b"test"
        head = bytes([0x81, 0x80 | len(data)]) if len(data) < 126 else (
            bytes([0x81, 0xfe]) + struct.pack("!H", len(data)))
        self.socket.sendall(head + mask + bytes(value ^ mask[i % 4] for i, value in enumerate(data)))


def receive(client):
    messages, times = [], []
    while True:
        opcode, payload = client.frame()
        if opcode == 8:
            assert struct.unpack("!H", payload[:2])[0] == 1001, payload
            break
        assert opcode == 1, opcode
        messages.append(json.loads(payload))
        times.append(time.monotonic())
    return messages, times


def wire_capture(capture):
    records = []
    data = bytearray()
    timestamp = None
    for line in capture.read_text(encoding="utf-8").splitlines():
        if line.startswith("d "):
            if timestamp is not None:
                records.append((timestamp, json.loads(data)))
            timestamp = float(line.split()[1])
            data.clear()
        elif line.startswith("  "):
            data.extend(bytes.fromhex(line.split("|", 1)[0]))
    if timestamp is not None:
        records.append((timestamp, json.loads(data)))
    return records


def listen(tool, source, capture, limit=0):
    expected = inspect(tool, source, capture)
    if limit:
        expected = expected[:limit]
    with process(tool, "bridge", "--source", source, "--capture", capture,
                 "--ws-listen", "127.0.0.1", "--ws-port", "0", "--ws-path", "/motion",
                 "--allow-origin", "https://first.example", "--allow-origin", "https://app.example",
                 "--max-frames", limit, "--duration", "6") as sender:
        child, lines, output, errors = sender
        port = endpoint(lines, "output:")
        refused = Client(port, path="/motion", origin="https://denied.example", status=403)
        refused.close()
        # Without a completed handshake, even the shortest capture must wait.
        time.sleep(0.15)
        assert child.poll() is None, "replay finished before a peer connected"
        client = Client(port, path="/motion", origin="https://app.example")
        try:
            messages, times = receive(client)
        finally:
            client.close()
        assert len(messages) == len(expected), len(messages)
        assert [int(message["frameNumber"]) for message in messages] == list(range(1, len(expected) + 1))
        for message, frame in zip(messages, expected):
            assert message["format"] == "openstrata.motion.frame/v1"
            assert f"profile={message['sourceProfile']} " in frame
            # Bridge retains the acquisition receive time; dump rewrites it.
            timestamp = float(re.search(r"receive=(\S+)", frame)[1])
            assert abs(message["timing"]["receiveTimestamp"] - timestamp) < 0.000002
            assert "state" not in message and "diagnostics" not in message
        if source == "websocket":
            originals = wire_capture(capture)
            for message, (timestamp, original) in zip(messages, originals):
                original["timing"]["receiveTimestamp"] = timestamp
                assert message == original, (message, original)
        if len(times) > 1:
            span = messages[-1]["timing"]["receiveTimestamp"] - messages[0]["timing"]["receiveTimestamp"]
            assert times[-1] - times[0] >= span - 0.03, "replay did not preserve pacing"
        finish(child, output, errors, len(expected))
        assert "[WEBSOCKET_ORIGIN_REFUSED]" in "".join(errors), errors
    print("bridge listener: independent client, origin policy, pacing, numbering and close 1001")


def live(tool):
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    with process(tool, "bridge", "--source", "websocket", "--port", port,
                 "--ws-port", "0", "--duration", "1.2") as sender:
        child, lines, output, errors = sender
        output_port = endpoint(lines, "output:")
        downstream = Client(output_port)
        upstream = Client(port)
        original = {"format": "openstrata.motion.frame/v1", "frameNumber": "41",
                    "sourceProfile": "example.trackers.v1",
                    "timing": {"receiveTimestamp": 123, "sourceClock": "device",
                               "sourceTimestamp": 4.5, "sequence": "9"},
                    "actors": [{"actor": "person", "trackers": [{"trackerId": "head",
                                "position": [1, 2, 3], "confidence": 0.5}]}]}
        try:
            upstream.send(original)
            # Keep the upstream connected and silent: no duplicate or placeholder.
            messages, _ = receive(downstream)
        finally:
            upstream.close()
            downstream.close()
        assert len(messages) == 1, messages
        message = messages[0]
        assert message["frameNumber"] == "1" and message["timing"]["receiveTimestamp"] != 123
        message["frameNumber"] = original["frameNumber"]
        message["timing"]["receiveTimestamp"] = 123
        assert message == original, message
        finish(child, output, errors, 1)
        assert "state: connected\n" in output, output
    print("live bridge: observations preserved, silence stays quiet, duration closes peers")


def errors(tool):
    with socket.socket() as occupied:
        occupied.bind(("127.0.0.1", 0))
        occupied.listen()
        port = occupied.getsockname()[1]
        cases = [
            (["bridge", "--source", "vmc", "--capture", "absent-fixture.vmcpackets",
              "--ws-port", "0"], "motion_connect:"),
            (["bridge", "--source", "websocket", "--port", str(port),
              "--ws-port", "0"], "[WEBSOCKET_SOCKET_BIND_FAILED]"),
            (["bridge", "--source", "vmc", "--port", "0",
              "--ws-port", str(port)], "[WEBSOCKET_SOCKET_BIND_FAILED]"),
            (["bridge", "--source", "vmc", "--port", "0", "--ws-connect", "localhost",
              "--ws-port", str(port)], "not a numeric address"),
        ]
        for arguments, error in cases:
            result = subprocess.run([str(tool), *arguments], text=True, encoding="utf-8",
                                    errors="replace", capture_output=True, timeout=5)
            assert result.returncode == 1 and error in result.stderr, result
    result = subprocess.run([str(tool), "bridge", "--source", "vmc", "--port", "0",
                             "--ws-port", "0", "--duration", "0.05"],
                            text=True, encoding="utf-8", capture_output=True, timeout=5)
    assert result.returncode == 0 and "frames: 0\n" in result.stdout, result
    print("bridge errors: unreadable capture and source/sender bind failures; quiet duration exit")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", type=pathlib.Path, required=True)
    parser.add_argument("--mode", choices=("replay", "listen", "live", "limits", "errors"), required=True)
    parser.add_argument("--source")
    parser.add_argument("--capture", type=pathlib.Path)
    args = parser.parse_args()
    if args.mode == "live":
        live(args.tool)
    elif args.mode == "errors":
        errors(args.tool)
    elif args.mode == "limits":
        listen(args.tool, args.source, args.capture, limit=1)
    elif args.mode == "listen":
        listen(args.tool, args.source, args.capture)
    else:
        replay(args.tool, args.source, args.capture)


if __name__ == "__main__":
    main()
