#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Author the WebSocket connector's two corpora (WEBSOCKET_CONNECTOR.md §9).

**framing/** -- byte streams for the RFC 6455 layer, one per row of §5's table
and per handshake refusal in §4. Each `.wsstream` file is what one connection
received, in the transport's packet-capture format under the magic
`!websocket-stream-capture`, one record per read: a stream split across
records is reassembled across reads. `motionConnectorWebSocket_framingCorpus`
drives the socket-free session with each stream and compares against
`expectations.txt`, tab-separated:

    <file> <side> <messages> <pongs> <result> <code> <status> <byte>

`result` is `open`, `closed` (the peer's close, echoed), `refused` (the
handshake; `status` is the HTTP status sent or received, 0 for none) or
`fault` (a framing error; `status` is the close status sent and `byte` the
offset it names, counted from the connection's first byte). The session's
options are fixed for the whole corpus: path `/`, allowed origin
`https://studio.example`, a 1024-byte message bound (configurable lower,
never higher), and the client key from RFC 6455 §1.3.

**messages/** -- packet captures of messages, `!websocket-packet-capture`,
replayed through `WebSocketConnector::PushMessage` by
`motionConnectorWebSocket_messageCorpus` for §7's restart, gap, regression and
profile rows. `expectations.txt`, tab-separated:

    capture    <file> <profile or -> <frames delivered>
    diagnostic <file> <CODE> <subject>            (in order, every one)

Every byte here is written by this file, independently of the C++ encoder and
session, so the corpus compares two implementations rather than one with
itself. Run:

    python libs/motionConnectorWebSocket/tools/generate_corpus.py

and --check to fail instead of writing when the committed corpus differs.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import pathlib
import struct
import sys

BYTES_PER_LINE = 16
HEX_COLUMN_WIDTH = BYTES_PER_LINE * 3 - 1

STREAM_MAGIC = "!websocket-stream-capture"
MESSAGE_MAGIC = "!websocket-packet-capture"

SUBPROTOCOL = "openstrata.motion.frame.v1"
# RFC 6455 §1.3's sample nonce, so the accept key below can be checked by eye.
CLIENT_KEY = "dGhlIHNhbXBsZSBub25jZQ=="
ACCEPT_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
ALLOWED_ORIGIN = "https://studio.example"
MAX_MESSAGE_BYTES = 1024
# RFC 6455 §5.7's sample masking key.
MASK = bytes([0x37, 0xFA, 0x21, 0x3D])


def accept_key(key: str) -> str:
    digest = hashlib.sha1((key + ACCEPT_GUID).encode("ascii")).digest()
    return base64.b64encode(digest).decode("ascii")


# ---------------------------------------------------------------------------
# Rendering -- must equal the transport's C++ writer, character for character
# ---------------------------------------------------------------------------


def gutter(chunk: bytes) -> str:
    return "".join(chr(byte) if 0x20 <= byte <= 0x7E else "." for byte in chunk)


def render(magic: str, header: list[tuple[str, str]],
           records: list[tuple[float, str, bytes]]) -> str:
    out = [f"{magic} 1"]
    for key, value in header:
        if value:
            out.append(f"{key} {value}")
    emitted = ""
    emitted_any = False
    for receive_time, peer, payload in records:
        out.append("")
        if peer != emitted and (emitted_any or peer):
            out.append(f"p {peer if peer else '-'}")
            emitted, emitted_any = peer, True
        out.append(f"d {receive_time:.6f} {len(payload)}")
        for offset in range(0, len(payload), BYTES_PER_LINE):
            chunk = payload[offset:offset + BYTES_PER_LINE]
            hex_column = " ".join(f"{byte:02x}" for byte in chunk)
            out.append(f"  {hex_column.ljust(HEX_COLUMN_WIDTH)}  |{gutter(chunk)}|")
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# Framing
# ---------------------------------------------------------------------------

TEXT, BINARY, CONTINUATION, CLOSE, PING, PONG = 0x1, 0x2, 0x0, 0x8, 0x9, 0xA


def frame(opcode: int, payload: bytes = b"", *, fin: bool = True, masked: bool = True,
          rsv: int = 0, length: tuple[int, int] | None = None,
          body: bool = True) -> bytes:
    """One frame, as a sender writes it -- or deliberately does not.

    `length` overrides the length encoding as (7-bit field, extended value),
    which is how a non-minimal or top-bit length is spelled. `body=False`
    writes the header alone: a refusal §5 makes from the header must not wait
    for a payload that never comes.
    """
    first = (0x80 if fin else 0) | (rsv << 4) | opcode
    mask_bit = 0x80 if masked else 0
    if length is not None:
        field, extended = length
        header = bytes([first, mask_bit | field])
        if field == 126:
            header += struct.pack(">H", extended)
        elif field == 127:
            header += struct.pack(">Q", extended)
    elif len(payload) < 126:
        header = bytes([first, mask_bit | len(payload)])
    elif len(payload) <= 0xFFFF:
        header = bytes([first, mask_bit | 126]) + struct.pack(">H", len(payload))
    else:
        header = bytes([first, mask_bit | 127]) + struct.pack(">Q", len(payload))
    if masked:
        header += MASK
        payload = bytes(byte ^ MASK[i % 4] for i, byte in enumerate(payload))
    return header + (payload if body else b"")


def request(target: str = "/", *, method: str = "GET", origin: str | None = None,
            protocol: str | None = SUBPROTOCOL, version: str = "13",
            key: str = "x3JJHMbDL1EzLkh9GBhXDw==", upgrade: bool = True,
            extra: str = "") -> bytes:
    lines = [f"{method} {target} HTTP/1.1", "Host: 127.0.0.1:8765"]
    if upgrade:
        lines += ["Upgrade: websocket", "Connection: Upgrade"]
    lines += [f"Sec-WebSocket-Key: {key}", f"Sec-WebSocket-Version: {version}"]
    if protocol is not None:
        lines.append(f"Sec-WebSocket-Protocol: {protocol}")
    if origin is not None:
        lines.append(f"Origin: {origin}")
    if extra:
        lines.append(extra)
    return ("\r\n".join(lines) + "\r\n\r\n").encode("ascii")


def response(*, status: str = "101 Switching Protocols", accept: str | None = None,
             protocol: str | None = SUBPROTOCOL) -> bytes:
    lines = [f"HTTP/1.1 {status}", "Upgrade: websocket", "Connection: Upgrade",
             f"Sec-WebSocket-Accept: {accept or accept_key(CLIENT_KEY)}"]
    if protocol is not None:
        lines.append(f"Sec-WebSocket-Protocol: {protocol}")
    return ("\r\n".join(lines) + "\r\n\r\n").encode("ascii")


def close(status: int | None, reason: bytes = b"", *, masked: bool = True) -> bytes:
    payload = b"" if status is None else struct.pack(">H", status) + reason
    return frame(CLOSE, payload, masked=masked)


class Stream:
    """One connection's inbound bytes, the reads they arrive in, and what the
    session must make of them."""

    def __init__(self, side: str = "server") -> None:
        self.side = side
        self.reads: list[bytes] = []
        self.messages = 0
        self.pongs = 0
        self.result = "open"
        self.code = "-"
        self.status = "-"
        self.byte = "-"

    @property
    def length(self) -> int:
        return sum(len(chunk) for chunk in self.reads)

    def read(self, data: bytes) -> "Stream":
        self.reads.append(data)
        return self

    def handshake(self) -> "Stream":
        return self.read(request() if self.side == "server" else response())

    def text(self, message: bytes) -> "Stream":
        self.messages += 1
        return self.read(frame(TEXT, message, masked=self.side == "server"))

    def fault(self, code: str, status: int, data: bytes) -> "Stream":
        """`data` is refused at its first byte."""
        self.byte = str(self.length)
        self.read(data)
        self.result, self.code, self.status = "fault", code, str(status)
        return self

    def refused(self, code: str, status: int) -> "Stream":
        self.result, self.code, self.status = "refused", code, str(status)
        return self

    def closed(self, status: int) -> "Stream":
        self.result, self.code, self.status = "closed", "WEBSOCKET_PEER_DISCONNECTED", str(status)
        return self


PV = "WEBSOCKET_PROTOCOL_VIOLATION"
HANDSHAKE = "WEBSOCKET_HANDSHAKE_REFUSED"


def streams() -> dict[str, Stream]:
    s: dict[str, Stream] = {}

    # -- §4: the opening handshake, as a listener reads it --------------------
    s["handshake-accepted"] = Stream().handshake()
    s["handshake-allowed-origin"] = Stream().read(request(origin=ALLOWED_ORIGIN))
    s["handshake-extension-ignored"] = Stream().read(
        request(extra="Sec-WebSocket-Extensions: permessage-deflate"))
    s["handshake-protocol-among-others"] = Stream().read(
        request(protocol="openstrata.motion.frame.v2, " + SUBPROTOCOL))
    # One head over three reads, the last of which also carries the first
    # frame: the session must find the head's end and hand the rest on.
    head = request()
    split = Stream().read(head[:10]).read(head[10:60])
    split.reads.append(head[60:] + frame(TEXT, b'{"split":true}'))
    split.messages = 1
    s["handshake-split-reads"] = split

    s["refused-origin"] = Stream().read(
        request(origin="https://ads.example")).refused("WEBSOCKET_ORIGIN_REFUSED", 403)
    s["refused-origin-null"] = Stream().read(
        request(origin="null")).refused("WEBSOCKET_ORIGIN_REFUSED", 403)
    s["refused-path"] = Stream().read(request("/other")).refused(HANDSHAKE, 404)
    s["refused-no-subprotocol"] = Stream().read(request(protocol=None)).refused(HANDSHAKE, 400)
    s["refused-other-subprotocol"] = Stream().read(
        request(protocol="openstrata.motion.frame/v1")).refused(HANDSHAKE, 400)
    s["refused-not-upgrade"] = Stream().read(request(upgrade=False)).refused(HANDSHAKE, 400)
    s["refused-method"] = Stream().read(request(method="POST")).refused(HANDSHAKE, 400)
    s["refused-version"] = Stream().read(request(version="8")).refused(HANDSHAKE, 400)
    s["refused-key"] = Stream().read(request(key="c2hvcnQ=")).refused(HANDSHAKE, 400)
    # A head over 8 KiB is dropped unanswered (§4.3).
    s["refused-head-too-large"] = Stream().read(
        request(extra="X-Padding: " + "a" * 8200)[:8300]).refused(HANDSHAKE, 0)

    # -- §5: framing, after a good handshake ----------------------------------
    s["text-message"] = Stream().handshake().text(b'{"hello":"motion"}')
    s["text-split-across-reads"] = Stream().handshake()
    whole = frame(TEXT, b'{"across":"reads"}')
    s["text-split-across-reads"].read(whole[:3]).read(whole[3:9]).read(whole[9:])
    s["text-split-across-reads"].messages = 1

    fragmented = Stream().handshake()
    fragmented.read(frame(TEXT, b'{"frag', fin=False))
    fragmented.read(frame(PING, b"tick"))
    fragmented.read(frame(CONTINUATION, b'mented":', fin=False))
    fragmented.read(frame(CONTINUATION, b"true}"))
    fragmented.messages, fragmented.pongs = 1, 1
    s["fragmented-with-ping"] = fragmented

    s["ping-answered"] = Stream().handshake().read(frame(PING, b"\x00\x01payload"))
    s["ping-answered"].pongs = 1
    s["pong-unsolicited"] = Stream().handshake().read(frame(PONG, b"x")).text(b"{}")
    s["text-utf8"] = Stream().handshake().text('{"name":"モーション"}'.encode("utf-8"))
    s["text-at-bound"] = Stream().handshake().text(b"a" * MAX_MESSAGE_BYTES)
    # A character split between two fragments is one character: UTF-8 is
    # checked on the reassembled message, not per fragment.
    split_character = Stream().handshake()
    split_character.read(frame(TEXT, b'{"a":"\xc3', fin=False))
    split_character.read(frame(CONTINUATION, b'\xa9"}'))
    split_character.messages = 1
    s["utf8-split-across-fragments"] = split_character

    s["close-echoed"] = Stream().handshake().text(b"{}").read(close(1000, b"bye")).closed(1000)
    s["close-going-away"] = Stream().handshake().read(close(1001)).closed(1001)
    s["close-no-status"] = Stream().handshake().read(close(None)).closed(1005)
    s["close-then-ignored"] = Stream().handshake().read(
        close(1000) + frame(TEXT, b"after")).closed(1000)

    def violation(name: str, data: bytes, code: str = PV, status: int = 1002,
                  before: bytes = b"") -> None:
        stream = Stream().handshake()
        if before:
            stream.read(before)
        s[name] = stream.fault(code, status, data)

    violation("binary-message", frame(BINARY, b"\x01\x02"),
              "WEBSOCKET_UNSUPPORTED_MESSAGE", 1003)
    violation("binary-header-only", frame(BINARY, b"\x00" * 40, body=False),
              "WEBSOCKET_UNSUPPORTED_MESSAGE", 1003)
    violation("too-large-declared",
              frame(TEXT, length=(127, 70000), body=False),
              "WEBSOCKET_MESSAGE_TOO_LARGE", 1009)
    violation("too-large-one-over",
              frame(TEXT, b"a" * (MAX_MESSAGE_BYTES + 1), body=False),
              "WEBSOCKET_MESSAGE_TOO_LARGE", 1009)
    violation("too-large-across-fragments", frame(CONTINUATION, b"b" * 600, body=False),
              "WEBSOCKET_MESSAGE_TOO_LARGE", 1009,
              before=frame(TEXT, b"a" * 600, fin=False))
    violation("unmasked-client-frame", frame(TEXT, b"{}", masked=False))
    violation("reserved-bit", frame(TEXT, b"{}", rsv=0x4))
    violation("unknown-opcode", frame(0x3, b"{}"))
    violation("unknown-control-opcode", frame(0xB, b""))
    violation("continuation-without-start", frame(CONTINUATION, b"{}"))
    violation("new-message-before-last-finished", frame(TEXT, b"{}"),
              before=frame(TEXT, b'{"a":', fin=False))
    violation("control-too-long", frame(PING, b"p" * 126, body=False))
    violation("control-fragmented", frame(PING, b"p", fin=False))
    violation("length-16-not-minimal", frame(TEXT, length=(126, 5), body=False))
    violation("length-64-not-minimal", frame(TEXT, length=(127, 200), body=False))
    violation("length-64-top-bit", frame(TEXT, length=(127, 1 << 63), body=False))
    violation("invalid-utf8", frame(TEXT, b'{"a":"\xc3\x28"}'), status=1007)
    violation("invalid-utf8-across-fragments", frame(CONTINUATION, b'\x28"}'), status=1007,
              before=frame(TEXT, b'{"a":"\xc3', fin=False))
    violation("close-one-byte", frame(CLOSE, b"\x03"))
    violation("close-reserved-status", close(1005))
    violation("close-reason-not-utf8", close(1000, b"\xff"), status=1007)

    # -- The client side: what a connect role reads ---------------------------
    s["client-accepted"] = Stream("client").handshake().text(b'{"from":"server"}')
    client_ping = Stream("client").handshake().read(frame(PING, b"x", masked=False))
    client_ping.pongs = 1
    s["client-ping-answered"] = client_ping
    s["client-close-echoed"] = Stream("client").handshake().read(
        close(1000, masked=False)).closed(1000)
    s["client-masked-server-frame"] = Stream("client").handshake().fault(
        PV, 1002, frame(TEXT, b"{}", masked=True))
    s["client-refused-503"] = Stream("client").read(
        b"HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n"
        b"Content-Length: 0\r\n\r\n").refused(HANDSHAKE, 503)
    s["client-wrong-accept"] = Stream("client").read(
        response(accept=accept_key("another key"))).refused(HANDSHAKE, 101)
    s["client-no-subprotocol"] = Stream("client").read(
        response(protocol=None)).refused(HANDSHAKE, 101)
    s["client-extension-not-offered"] = Stream("client").read(
        response().replace(b"\r\n\r\n", b"\r\nSec-WebSocket-Extensions: permessage-deflate"
                                        b"\r\n\r\n")).refused(HANDSHAKE, 101)
    return s


def render_stream(name: str, stream: Stream) -> str:
    records = [(index * 0.001, "", chunk) for index, chunk in enumerate(stream.reads)]
    header = [("sender", "example.synthetic"), ("sourceId", name)]
    return render(STREAM_MAGIC, header, records)


def framing_expectations(all_streams: dict[str, Stream]) -> str:
    lines = ["# Generated by tools/generate_corpus.py; do not edit.",
             "# file\tside\tmessages\tpongs\tresult\tcode\tstatus\tbyte"]
    for name, stream in sorted(all_streams.items()):
        lines.append("\t".join([f"{name}.wsstream", stream.side, str(stream.messages),
                                str(stream.pongs), stream.result, stream.code,
                                stream.status, stream.byte]))
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# Messages
# ---------------------------------------------------------------------------

BODY = "example.body.v1"
TRACKERS = "example.trackers.v1"
FRAME_SECONDS = 1.0 / 60.0


def message(number: int, *, profile: str = BODY, actors=None, clock: str = "device",
            source_time: float | None = None) -> bytes:
    timing: dict = {}
    if source_time is not None:
        timing["sourceTimestamp"] = source_time
    timing["receiveTimestamp"] = round(number * FRAME_SECONDS, 6)
    timing["sequence"] = str(number)
    timing["sourceClock"] = clock
    body = {"format": "openstrata.motion.frame/v1", "frameNumber": str(number),
            "sourceProfile": profile, "timing": timing, "actors": actors or []}
    return json.dumps(body, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def pose_actor(actor: str, timestamp: float) -> dict:
    return {"actor": actor, "pose": {
        "timestamp": timestamp,
        "root": {"worldPosition": [0, 0.9, 0], "worldOrientation": [1, 0, 0, 0]},
        "joints": {"hips": [1, 0, 0, 0], "spine": [0.6, 0.8, 0, 0]},
        "channels": {},
        "metadata": {"kind": "liveCapture", "provider": "example.sender",
                     "protocol": "example", "sourceId": "Example Avatar"}},
        "trackers": []}


def body_message(number: int, timestamp: float | None = None, *, actors=("0",),
                 profile: str = BODY) -> bytes:
    stamp = number * FRAME_SECONDS if timestamp is None else timestamp
    return message(number, profile=profile, source_time=round(stamp, 6),
                   actors=[pose_actor(actor, round(stamp, 6)) for actor in actors])


class Session:
    def __init__(self, source_id: str, profile: str = BODY) -> None:
        self.source_id = source_id
        self.profile = profile
        self.records: list[tuple[float, str, bytes]] = []
        self.diagnostics: list[tuple[str, str]] = []
        self.delivered = 0

    def add(self, peer: str, payload: bytes, *, delivered: bool = True,
            diagnostic: tuple[str, str] | None = None) -> None:
        self.records.append((round(len(self.records) * FRAME_SECONDS, 6), peer, payload))
        if diagnostic:
            self.diagnostics.append(diagnostic)
        if delivered:
            self.delivered += 1


PEER_A = "127.0.0.1:52001"
PEER_B = "127.0.0.1:52002"


def sessions() -> dict[str, Session]:
    out: dict[str, Session] = {}

    steady = Session("steady-60hz")
    for n in range(1, 11):
        steady.add(PEER_A, body_message(n))
    out["steady-60hz"] = steady

    gap = Session("sender-gap")
    for n in (1, 2, 3):
        gap.add(PEER_A, body_message(n))
    gap.add(PEER_A, body_message(6), diagnostic=("WEBSOCKET_FRAME_GAP", "4..5"))
    gap.add(PEER_A, body_message(7))
    gap.add(PEER_A, body_message(9), diagnostic=("WEBSOCKET_FRAME_GAP", "8..8"))
    out["sender-gap"] = gap

    # The sender started over inside one connection: its numbering and its
    # clock both go back, and the timestamp history starts again with it.
    restart = Session("sender-restart")
    for n in range(1, 6):
        restart.add(PEER_A, body_message(n))
    restart.add(PEER_A, body_message(1), diagnostic=("WEBSOCKET_SOURCE_RESTARTED", "1"))
    for n in (2, 3):
        restart.add(PEER_A, body_message(n))
    out["sender-restart"] = restart

    # A new connection after frames is a restart even when the numbering
    # carries on: the peer changed, and that is what the `p` line records.
    reconnect = Session("reconnect")
    for n in (1, 2, 3):
        reconnect.add(PEER_A, body_message(n))
    reconnect.add(PEER_B, body_message(4), diagnostic=("WEBSOCKET_SOURCE_RESTARTED", "4"))
    for n in (5, 6):
        reconnect.add(PEER_B, body_message(n))
    out["reconnect"] = reconnect

    # Two actors; in frame 3 the second one's pose does not advance, so the
    # whole frame is dropped, and frame 4 is judged against frame 2.
    regression = Session("timestamp-regression")
    regression.add(PEER_A, body_message(1, 0.1, actors=("0", "1")))
    regression.add(PEER_A, body_message(2, 0.2, actors=("0", "1")))
    third = json.loads(body_message(3, 0.3, actors=("0", "1")))
    third["actors"][1]["pose"]["timestamp"] = 0.2
    regression.add(PEER_A,
                   json.dumps(third, separators=(",", ":")).encode("utf-8"),
                   delivered=False, diagnostic=("WEBSOCKET_TIMESTAMP_REGRESSION", "1"))
    regression.add(PEER_A, body_message(4, 0.25, actors=("0", "1")))
    regression.add(PEER_A, body_message(5, 0.25, actors=("0",)), delivered=False,
                   diagnostic=("WEBSOCKET_TIMESTAMP_REGRESSION", "0"))
    out["timestamp-regression"] = regression

    mismatch = Session("profile-mismatch")
    mismatch.add(PEER_A, body_message(1))
    mismatch.add(PEER_A, body_message(2, profile=TRACKERS), delivered=False,
                 diagnostic=("WEBSOCKET_PROFILE_MISMATCH", TRACKERS))
    mismatch.add(PEER_A, body_message(3))
    out["profile-mismatch"] = mismatch

    # A refusal by the codec drops one message and nothing else: its code is
    # the codec's, and the session and the numbering carry on.
    refusals = Session("codec-refusals")
    refusals.add(PEER_A, body_message(1))
    refusals.add(PEER_A, b"", delivered=False,
                 diagnostic=("WIRE_MESSAGE_MALFORMED", "byte 0"))
    refusals.add(PEER_A, body_message(2).replace(b"frame/v1", b"frame/v2"), delivered=False,
                 diagnostic=("WIRE_FORMAT_UNKNOWN", "$.format"))
    # The refused message's number was never read, so the next one is a gap.
    refusals.add(PEER_A, body_message(3), diagnostic=("WEBSOCKET_FRAME_GAP", "2..2"))
    out["codec-refusals"] = refusals

    # Any profile is accepted when none is configured, and trackers and
    # several actors are carried as they came.
    trackers = Session("trackers-any-profile", profile="-")
    for n in range(1, 4):
        trackers.add(PEER_A, message(n, profile=TRACKERS, clock="localMonotonic", actors=[
            {"actor": "0", "trackers": [
                {"trackerId": "1", "position": [0.1, 1.2, -0.3], "rotation": [1, 0, 0, 0]},
                {"trackerId": "head", "rotation": [0.6, 0, 0.8, 0], "confidence": 0.5}]},
            {"actor": "1", "trackers": [{"trackerId": "1", "position": [0, 1, 0]}]}]))
    trackers.add(PEER_A, body_message(4))
    out["trackers-any-profile"] = trackers
    return out


def render_session(session: Session) -> str:
    header = [("sender", "example.synthetic"), ("sourceId", session.source_id),
              ("listen", "127.0.0.1:8765")]
    return render(MESSAGE_MAGIC, header, session.records)


def message_expectations(all_sessions: dict[str, Session]) -> str:
    lines = ["# Generated by tools/generate_corpus.py; do not edit."]
    for name, session in sorted(all_sessions.items()):
        file = f"{name}.websocketpackets"
        lines.append("\t".join(["capture", file, session.profile, str(session.delivered)]))
        for code, subject in session.diagnostics:
            lines.append("\t".join(["diagnostic", file, code, subject]))
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------


def main() -> int:
    default_output = pathlib.Path(__file__).resolve().parents[1] / "tests" / "corpus"
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=pathlib.Path, default=default_output,
                        help="corpus directory, holding framing/ and messages/")
    parser.add_argument("--check", action="store_true",
                        help="fail if the committed corpus differs instead of rewriting it")
    args = parser.parse_args()

    files: dict[pathlib.Path, str] = {}
    framing = args.output / "framing"
    all_streams = streams()
    for name, stream in all_streams.items():
        files[framing / f"{name}.wsstream"] = render_stream(name, stream)
    files[framing / "expectations.txt"] = framing_expectations(all_streams)
    messages = args.output / "messages"
    all_sessions = sessions()
    for name, session in all_sessions.items():
        files[messages / f"{name}.websocketpackets"] = render_session(session)
    files[messages / "expectations.txt"] = message_expectations(all_sessions)

    generated = {path.resolve() for path in files}
    drifted: list[str] = []
    for directory, pattern in ((framing, "*.wsstream"), (messages, "*.websocketpackets")):
        for path in directory.glob(pattern) if directory.exists() else ():
            if path.resolve() not in generated:
                drifted.append(f"{path.name} (not generated)")
                if not args.check:
                    path.unlink()

    for path, text in sorted(files.items()):
        existing = path.read_text(encoding="utf-8", newline="") if path.exists() else None
        if args.check:
            if existing != text:
                drifted.append(path.name)
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        if existing != text:
            with open(path, "w", encoding="utf-8", newline="\n") as handle:
                handle.write(text)
            print(f"wrote {path.parent.name}/{path.name}")

    if args.check and drifted:
        print("the committed WebSocket corpus differs from its generator: "
              + ", ".join(sorted(drifted)), file=sys.stderr)
        print("run: python libs/motionConnectorWebSocket/tools/generate_corpus.py",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
