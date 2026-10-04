#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Author the `openstrata.motion.frame/v1` message corpus.

FRAME_WIRE_FORMAT §6 lists what a reader refuses, and says the generated corpus
tests each refusal. This file writes that corpus: one message per file under
tests/corpus/, and `expectations.txt`, which `motionConnectorWire_corpus` reads.

    canonical <file>                 the C++ writer's own bytes: decode, then
                                     re-encode, must reproduce the file exactly
    accepted  <file>                 valid version 1 the writer would spell
                                     differently (key order, whitespace, escapes)
    refused   <file> <CODE> <subject>

The canonical messages are written here independently of the C++ writer, so
the corpus test compares two implementations of §4's spelling rather than one
implementation with itself. That is why numbers below are tokens, not floats:
the spelling *is* what is under test. A float token has at most six
significant digits, so it is already the shortest decimal that reads back to
the same `float`, and a double token is checked against Python's shortest
`repr`.

Every refused message is a canonical one with one thing wrong, so a refusal
pins the rule it names and nothing else. Run:

    python libs/motionConnectorWire/tools/generate_messages.py

and --check to fail instead of writing when the committed corpus differs.
"""

from __future__ import annotations

import argparse
import copy
import pathlib
import re
import sys

SUFFIX = ".frame.json"
EXPECTATIONS = "expectations.txt"


class Num:
    """A JSON number written exactly as given."""

    def __init__(self, token: str, kind: str = "float") -> None:
        if kind == "float":
            digits = re.sub(r"[^0-9]", "", token.split("e")[0]).lstrip("0")
            assert len(digits) <= 6, f"{token}: a float token has at most six digits"
        elif kind == "double":
            assert repr(float(token)).removesuffix(".0") == token, \
                f"{token}: not the shortest spelling of its double"
        self.token = token


def F(token: str) -> Num:
    return Num(token, "float")


def D(token: str) -> Num:
    return Num(token, "double")


def quat(*components: str) -> list[Num]:
    return [F(c) for c in components]


def vec(*components: str) -> list[Num]:
    return [F(c) for c in components]


IDENTITY = quat("1", "0", "0", "0")


def escape(text: str) -> str:
    """§4.2's string spelling, as the C++ writer escapes it."""
    out = ['"']
    for ch in text:
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif ch == "\b":
            out.append("\\b")
        elif ch == "\f":
            out.append("\\f")
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append("\\t")
        elif ord(ch) < 0x20:
            out.append(f"\\u{ord(ch):04x}")
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def render(value) -> str:
    """Compact JSON, keys in insertion order: the writer's form."""
    if isinstance(value, Num):
        return value.token
    if isinstance(value, str):
        return escape(value)
    if isinstance(value, list):
        return "[" + ",".join(render(v) for v in value) + "]"
    if isinstance(value, dict):
        return "{" + ",".join(escape(k) + ":" + render(v) for k, v in value.items()) + "}"
    if value is None:
        return "null"
    if value is True:
        return "true"
    if value is False:
        return "false"
    raise TypeError(type(value))


# ---------------------------------------------------------------------------
# Canonical messages
# ---------------------------------------------------------------------------


def body_pose() -> dict:
    """FRAME_WIRE_FORMAT §3's example, with neutral names."""
    return {
        "format": "openstrata.motion.frame/v1",
        "frameNumber": "1042",
        "sourceProfile": "example.body.v1",
        "timing": {
            "sourceTimestamp": D("1789795161.0333333"),
            "receiveTimestamp": D("5123.481902"),
            "sequence": "88213",
            "sourceClock": "device",
        },
        "actors": [{
            "actor": "0",
            "pose": {
                "timestamp": D("1789795161.0333333"),
                "root": {
                    "worldPosition": vec("0", "0.9", "0"),
                    "worldOrientation": IDENTITY,
                },
                "joints": {
                    "hips": IDENTITY,
                    "spine": quat("0.6", "0.8", "0", "0"),
                    "leftUpperArm": quat("0.5", "0.5", "-0.5", "0.5"),
                },
                "channels": {"mouth:aa": F("0.25"), "mouth:smile": F("0.75")},
                "metadata": {
                    "kind": "liveCapture",
                    "provider": "example.sender",
                    "protocol": "example",
                    "sourceId": "Example Avatar",
                    "sequenceNumber": "88213",
                },
            },
            "trackers": [],
        }],
    }


