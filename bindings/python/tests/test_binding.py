#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""The Python binding, against the C header and the C++ connectors.

  test_binding.py LayoutTest        needs no library
  test_binding.py ApiTest           no socket
  test_binding.py ReplayParityTest  needs MOTION_CONNECTORS_TEST_ORACLE
  test_binding.py LoopbackTest      binds loopback at an OS-chosen port
  test_binding.py InstalledTest     needs MOTION_CONNECTORS_TEST_PREFIX

The package is what PYTHONPATH finds, so the same tests run from the source tree
and from an installed prefix.
"""
from __future__ import annotations

import ctypes
import dataclasses
import gc
import json
import math
import os
import pathlib
import pickle
import re
import socket
import struct
import subprocess
import sys
import threading
import time
import unittest
from ctypes import (POINTER, c_char, c_char_p, c_double, c_float, c_int32, c_uint8, c_uint16,
                    c_uint32, c_uint64, c_void_p)

import motion_connectors as mc
from motion_connectors import _abi

REPO = pathlib.Path(__file__).resolve().parents[3]
HEADER = REPO / "libs/motionConnectorCAbi/include/motionConnectorCAbi/MotionConnector.h"
VMC = REPO / "libs/motionConnectorVmc/tests/corpus"
MOCOPI = REPO / "libs/motionConnectorMocopi/tests/corpus"
VRCHAT = REPO / "libs/motionConnectorVrchatOsc/tests/corpus/generated"
WEBSOCKET = REPO / "libs/motionConnectorWebSocket/tests/corpus/messages"
# A capture with frames and a capture with refusals, for each source, as
# motionConnectorCAbi_conformance replays them: (source, path, frames, diagnostics).
CAPTURES = (
    ("vmc", VMC / "mixed-traffic-30hz.vmcpackets", True, False),
    ("mocopi", MOCOPI / "neutral-standing-60hz.mocopipackets", True, False),
    ("mocopi", MOCOPI / "malformed-packets.mocopipackets", False, True),
    ("vrchat-osc", VRCHAT / "eight-trackers.vrchatoscpackets", True, False),
    ("vrchat-osc", VRCHAT / "malformed-packets.vrchatoscpackets", False, True),
    ("websocket", WEBSOCKET / "steady-60hz.websocketpackets", True, False),
    ("websocket", WEBSOCKET / "trackers-any-profile.websocketpackets", True, False),
    ("websocket", WEBSOCKET / "codec-refusals.websocketpackets", False, True),
)
PRIMITIVES = {"uint8_t": c_uint8, "uint16_t": c_uint16, "uint32_t": c_uint32,
              "uint64_t": c_uint64, "int32_t": c_int32, "float": c_float, "double": c_double}


def read_capture(path: pathlib.Path) -> list[tuple[float, str | None, bytes]]:
    """(receive time, peer, bytes). The format is motionConnectorTransport's."""
    records = []
    peer = None
    open_record = None
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith(("#", "!")):
            continue
        fields = stripped.split()
        if open_record is not None:
            open_record[3] += bytes.fromhex(stripped.split("|", 1)[0])
            if len(open_record[3]) == open_record[2]:
                records.append((open_record[0], open_record[1], bytes(open_record[3])))
                open_record = None
        elif fields[0] == "p":
            peer = None if fields[1] == "-" else fields[1]
        elif fields[0] == "d":
            if int(fields[2]) == 0:
                records.append((float(fields[1]), peer, b""))
            else:
                open_record = [float(fields[1]), peer, int(fields[2]), b""]
    return records


def replay(connector: mc.Connector, records) -> list[dict]:
    """Drains frames and diagnostics after each input, and after the flush."""
    steps = []
    def drain() -> None:
        steps.append({"frames": list(connector.frames()),
                      "diagnostics": list(connector.diagnostics())})
    last = 0.0
    for time_, peer, data in records:
        last = time_
        connector.push_input(data, time_, peer)
        drain()
    connector.flush_input(last)
    drain()
    return steps