def trackers_only() -> dict:
    return {
        "format": "openstrata.motion.frame/v1",
        "frameNumber": "7",
        "sourceProfile": "example.trackers.v1",
        "timing": {
            "receiveTimestamp": D("12.5"),
            "sourceClock": "localMonotonic",
        },
        "actors": [{
            "actor": "0",
            "trackers": [
                {"trackerId": "1", "position": vec("0.1", "1.2", "-0.3"),
                 "rotation": IDENTITY},
                {"trackerId": "2", "position": vec("-0.1", "0.05", "0")},
                {"trackerId": "head", "rotation": quat("0.6", "0", "0.8", "0"),
                 "confidence": F("0.5")},
            ],
        }],
    }


def every_field() -> dict:
    """Every optional key the format has, two actors, and channel names that
    need every escape the writer makes."""
    return {
        "format": "openstrata.motion.frame/v1",
        "frameNumber": "18446744073709551615",
        "sourceProfile": "example.body.v1",
        "timing": {
            "sourceTimestamp": D("100.25"),
            "receiveTimestamp": D("0.001"),
            "sequence": "0",
            "sourceClock": "networkSynchronized",
        },
        "actors": [
            {
                "actor": "performer A",
                "pose": {
                    "timestamp": D("100.25"),
                    "root": {
                        "worldPosition": vec("1.5", "0.95", "-2"),
                        "worldOrientation": quat("0.6", "0", "0.8", "0"),
                        "linearVelocity": vec("0.1", "0", "-0.333333"),
                        "angularVelocity": vec("0", "1.5", "0"),
                    },
                    "joints": {
                        "hips": IDENTITY,
                        "chest": quat("0.6", "0", "0", "0.8"),
                        "head": quat("-1", "0", "0", "0"),
                        "leftHand": quat("0.5", "-0.5", "0.5", "-0.5"),
                        "rightLittleDistal": IDENTITY,
                    },
                    "confidence": {
                        "chest": F("0.75"),
                        "head": F("0"),
                        "hips": F("1"),
                        "leftHand": F("0.1"),
                        "rightLittleDistal": F("0.5"),
                    },
                    "contacts": {"leftFoot": "contact", "rightFoot": "free"},
                    "lookAtTarget": vec("0", "1.6", "2"),
                    "channels": {
                        "a name with a space": F("1.5"),
                        "control \u0001 tab \t newline \n": F("0"),
                        "mouth:aa": F("0.25"),
                        "quote \" backslash \\ slash /": F("-0.25"),
                        "あ": F("0.5"),
                    },
                    "metadata": {
                        "kind": "simulated",
                        "provider": "example.sender",
                        "protocol": "example",
                        "sourceId": "",
                        "sourceTimestamp": D("100.25"),
                        "sequenceNumber": "18446744073709551615",
                    },
                },
                "trackers": [],
            },
            {
                "actor": "performer B",
                "pose": {
                    "timestamp": D("0"),
                    "root": {},
                    "joints": {},
                    "contacts": {"leftFoot": "unknown", "rightFoot": "unknown"},
                    "channels": {},
                    "metadata": {
                        "kind": "clip",
                        "provider": "",
                        "protocol": "",
                        "sourceId": "",
                    },
                },
                "trackers": [
                    {"trackerId": "left wrist", "position": vec("0.3", "1", "0.1"),
                     "confidence": F("1")},
                ],
            },
        ],
    }


def empty_actors() -> dict:
    return {
        "format": "openstrata.motion.frame/v1",
        "frameNumber": "0",
        "sourceProfile": "",
        "timing": {"receiveTimestamp": D("0"), "sourceClock": "none"},
        "actors": [],
    }


def wall_clock() -> dict:
    message = trackers_only()
    message["timing"] = {
        "sourceTimestamp": D("1.5e+16"),
        "receiveTimestamp": D("3.5"),
        "sequence": "4294967296",
        "sourceClock": "wall",
    }
    message["actors"][0]["actor"] = "1"
    message["actors"][0]["pose"] = {
        "timestamp": D("3.5"),
        "root": {"worldPosition": vec("0", "1e-07", "0")},
        "joints": {},
        "channels": {},
        "metadata": {"kind": "generated", "provider": "p", "protocol": "q",
                     "sourceId": "r", "sourceTimestamp": D("1.5e+16")},
    }
    # Key order is part of the spelling: `pose` comes before `trackers`.
    actor = message["actors"][0]
    message["actors"][0] = {"actor": actor["actor"], "pose": actor["pose"],
                            "trackers": actor["trackers"]}
    return message