def plain(value):
    """A value as the oracle spells it."""
    if dataclasses.is_dataclass(value):
        return {field.name: plain(getattr(value, field.name))
                for field in dataclasses.fields(value)}
    if isinstance(value, dict):
        return {key: plain(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [plain(item) for item in value]
    if isinstance(value, bool) or not isinstance(value, int):
        return value
    return int(value)


def differs(actual, expected, where: str = "") -> str | None:
    """Exact: a float must be the same float, and NaN equals NaN."""
    if isinstance(expected, dict):
        if not isinstance(actual, dict) or actual.keys() != expected.keys():
            return f"{where}: keys differ"
        for key, value in expected.items():
            if (found := differs(actual[key], value, f"{where}.{key}")):
                return found
        return None
    if isinstance(expected, list):
        if not isinstance(actual, list) or len(actual) != len(expected):
            return f"{where}: lengths differ"
        for index, value in enumerate(expected):
            if (found := differs(actual[index], value, f"{where}[{index}]")):
                return found
        return None
    if isinstance(expected, float) and math.isnan(expected):
        return None if isinstance(actual, float) and math.isnan(actual) else f"{where}: not NaN"
    return None if actual == expected else f"{where}: {actual!r} != {expected!r}"


def osc_string(text: bytes) -> bytes:
    return text + b"\0" * (4 - len(text) % 4)


def osc_floats(*values: float) -> bytes:
    return b"".join(struct.pack(">f", value) for value in values)


def vmc_bone() -> bytes:
    return (osc_string(b"/VMC/Ext/Bone/Pos") + osc_string(b",sfffffff") + osc_string(b"Hips")
            + osc_floats(0, 0, 0, 0, 0, 0, 1))


def vmc_blend(name: bytes, value: float) -> bytes:
    return (osc_string(b"/VMC/Ext/Blend/Val") + osc_string(b",sf") + osc_string(name)
            + osc_floats(value))


def vmc_time(seconds: float) -> bytes:
    return osc_string(b"/VMC/Ext/T") + osc_string(b",f") + osc_floats(seconds)


def push_vmc_frame(connector: mc.Connector, time_: float, blend: bytes = b"Joy") -> None:
    for message in (vmc_bone(), vmc_blend(blend, 0.5), vmc_time(time_)):
        connector.push_input(message, time_)
    connector.flush_input(time_)


class LayoutTest(unittest.TestCase):
    """_abi.py is the header, field for field: both are hand-written."""

    @classmethod
    def setUpClass(cls) -> None:
        text = re.sub(r"/\*.*?\*/", " ", HEADER.read_text(encoding="utf-8"), flags=re.S)
        text = re.sub(r"^#.*(?:\\\n.*)*$", " ", text, flags=re.M)
        cls.text = text
        cls.opaque = set(re.findall(r"typedef struct (\w+) \1;", text))
        cls.scalars = {alias: PRIMITIVES[base]
                       for base, alias in re.findall(r"typedef (\w+) (motion_\w+_t);", text)}
        cls.by_c_name = {
            "motion_string_view_t": _abi.StringView,
            "motion_source_info_t": _abi.SourceInfo,
            "motion_websocket_options_t": _abi.WebSocketOptions,
            "motion_connector_config_t": _abi.ConnectorConfig,
            "motion_connector_stats_t": _abi.ConnectorStats,
            "motion_frame_info_t": _abi.FrameInfo,
            "motion_actor_view_t": _abi.ActorView,
            "motion_pose_view_t": _abi.PoseView,
            "motion_channel_view_t": _abi.ChannelView,
            "motion_tracker_view_t": _abi.TrackerView,
            "motion_diagnostic_view_t": _abi.DiagnosticView,
        }

    def resolve(self, spelled: str):
        """A C type as the types ctypes may declare it with."""
        spelled = re.sub(r"\bconst\b", " ", spelled)
        spelled = re.sub(r"\s+", "", spelled)
        depth = len(spelled) - len(spelled.rstrip("*"))
        base = spelled.rstrip("*")
        if base == "char":
            if depth == 1:
                return (c_char_p, POINTER(c_char))
            if depth == 2:
                return (POINTER(c_char_p),)
        if base in self.opaque:
            return (c_void_p,) if depth == 1 else (POINTER(c_void_p),)
        resolved = (PRIMITIVES.get(base) or self.scalars.get(base) or self.by_c_name[base])
        for _ in range(depth):
            resolved = POINTER(resolved)
        return (resolved,)

    def test_structs(self) -> None:
        found = dict(re.findall(r"typedef struct (\w+) \{(.*?)\} \1;", self.text, flags=re.S))
        self.assertEqual(found.keys(), self.by_c_name.keys())
        for c_name, body in found.items():
            declared = self.by_c_name[c_name]._fields_
            names, types = [], []
            for field in filter(None, (part.strip() for part in body.split(";"))):
                if (array_pointer := re.fullmatch(r"(.+)\(\*(\w+)\)\[(\d+)\]", field)):
                    element = self.resolve(array_pointer[1])[0]
                    names.append(array_pointer[2])
                    types.append((POINTER(element * int(array_pointer[3])),))
                    continue
                match = re.fullmatch(r"(.+?[\s*])(\w+)(?:\[(\d+)\])?", field)
                self.assertIsNotNone(match, f"{c_name}: {field}")
                resolved = self.resolve(match[1])
                names.append(match[2])
                types.append((resolved[0] * int(match[3]),) if match[3] else resolved)
            self.assertEqual([name for name, _ in declared], names, c_name)
            for (name, kind), allowed in zip(declared, types):
                self.assertIn(kind, allowed, f"{c_name}.{name}")

    def test_prototypes(self) -> None:
        found = re.findall(r"MOTIONCONNECTORCABI_API\s+([\w\s*]+?)\s*\b(motion_\w+)\(([^)]*)\);",
                           self.text)
        self.assertEqual({name for _, name, _ in found}, _abi.PROTOTYPES.keys())
        for result, name, parameters in found:
            restype, argtypes = _abi.PROTOTYPES[name]
            if result.strip() == "void":
                self.assertIsNone(restype, name)
            else:
                self.assertIn(restype, self.resolve(result), name)
            spelled = [] if parameters.strip() == "void" else parameters.split(",")
            self.assertEqual(len(spelled), len(argtypes), name)
            for parameter, kind in zip(spelled, argtypes):
                without_name = re.sub(r"\w+\s*$", "", parameter.strip())
                self.assertIn(kind, self.resolve(without_name), f"{name}: {parameter}")

    def test_enums(self) -> None:
        enums = {"MOTION_STATE_": mc.ConnectorState, "MOTION_CAPABILITY_": mc.Capability,
                 "MOTION_BUFFER_": mc.BufferMode, "MOTION_CLOCK_": mc.ClockDomain,
                 "MOTION_SOURCE_KIND_": mc.SourceKind, "MOTION_FOOT_CONTACT_": mc.FootContact,
                 "MOTION_SEVERITY_": mc.Severity, "MOTION_WEBSOCKET_": mc.WebSocketRole}
        constants = re.findall(r"\b(MOTION_[A-Z_]+) = (?:1u << )?(-?\d+)", self.text)
        shifted = dict(re.findall(r"\b(MOTION_[A-Z_]+) = 1u << (\d+)", self.text))
        for prefix, kind in enums.items():
            declared = {name[len(prefix):]: (1 << int(shifted[name]) if name in shifted
                                             else int(value))
                        for name, value in constants if name.startswith(prefix)}
            self.assertEqual({member.name: member.value for member in kind}, declared, prefix)
        statuses = dict(constants)
        self.assertEqual(int(statuses["MOTION_STATUS_OK"]), _abi.STATUS_OK)
        self.assertEqual(int(statuses["MOTION_STATUS_EMPTY"]), _abi.STATUS_EMPTY)
        self.assertEqual(int(statuses["MOTION_STATUS_OPEN_FAILED"]), _abi.STATUS_OPEN_FAILED)
        self.assertEqual(int(statuses["MOTION_STATUS_NOT_AVAILABLE"]), _abi.STATUS_NOT_AVAILABLE)
        major = re.search(r"MOTION_ABI_VERSION_MAJOR (\d+)u", HEADER.read_text(encoding="utf-8"))
        self.assertEqual(int(major[1]), _abi.ABI_MAJOR)

    def test_every_view_starts_with_its_size(self) -> None:
        for kind in self.by_c_name.values():
            if kind is not _abi.StringView:
                self.assertEqual(kind._fields_[0][0], "struct_size")
                self.assertEqual(kind.new().struct_size, ctypes.sizeof(kind))


class ApiTest(unittest.TestCase):
    def test_library(self) -> None:
        self.assertEqual(mc.abi_version()[0], _abi.ABI_MAJOR)
        names = mc.joint_names()
        self.assertEqual(len(names), len(set(names)))
        self.assertIn("hips", names)
        self.assertGreater(mc.joint_vocabulary_version(), 0)

    def test_sources(self) -> None:
        found = {source.id: source for source in mc.sources()}
        self.assertLessEqual(found.keys(), {"vmc", "mocopi", "vrchat-osc", "websocket"})
        if "vmc" in found:
            self.assertEqual(found["vmc"].default_profile, "vmc.v1")
            self.assertEqual(found["vmc"].default_port, 39539)
            self.assertIn(mc.Capability.BODY, found["vmc"].capabilities)
            connector = mc.Connector("vmc")
            self.assertEqual(connector.capabilities, found["vmc"].capabilities)
            self.assertEqual(connector.source_profile, "vmc.v1")
            self.assertEqual(connector.endpoint, "")
            self.assertEqual(connector.state, mc.ConnectorState.DISCONNECTED)

    def test_refusals(self) -> None:
        with self.assertRaises(mc.MotionConnectorError) as raised:
            mc.Connector("no-such-source")
        self.assertEqual(raised.exception.status_name, "MOTION_STATUS_UNKNOWN_SOURCE")
        connector = mc.Connector("vmc")
        # ctypes would wrap each of these, or stop at the NUL, instead of refusing.
        for options in ({"port": -1}, {"port": 65536}, {"port": 0, "buffer_capacity": 0},
                        {"port": 0, "buffer_capacity": -1}, {"port": 0, "address": "1\0.2"},
                        {"port": 0, "diagnostic_capacity": 2**64}, {"port": 0, "buffer_mode": 9}):
            with self.subTest(options), self.assertRaises(ValueError):
                connector.open(**options)
        with self.assertRaises(ValueError):
            mc.Connector("vmc\0x")
        with self.assertRaises(TypeError):
            mc.open_connector("vmc")  # A port is never implied.
        with self.assertRaises(mc.MotionConnectorError) as raised:
            connector.open(port=0, websocket=mc.WebSocketOptions())
        self.assertEqual(raised.exception.status_name, "MOTION_STATUS_INVALID_ARGUMENT")
        self.assertIn("websocket", raised.exception.reason)
        self.assertNotIsInstance(raised.exception, mc.OpenFailedError)
        with self.assertRaises(mc.MotionConnectorError):
            connector.push_input(b"", float("nan"))
        with self.assertRaises(mc.MotionConnectorError):
            connector.flush_input(float("inf"))

    def test_failed_open_is_a_state_and_may_retry(self) -> None:
        connector = mc.Connector("vmc")
        with self.assertRaises(mc.OpenFailedError) as raised:
            connector.open(port=0, source_profile="not-the-profile")
        self.assertEqual(raised.exception.status_name, "MOTION_STATUS_OPEN_FAILED")
        self.assertTrue(raised.exception.reason)
        self.assertEqual(connector.last_error, raised.exception.reason)
        self.assertEqual(connector.state, mc.ConnectorState.ERROR)
        connector.close()
        self.assertEqual(connector.state, mc.ConnectorState.DISCONNECTED)
        with self.assertRaises(mc.OpenFailedError):
            mc.open_connector("vmc", port=0, source_profile="not-the-profile")

    def test_frame_is_a_value_that_outlives_everything(self) -> None:
        connector = mc.Connector("vmc")
        push_vmc_frame(connector, 1.0)
        frame = connector.poll()
        self.assertIsNotNone(frame)
        self.assertIsNone(connector.poll())
        before = pickle.dumps(frame)
        push_vmc_frame(connector, 2.0)
        self.assertEqual(connector.poll().frame_number, frame.frame_number + 1)
        connector.close()
        del connector
        gc.collect()
        self.assertEqual(pickle.dumps(frame), before)
        with self.assertRaises(dataclasses.FrozenInstanceError):
            frame.frame_number = 9
        self.assertEqual(pickle.loads(before), frame)

    def test_absence_is_none(self) -> None:
        connector = mc.Connector("vmc")
        push_vmc_frame(connector, 1.0)
        frame = connector.poll()
        self.assertEqual(frame.source_profile, "vmc.v1")
        self.assertEqual(frame.source_clock, mc.ClockDomain.DEVICE)
        self.assertEqual(frame.actors[0].trackers, ())
        pose = frame.actors[0].pose
        self.assertEqual(pose.channels, (("vmc:Joy", 0.5),))
        rotations = dict(zip(mc.joint_names(), pose.local_rotations))
        self.assertEqual(rotations.pop("hips"), (1.0, 0.0, 0.0, 0.0))
        self.assertTrue(rotations and all(value is None for value in rotations.values()))
        self.assertIsNone(pose.confidence)
        self.assertIsNone(pose.look_at_target)
        self.assertIsNone(pose.left_foot_contact)

        # A tracker-only actor has no pose, and absent parts of a tracker are None.
        websocket = mc.Connector("websocket")
        websocket.push_input(
            b'{"format":"openstrata.motion.frame/v1","frameNumber":"18446744073709551615",'
            b'"sourceProfile":"test.v1","timing":{"receiveTimestamp":1.5,"sourceClock":"none"},'
            b'"actors":[{"actor":"a","trackers":[{"trackerId":"t\\u0000x",'
            b'"position":[1,2,3],"confidence":0.25}]}]}', 1.5, "peer")
        frame = websocket.poll()
        self.assertEqual(frame.actors[0].actor_id, "a")
        self.assertIsNone(frame.actors[0].pose)
        tracker = frame.actors[0].trackers[0]
        self.assertEqual(tracker.tracker_id, "t\0x")  # A value may contain NUL.
        self.assertEqual(tracker.position, (1.0, 2.0, 3.0))
        self.assertIsNone(tracker.rotation)
        self.assertEqual(tracker.confidence, 0.25)
        self.assertEqual(frame.source_profile, "test.v1")

    def test_a_frame_that_is_not_utf8_is_refused_and_named(self) -> None:
        connector = mc.Connector("vmc")
        push_vmc_frame(connector, 1.0, blend=b"\xff\xfename")
        self.assertIsNone(connector.poll())
        self.assertEqual(connector.stats.refused_frames, 1)
        codes = {diagnostic.code: diagnostic for diagnostic in connector.diagnostics()}
        refused = codes["C_ABI_FRAME_NOT_UTF8"]
        self.assertEqual(refused.severity, mc.Severity.WARNING)
        self.assertTrue(refused.recoverable)
        self.assertEqual(refused.subject, "actors[0].pose.channels[0].name")
        push_vmc_frame(connector, 2.0)
        self.assertIsNotNone(connector.poll())

    def test_iterators_drain_and_can_run_again(self) -> None:
        connector = mc.Connector("vmc")
        self.assertEqual(list(connector.frames()), [])
        self.assertEqual(list(connector.diagnostics()), [])
        for time_ in (1.0, 2.0):
            push_vmc_frame(connector, time_)
            self.assertEqual(len(list(connector.frames())), 1)
        stats = connector.stats
        self.assertEqual(stats.pushed_frames, stats.polled_frames)

    def test_diagnostics_survive_close(self) -> None:
        connector = mc.Connector("mocopi")
        connector.push_input(b"\x00", 0.1)
        connector.close()
        connector.close()
        self.assertIsNotNone(connector.poll_diagnostic())

    def test_context_manager_closes(self) -> None:
        with mc.Connector("vmc") as connector:
            push_vmc_frame(connector, 1.0)
        self.assertEqual(connector.state, mc.ConnectorState.DISCONNECTED)
        self.assertIsNone(connector.poll())

    def test_handles_are_one_threads_at_a_time(self) -> None:
        # Sharing a connector is safe here without the caller's help, and the
        # counters show that no frame was lost or delivered twice.
        connector = mc.Connector("vmc")
        delivered = []
        failures = []
        stop = threading.Event()
        def read() -> None:
            try:
                while not stop.is_set():
                    delivered.extend(frame.frame_number for frame in connector.frames())
                    connector.stats, connector.state, connector.last_error
            except Exception as error:
                failures.append(error)
        readers = [threading.Thread(target=read) for _ in range(4)]
        for reader in readers:
            reader.start()
        for index in range(200):
            push_vmc_frame(connector, float(index + 1))
        stop.set()
        for reader in readers:
            reader.join()
        delivered.extend(frame.frame_number for frame in connector.frames())
        self.assertEqual(failures, [])
        self.assertEqual(len(delivered), len(set(delivered)))
        self.assertEqual(len(delivered), connector.stats.polled_frames)

    def test_a_library_of_another_major_is_refused(self) -> None:
        class Fake:
            def __getattr__(self, name):
                def function():
                    return (_abi.ABI_MAJOR + 1) << 16
                return function
        real, _abi._load_dll = _abi._load_dll, lambda _: Fake()
        try:
            with self.assertRaisesRegex(_abi.LibraryError, "needs major"):
                _abi._load()
        finally:
            _abi._load_dll = real

    def test_a_missing_library_is_named_at_first_use(self) -> None:
        program = ("import motion_connectors as mc\n"
                   "try:\n    mc.sources()\n"
                   "except mc.LibraryError as error:\n    print(error)\n")
        environment = dict(os.environ, MOTION_CONNECTORS_LIBRARY="no-such-library.dll")
        output = subprocess.run([sys.executable, "-c", program], env=environment, text=True,
                                capture_output=True, check=True).stdout
        self.assertIn("no-such-library.dll", output)
        self.assertIn("MOTION_CONNECTORS_LIBRARY", output)


class ReplayParityTest(unittest.TestCase):
    """C_ABI §10's rule, one layer up: every field, frame and diagnostic."""

    def test_every_capture(self) -> None:
        oracle = os.environ["MOTION_CONNECTORS_TEST_ORACLE"]
        for source, path, want_frames, want_diagnostics in CAPTURES:
            with self.subTest(path.name):
                result = subprocess.run([oracle, source, path], check=True, capture_output=True,
                                        encoding="utf-8")
                expected = json.loads(result.stdout)
                connector = mc.Connector(source)
                actual = plain(replay(connector, read_capture(path)))
                self.assertEqual(differs(actual, expected["steps"], path.name), None)
                frames = sum(len(step["frames"]) for step in actual)
                diagnostics = sum(len(step["diagnostics"]) for step in actual)
                self.assertTrue(frames > 0 or not want_frames)
                self.assertTrue(diagnostics > 0 or not want_diagnostics)
                stats = connector.stats
                self.assertEqual(stats.pushed_frames, expected["pushed_frames"])
                self.assertEqual(stats.polled_frames, expected["polled_frames"])
                self.assertEqual(stats.dropped_frames, expected["dropped_frames"])
                self.assertEqual(stats.refused_frames, 0)
                print(f"{path.name}: {frames} frame(s), {diagnostics} diagnostic(s)")


class LoopbackTest(unittest.TestCase):
    def test_live_vmc_session(self) -> None:
        connector = mc.open_connector("vmc", port=0, buffer_mode=mc.BufferMode.ORDERED,
                                      buffer_capacity=64)
        self.assertEqual(connector.state, mc.ConnectorState.CONNECTING)
        port = int(connector.endpoint.rsplit(":", 1)[1])
        self.assertGreater(port, 0)
        frames: list[mc.Frame] = []
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
            deadline = time.monotonic() + 10.0
            sent = 0
            while len(frames) < 3 and time.monotonic() < deadline:
                sent += 1
                for message in (vmc_bone(), vmc_blend(b"Joy", 0.5), vmc_time(float(sent))):
                    sender.sendto(message, ("127.0.0.1", port))
                time.sleep(0.01)
                frames.extend(connector.frames())
        self.assertGreaterEqual(len(frames), 3)
        numbers = [frame.frame_number for frame in frames]
        self.assertEqual(numbers, sorted(set(numbers)))
        self.assertEqual(frames[0].actors[0].pose.channels, (("vmc:Joy", 0.5),))
        self.assertIn(connector.state,
                      {mc.ConnectorState.CONNECTED, mc.ConnectorState.DEGRADED})
        connector.close()
        self.assertEqual(connector.state, mc.ConnectorState.DISCONNECTED)
        self.assertEqual(list(connector.frames()), [])
        # Closed is not gone, and neither is failed: it opens again.
        with self.assertRaises(mc.OpenFailedError):
            connector.open(port=0, source_profile="not-the-profile")
        self.assertEqual(connector.state, mc.ConnectorState.ERROR)
        connector.open(port=0)
        self.assertEqual(connector.last_error, "")
        self.assertEqual(connector.state, mc.ConnectorState.CONNECTING)
        self.assertNotEqual(connector.endpoint, "")
        connector.close()


class InstalledTest(unittest.TestCase):
    """From the prefix alone, finding the library by the package's own location."""

    def test_the_package_and_library_come_from_the_prefix(self) -> None:
        prefix = pathlib.Path(os.environ["MOTION_CONNECTORS_TEST_PREFIX"]).resolve()
        self.assertTrue(pathlib.Path(mc.__file__).resolve().is_relative_to(prefix))
        self.assertNotIn(_abi.LIBRARY_ENV, os.environ)
        located = pathlib.Path(_abi._candidates()[0]).resolve()
        self.assertTrue(located.is_relative_to(prefix), located)
        self.assertEqual(mc.__version__,
                         (REPO / "VERSION").read_text(encoding="utf-8").strip())

    def test_every_source_replays(self) -> None:
        for source, path, want_frames, want_diagnostics in CAPTURES:
            if source not in {info.id for info in mc.sources()}:
                continue
            with self.subTest(path.name):
                steps = replay(mc.Connector(source), read_capture(path))
                self.assertTrue(sum(len(s["frames"]) for s in steps) > 0 or not want_frames)
                self.assertTrue(sum(len(s["diagnostics"]) for s in steps) > 0
                                or not want_diagnostics)


if __name__ == "__main__":
    unittest.main(verbosity=2)