CANONICAL = {
    "body-pose": (body_pose, "§3's example: root, three joints, two channels, a sequence"),
    "trackers-only": (trackers_only, "an actor with no pose; position, rotation and both"),
    "every-field": (every_field, "every optional key, two actors, every escape"),
    "empty-actors": (empty_actors, "an empty frame, an empty profile, no source clock"),
    "wall-clock": (wall_clock, "a scientific double, a 2^32 sequence, a pose with no joints"),
}


# ---------------------------------------------------------------------------
# Accepted, not canonical
# ---------------------------------------------------------------------------

ACCEPTED: dict[str, tuple[bytes, str]] = {
    "reordered-pretty": (
        b'{\n  "actors": [],\n  "timing": { "sourceClock": "none", "receiveTimestamp": 0 },\n'
        b'  "sourceProfile": "p",\n  "frameNumber": "1",\n'
        b'  "format": "openstrata.motion.frame/v1"\n}\n',
        "keys in any order, whitespace anywhere: a reader does not depend on order (§4.5)"),
    "escaped-strings": (
        b'{"format":"openstrata.motion.frame/v1","frameNumber":"1",'
        b'"sourceProfile":"\\u0065xample\\/v1","timing":{"receiveTimestamp":0,'
        b'"sourceClock":"none"},"actors":[{"actor":"\\ud83d\\ude00","trackers":[]}]}',
        "\\u escapes, a surrogate pair, an escaped solidus"),
    "number-spellings": (
        b'{"format":"openstrata.motion.frame/v1","frameNumber":"1","sourceProfile":"p",'
        b'"timing":{"receiveTimestamp":1.0E0,"sourceClock":"none"},"actors":[{"actor":"0",'
        b'"trackers":[{"trackerId":"t","position":[-0.0,1e-50,0.50],'
        b'"rotation":[0.9995,0,0,0],"confidence":1E-0}]}]}',
        "exponents, a negative zero, an underflow to zero, a quaternion inside the tolerance"),
}


# ---------------------------------------------------------------------------
# Refused
# ---------------------------------------------------------------------------


def canonical_text(builder) -> str:
    return render(builder())


def mutate(builder, change) -> bytes:
    message = copy.deepcopy(builder())
    change(message)
    return render(message).encode("utf-8")


def pose(message: dict, actor: int = 0) -> dict:
    return message["actors"][actor]["pose"]


def replace_once(text: str, old: str, new: str) -> tuple[bytes, int]:
    """`text` with `old` replaced, and the byte offset where it was."""
    index = text.index(old)
    out = text[:index] + new + text[index + len(old):]
    return out.encode("utf-8"), len(text[:index].encode("utf-8"))


def text_refusals() -> list[tuple[str, bytes, str, str, str]]:
    """Messages that are not JSON text, each with the byte the reader stops at."""
    base = canonical_text(body_pose)
    rows = []

    def add(name, data, offset, why):
        rows.append((name, data, "WIRE_MESSAGE_MALFORMED", f"byte {offset}", why))

    add("empty", b"", 0, "no value at all")
    data = ("﻿" + base).encode("utf-8")
    add("byte-order-mark", data, 0, "a byte order mark is not whitespace")
    data = (base + "x").encode("utf-8")
    add("trailing-content", data, len(base.encode("utf-8")), "content after the message")
    data, at = replace_once(base, ',"trackers":[]}', ',"trackers":[],}')
    add("trailing-comma", data, at + len(',"trackers":[],'), "a comma before '}'")
    data, at = replace_once(base, '"frameNumber":"1042"', "'frameNumber':\"1042\"")
    add("single-quotes", data, at, "a key in single quotes")
    data, at = replace_once(base, '"receiveTimestamp":5123.481902', '"receiveTimestamp":NaN')
    add("nan-literal", data, at + len('"receiveTimestamp":'), "JSON has no NaN")
    data, at = replace_once(base, '"receiveTimestamp":5123.481902', '"receiveTimestamp":05123.5')
    add("leading-zero-number", data, at + len('"receiveTimestamp":0'),
        "a number with a leading zero")
    data, at = replace_once(base, '"receiveTimestamp":5123.481902', '"receiveTimestamp":5123.')
    add("fraction-without-digit", data, at + len('"receiveTimestamp":5123.'),
        "a point with no digit after it")
    prefix, _, suffix = base.partition('"sourceId":"Example Avatar"')
    head = (prefix + '"sourceId":"Example ').encode("utf-8")
    for name, raw, why in (
            ("utf8-invalid-byte", b"\xff", "0xFF is never UTF-8"),
            ("utf8-overlong", b"\xc0\xaf", "an overlong encoding of '/'"),
            ("utf8-surrogate", b"\xed\xa0\x80", "a UTF-16 surrogate encoded as UTF-8"),
            ("utf8-truncated", b"\xe3\x81", "a sequence cut short by the closing quote"),
            ("control-character", b"\x01", "an unescaped control character")):
        add(name, head + raw + b'Avatar"' + suffix.encode("utf-8"), len(head), why)
    data, at = replace_once(base, '"sourceId":"Example Avatar"', '"sourceId":"\\ud800"')
    add("escape-lone-surrogate", data, at + len('"sourceId":"'), "a high surrogate alone")
    data, at = replace_once(base, '"sourceId":"Example Avatar"', '"sourceId":"\\x41"')
    add("escape-unknown", data, at + len('"sourceId":"\\'), "\\x is not a JSON escape")
    return rows


def refusals() -> list[tuple[str, bytes, str, str, str]]:
    rows = text_refusals()

    def add(name, data, code, subject, why):
        rows.append((name, data, code, subject, why))

    base = canonical_text(body_pose)
    data, at = replace_once(base, '"mouth:aa":0.25', '"mouth:aa":[[0.25]]')
    inner = at + len('"mouth:aa":[')
    add("nesting-too-deep", data, "WIRE_NESTING_TOO_DEEP", f"byte {inner}",
        "the format nests six deep; a channel value at seven is refused before it is read")
    data, _ = replace_once(base, '"sourceProfile":"example.body.v1"',
                           '"sourceProfile":"a","sourceProfile":"b"')
    add("key-duplicate", data, "WIRE_KEY_DUPLICATE", "$.sourceProfile", "a top-level key twice")
    data, _ = replace_once(base, '"hips":[1,0,0,0]', '"hips":[1,0,0,0],"hips":[1,0,0,0]')
    add("key-duplicate-joint", data, "WIRE_KEY_DUPLICATE", "$.actors[0].pose.joints.hips",
        "a joint twice")

    add("format-unknown",
        mutate(body_pose, lambda m: m.update(format="openstrata.motion.frame/v2")),
        "WIRE_FORMAT_UNKNOWN", "$.format", "a version this reader does not know")
    add("format-missing", mutate(body_pose, lambda m: m.pop("format")),
        "WIRE_KEY_MISSING", "$.format", "no format at all")
    add("key-unknown", mutate(body_pose, lambda m: m.update(comment="hi")),
        "WIRE_KEY_UNKNOWN", "$.comment", "adding a key is a new version (§5)")
    add("key-unknown-pose",
        mutate(body_pose, lambda m: pose(m).update(velocity=vec("0", "0", "0"))),
        "WIRE_KEY_UNKNOWN", "$.actors[0].pose.velocity", "an unknown key at depth")
    add("key-unknown-tracker",
        mutate(trackers_only, lambda m: m["actors"][0]["trackers"][0].update(serial="x")),
        "WIRE_KEY_UNKNOWN", "$.actors[0].trackers[0].serial", "an unknown key in a tracker")
    add("key-missing-receive-timestamp",
        mutate(body_pose, lambda m: m["timing"].pop("receiveTimestamp")),
        "WIRE_KEY_MISSING", "$.timing.receiveTimestamp", "a required timing key")
    add("key-missing-trackers", mutate(body_pose, lambda m: m["actors"][0].pop("trackers")),
        "WIRE_KEY_MISSING", "$.actors[0].trackers", "trackers is required, possibly empty")
    add("key-missing-metadata-kind",
        mutate(body_pose, lambda m: pose(m)["metadata"].pop("kind")),
        "WIRE_KEY_MISSING", "$.actors[0].pose.metadata.kind", "a required metadata key")

    add("type-message-array", b"[]", "WIRE_TYPE_MISMATCH", "$", "a message is an object")
    add("type-frame-number-number",
        mutate(body_pose, lambda m: m.update(frameNumber=Num("1042", "raw"))),
        "WIRE_TYPE_MISMATCH", "$.frameNumber", "a counter is a string (§4.3)")
    add("type-null", mutate(body_pose, lambda m: pose(m).update(lookAtTarget=None)),
        "WIRE_TYPE_MISMATCH", "$.actors[0].pose.lookAtTarget",
        "absence is an absent key, never null (§4.1)")
    add("type-boolean",
        mutate(trackers_only, lambda m: m["actors"][0]["trackers"][2].update(confidence=True)),
        "WIRE_TYPE_MISMATCH", "$.actors[0].trackers[2].confidence", "a boolean for a number")
    add("type-quaternion-three",
        mutate(body_pose, lambda m: pose(m)["joints"].update(hips=vec("1", "0", "0"))),
        "WIRE_TYPE_MISMATCH", "$.actors[0].pose.joints.hips", "a rotation has four components")
    add("type-joints-array", mutate(body_pose, lambda m: pose(m).update(joints=[])),
        "WIRE_TYPE_MISMATCH", "$.actors[0].pose.joints", "joints is an object")

    add("value-unknown-clock",
        mutate(body_pose, lambda m: m["timing"].update(sourceClock="gps")),
        "WIRE_VALUE_UNKNOWN", "$.timing.sourceClock", "a clock domain the format has no word for")
    add("value-unknown-kind",
        mutate(body_pose, lambda m: pose(m)["metadata"].update(kind="recorded")),
        "WIRE_VALUE_UNKNOWN", "$.actors[0].pose.metadata.kind", "a source kind")
    add("value-unknown-contact",
        mutate(every_field, lambda m: pose(m)["contacts"].update(leftFoot="grounded")),
        "WIRE_VALUE_UNKNOWN", "$.actors[0].pose.contacts.leftFoot", "the trace's words only")

    data, _ = replace_once(base, '"leftUpperArm"', '"leftUperArm"')
    add("joint-unknown", data, "WIRE_JOINT_UNKNOWN", "$.actors[0].pose.joints.leftUperArm",
        "a typo must not read as a missing limb (§6)")
    add("confidence-extra-joint",
        mutate(every_field, lambda m: pose(m)["confidence"].update(neck=F("1"))),
        "WIRE_CONFIDENCE_MISMATCH", "$.actors[0].pose.confidence.neck",
        "a confidence for a joint the pose does not carry")
    add("confidence-missing-joint",
        mutate(every_field, lambda m: pose(m)["confidence"].pop("head")),
        "WIRE_CONFIDENCE_MISMATCH", "$.actors[0].pose.confidence.head",
        "a carried joint with no confidence")

    data, _ = replace_once(base, '"spine":[0.6,', '"spine":[1e39,')
    add("number-overflows-float", data, "WIRE_NUMBER_OUT_OF_RANGE",
        "$.actors[0].pose.joints.spine[0]", "past FLT_MAX")
    data, _ = replace_once(base, '"receiveTimestamp":5123.481902', '"receiveTimestamp":1e309')
    add("number-overflows-double", data, "WIRE_NUMBER_OUT_OF_RANGE",
        "$.timing.receiveTimestamp", "past DBL_MAX")

    add("quaternion-zero",
        mutate(body_pose, lambda m: pose(m)["joints"].update(hips=quat("0", "0", "0", "0"))),
        "WIRE_QUATERNION_INVALID", "$.actors[0].pose.joints.hips", "a zero quaternion")
    add("quaternion-not-unit",
        mutate(body_pose, lambda m: pose(m)["joints"].update(spine=quat("2", "0", "0", "0"))),
        "WIRE_QUATERNION_INVALID", "$.actors[0].pose.joints.spine", "|q|^2 = 4")
    add("quaternion-root",
        mutate(body_pose,
               lambda m: pose(m)["root"].update(worldOrientation=quat("0.99", "0", "0", "0"))),
        "WIRE_QUATERNION_INVALID", "$.actors[0].pose.root.worldOrientation",
        "|q|^2 = 0.9801, outside the trace's 1e-3")
    add("quaternion-tracker",
        mutate(trackers_only,
               lambda m: m["actors"][0]["trackers"][0].update(rotation=quat("0.5", "0", "0", "0"))),
        "WIRE_QUATERNION_INVALID", "$.actors[0].trackers[0].rotation", "a tracker rotation")

    add("counter-negative", mutate(body_pose, lambda m: m.update(frameNumber="-1")),
        "WIRE_COUNTER_INVALID", "$.frameNumber", "a uint64 has no sign")
    add("counter-overflow",
        mutate(body_pose, lambda m: m["timing"].update(sequence="18446744073709551616")),
        "WIRE_COUNTER_INVALID", "$.timing.sequence", "2^64")
    add("counter-leading-zero", mutate(body_pose, lambda m: m.update(frameNumber="01042")),
        "WIRE_COUNTER_INVALID", "$.frameNumber", "one spelling per counter")
    add("counter-fraction",
        mutate(body_pose, lambda m: pose(m)["metadata"].update(sequenceNumber="1.5")),
        "WIRE_COUNTER_INVALID", "$.actors[0].pose.metadata.sequenceNumber", "not an integer")
    add("counter-empty", mutate(body_pose, lambda m: m["timing"].update(sequence="")),
        "WIRE_COUNTER_INVALID", "$.timing.sequence", "no digits")

    def second_actor(m):
        m["actors"].append({"actor": "0", "trackers": []})

    add("actor-duplicate", mutate(body_pose, second_actor),
        "WIRE_ID_DUPLICATE", "$.actors[1].actor", "one actor twice in a frame")
    add("tracker-duplicate",
        mutate(trackers_only, lambda m: m["actors"][0]["trackers"][2].update(trackerId="1")),
        "WIRE_ID_DUPLICATE", "$.actors[0].trackers[2].trackerId", "one tracker twice in an actor")
    return rows


# ---------------------------------------------------------------------------


def build() -> tuple[dict[str, bytes], str]:
    files: dict[str, bytes] = {}
    lines = [
        "# Generated by libs/motionConnectorWire/tools/generate_messages.py; do not edit.",
        "# <kind>\\t<file>[\\t<code>\\t<subject>], one message per line, each with the",
        "# reason it exists on the comment line above it.",
    ]
    for name, (builder, why) in CANONICAL.items():
        file = name + SUFFIX
        files[file] = canonical_text(builder).encode("utf-8")
        lines += [f"# {why}", f"canonical\t{file}"]
    for name, (data, why) in ACCEPTED.items():
        file = name + SUFFIX
        files[file] = data
        lines += [f"# {why}", f"accepted\t{file}"]
    for name, data, code, subject, why in refusals():
        file = "refused-" + name + SUFFIX
        assert file not in files, file
        files[file] = data
        lines += [f"# {why}", f"refused\t{file}\t{code}\t{subject}"]
    return files, "\n".join(lines) + "\n"


def main() -> int:
    default_output = pathlib.Path(__file__).resolve().parents[1] / "tests" / "corpus"
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--output", type=pathlib.Path, default=default_output,
                        help="corpus directory to write into")
    parser.add_argument("--check", action="store_true",
                        help="fail if the committed corpus differs instead of rewriting it")
    args = parser.parse_args()

    files, expectations = build()
    files[EXPECTATIONS] = expectations.encode("utf-8")

    args.output.mkdir(parents=True, exist_ok=True)
    present = {p.name for p in args.output.iterdir()
               if p.name.endswith(SUFFIX) or p.name == EXPECTATIONS}
    stale = sorted(present - set(files))
    drifted = sorted(name for name, data in files.items()
                     if not (args.output / name).is_file()
                     or (args.output / name).read_bytes() != data)

    if args.check:
        if drifted or stale:
            for name in drifted:
                print(f"differs from the generator: {name}", file=sys.stderr)
            for name in stale:
                print(f"not written by the generator: {name}", file=sys.stderr)
            print("run: python libs/motionConnectorWire/tools/generate_messages.py",
                  file=sys.stderr)
            return 1
        print(f"{len(files) - 1} message(s) match the generator")
        return 0

    for name in stale:
        (args.output / name).unlink()
        print(f"removed {name}")
    for name in drifted:
        (args.output / name).write_bytes(files[name])
        print(f"wrote {name}")
    print(f"{len(files) - 1} message(s) in {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
