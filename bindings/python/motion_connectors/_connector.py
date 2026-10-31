# SPDX-License-Identifier: Apache-2.0
"""A connector, and the copies that leave it.

A frame or diagnostic handle lives only inside the poll that took it: its views
are copied into a value and it is released at once, so no Python object ever
holds a pointer whose lifetime could be misjudged. The connector handle is one
thread's at a time (C_ABI §6) and ctypes releases the GIL around every call, so
each connector holds a lock rather than leaving that rule to the caller.
"""
from __future__ import annotations

import ctypes
import struct
import threading
import weakref
from collections.abc import Iterator
from ctypes import POINTER, byref, c_char_p, c_uint8, c_uint32, c_void_p

from . import _abi
from ._values import (Actor, BufferMode, Capability, ClockDomain, ConnectorState, ConnectorStats,
                      Diagnostic, FootContact, Frame, Pose, Severity, SourceInfo, SourceKind,
                      Tracker, WebSocketOptions, WebSocketRole)


class MotionConnectorError(Exception):
    """A call the C ABI refused. `status_name` is stable; `reason` may be empty."""

    def __init__(self, status: int, status_name: str, reason: str = "") -> None:
        super().__init__(f"{status_name}: {reason}" if reason else status_name)
        self.status = status
        self.status_name = status_name
        self.reason = reason


class OpenFailedError(MotionConnectorError):
    """An open failed: a state, not a fault. The same `Connector` may open again."""


def _text(view: _abi.StringView) -> str:
    # C_ABI §7: every string that reaches here is valid UTF-8, and may contain NUL.
    return ctypes.string_at(view.data, view.size).decode("utf-8")


def _encode(name: str, value: str | None) -> bytes | None:
    if value is None:
        return None
    # A C string would silently stop at a NUL.
    if "\0" in value:
        raise ValueError(f"{name} contains a NUL")
    return value.encode("utf-8")


def _integer(name: str, value: int, low: int, high: int) -> int:
    # ctypes wraps an out-of-range integer instead of refusing it.
    if not low <= value <= high:
        raise ValueError(f"{name} must be in [{low}, {high}], got {value}")
    return value


def _fail(library: ctypes.CDLL, status: int, reason: str = "") -> MotionConnectorError:
    return MotionConnectorError(status, library.motion_status_name(status).decode("ascii"), reason)


def _check(library: ctypes.CDLL, status: int) -> int:
    if status < 0:
        raise _fail(library, status)
    return status


def abi_version() -> tuple[int, int]:
    """The loaded library's C ABI (major, minor)."""
    version = _abi.library().motion_abi_version()
    return version >> 16, version & 0xFFFF


def joint_vocabulary_version() -> int:
    return _abi.library().motion_joint_vocabulary_version()


def joint_names() -> tuple[str, ...]:
    """The shared joint vocabulary; `Pose.local_rotations` is indexed by it."""
    library = _abi.library()
    names = []
    for index in range(library.motion_joint_count()):
        view = _abi.StringView()
        _check(library, library.motion_joint_name(index, byref(view)))
        names.append(_text(view))
    return tuple(names)


def sources() -> tuple[SourceInfo, ...]:
    """The sources the loaded library was built with."""
    library = _abi.library()
    found = []
    for index in range(library.motion_source_count()):
        info = _abi.SourceInfo.new()
        _check(library, library.motion_source_get_info(index, byref(info)))
        found.append(SourceInfo(id=_text(info.id),
                                capabilities=Capability(info.capabilities),
                                default_profile=_text(info.default_profile),
                                default_port=info.default_port))
    return tuple(found)


def _read_pose(library: ctypes.CDLL, frame: c_void_p, actor_index: int, channel_count: int) -> Pose:
    pose = _abi.PoseView.new()
    _check(library, library.motion_frame_get_pose(frame, actor_index, byref(pose)))
    count = pose.joint_count
    rotations = struct.unpack(f"={4 * count}f", ctypes.string_at(pose.local_rotations, 16 * count))
    valid = ctypes.string_at(pose.valid_rotations, count)
    confidence = None
    if pose.confidence:
        confidence = struct.unpack(f"={count}f", ctypes.string_at(pose.confidence, 4 * count))
    channels = []
    for index in range(channel_count):
        channel = _abi.ChannelView.new()
        _check(library, library.motion_frame_get_channel(frame, actor_index, index, byref(channel)))
        channels.append((_text(channel.name), channel.value))
    return Pose(
        source_kind=SourceKind(pose.source_kind),
        timestamp=pose.timestamp,
        root_position=tuple(pose.root_world_position) if pose.has_root_position else None,
        root_orientation=tuple(pose.root_world_orientation) if pose.has_root_orientation else None,
        root_linear_velocity=
            tuple(pose.root_linear_velocity) if pose.has_root_linear_velocity else None,
        root_angular_velocity=
            tuple(pose.root_angular_velocity) if pose.has_root_angular_velocity else None,
        look_at_target=tuple(pose.look_at_target) if pose.has_look_at_target else None,
        local_rotations=tuple(rotations[4 * joint:4 * joint + 4] if valid[joint] else None
                              for joint in range(count)),
        confidence=confidence,
        left_foot_contact=FootContact(pose.left_foot_contact) if pose.has_contacts else None,
        right_foot_contact=FootContact(pose.right_foot_contact) if pose.has_contacts else None,
        channels=tuple(channels),
        provider=_text(pose.provider),
        protocol=_text(pose.protocol),
        source_id=_text(pose.source_id),
        metadata_source_timestamp=
            pose.metadata_source_timestamp if pose.has_metadata_source_timestamp else None,
        metadata_sequence_number=
            pose.metadata_sequence_number if pose.has_metadata_sequence_number else None)


def _read_tracker(library: ctypes.CDLL, frame: c_void_p, actor_index: int, index: int) -> Tracker:
    tracker = _abi.TrackerView.new()
    _check(library, library.motion_frame_get_tracker(frame, actor_index, index, byref(tracker)))
    return Tracker(tracker_id=_text(tracker.tracker_id),
                   position=tuple(tracker.position) if tracker.has_position else None,
                   rotation=tuple(tracker.rotation) if tracker.has_rotation else None,
                   confidence=tracker.confidence if tracker.has_confidence else None)


def _read_frame(library: ctypes.CDLL, frame: c_void_p) -> Frame:
    info = _abi.FrameInfo.new()
    _check(library, library.motion_frame_get_info(frame, byref(info)))
    actors = []
    for actor_index in range(info.actor_count):
        actor = _abi.ActorView.new()
        _check(library, library.motion_frame_get_actor(frame, actor_index, byref(actor)))
        pose = (_read_pose(library, frame, actor_index, actor.channel_count)
                if actor.has_pose else None)
        trackers = tuple(_read_tracker(library, frame, actor_index, index)
                         for index in range(actor.tracker_count))
        actors.append(Actor(actor_id=_text(actor.actor_id), pose=pose, trackers=trackers))
    return Frame(frame_number=info.frame_number,
                 receive_timestamp=info.receive_timestamp,
                 source_timestamp=info.source_timestamp if info.has_source_timestamp else None,
                 sequence=info.sequence if info.has_sequence else None,
                 source_clock=ClockDomain(info.source_clock),
                 source_profile=_text(info.source_profile),
                 actors=tuple(actors))


def _read_diagnostic(library: ctypes.CDLL, diagnostic: c_void_p) -> Diagnostic:
    view = _abi.DiagnosticView.new()
    _check(library, library.motion_diagnostic_get_view(diagnostic, byref(view)))
    return Diagnostic(code=_text(view.code),
                      severity=Severity(view.severity),
                      recoverable=bool(view.recoverable),
                      source=_text(view.source),
                      subject=_text(view.subject),
                      detail=_text(view.detail),
                      timestamp=view.timestamp if view.has_timestamp else None,
                      sequence=view.sequence if view.has_sequence else None)


class Connector:
    """One source. It starts closed, and `close` keeps it usable: it may open again.

    Nothing here blocks or runs a loop. The caller's tick drains `frames()` and
    `diagnostics()`, which are separate reads, so neither consumes the other.
    The handle itself is freed when the object is collected.
    """

    def __init__(self, source: str) -> None:
        self._library = _abi.library()
        self._lock = threading.Lock()
        self.source = source
        handle = c_void_p()
        status = self._library.motion_connector_create(_encode("source", source), byref(handle))
        _check(self._library, status)
        self._handle = handle
        weakref.finalize(self, self._library.motion_connector_destroy, handle)

    def __enter__(self) -> Connector:
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def __repr__(self) -> str:
        return f"Connector({self.source!r}, state={self.state.name})"

    def open(self, *, port: int, address: str | None = None,
             buffer_mode: BufferMode = BufferMode.LATEST, buffer_capacity: int = 1,
             source_profile: str | None = None, coordinate_conversion: str | None = None,
             diagnostic_capacity: int = 0, websocket: WebSocketOptions | None = None) -> None:
        """Opens, closing first if open. `port` is always stated; 0 asks the OS for one.

        Raises OpenFailedError with the reason. The connector is then in
        ConnectorState.ERROR and may open again.
        """
        config = _abi.ConnectorConfig.new()
        config.port = _integer("port", port, 0, 0xFFFF)
        config.buffer_mode = BufferMode(buffer_mode)
        config.buffer_capacity = _integer("buffer_capacity", buffer_capacity, 1, 2**64 - 1)
        config.diagnostic_capacity = _integer("diagnostic_capacity", diagnostic_capacity,
                                              0, 2**64 - 1)
        config.address = _encode("address", address)
        config.source_profile = _encode("source_profile", source_profile)
        config.coordinate_conversion = _encode("coordinate_conversion", coordinate_conversion)
        if websocket is not None:
            options = _abi.WebSocketOptions.new()
            options.role = WebSocketRole(websocket.role)
            options.path = _encode("path", websocket.path)
            origins = [_encode("allowed_origins", origin) for origin in websocket.allowed_origins]
            origin_array = (c_char_p * len(origins))(*origins)
            options.allowed_origins = ctypes.cast(origin_array, POINTER(c_char_p))
            options.allowed_origin_count = len(origins)
            options.max_message_bytes = _integer("max_message_bytes",
                                                 websocket.max_message_bytes, 0, 2**64 - 1)
            options.read_budget_messages = _integer("read_budget_messages",
                                                    websocket.read_budget_messages, 0, 2**64 - 1)
            options.handshake_timeout_seconds = websocket.handshake_timeout_seconds
            options.silence_timeout_seconds = websocket.silence_timeout_seconds
            options.reconnect_initial_seconds = websocket.reconnect_initial_seconds
            options.reconnect_max_seconds = websocket.reconnect_max_seconds
            config.websocket = ctypes.pointer(options)
        with self._lock:
            status = self._library.motion_connector_open(self._handle, byref(config))
            if status < 0:
                reason = self._read_view(self._library.motion_connector_last_error)
                name = self._library.motion_status_name(status).decode("ascii")
                kind = OpenFailedError if status == _abi.STATUS_OPEN_FAILED else MotionConnectorError
                raise kind(status, name, reason)

    def close(self) -> None:
        """Idempotent. Pending frames are discarded; undelivered diagnostics remain."""
        with self._lock:
            _check(self._library, self._library.motion_connector_close(self._handle))

    def poll(self) -> Frame | None:
        """The next frame, or None when none is pending. Never blocks."""
        frame = c_void_p()
        with self._lock:
            status = self._library.motion_connector_poll(self._handle, byref(frame))
        if _check(self._library, status) == _abi.STATUS_EMPTY:
            return None
        try:
            return _read_frame(self._library, frame)
        finally:
            self._library.motion_frame_release(frame)

    def frames(self) -> Iterator[Frame]:
        """Drains what is pending, then ends. Iterate again on the caller's next tick."""
        while (frame := self.poll()) is not None:
            yield frame

    def poll_diagnostic(self) -> Diagnostic | None:
        """The oldest undelivered diagnostic, or None. Never blocks."""
        diagnostic = c_void_p()
        with self._lock:
            status = self._library.motion_connector_poll_diagnostic(self._handle,
                                                                    byref(diagnostic))
        if _check(self._library, status) == _abi.STATUS_EMPTY:
            return None
        try:
            return _read_diagnostic(self._library, diagnostic)
        finally:
            self._library.motion_diagnostic_release(diagnostic)

    def diagnostics(self) -> Iterator[Diagnostic]:
        """Drains what is undelivered, then ends."""
        while (diagnostic := self.poll_diagnostic()) is not None:
            yield diagnostic

    def push_input(self, data: bytes, receive_timestamp: float, peer: str | None = None) -> None:
        """One received datagram or message, with no socket and no open (C_ABI §6.2).

        An unopened connector's buffer holds one frame, so drain after each push.
        """
        data = bytes(data)
        pointer = ctypes.cast(c_char_p(data), POINTER(c_uint8))
        self._call(self._library.motion_connector_push_input, pointer, len(data),
                   receive_timestamp, _encode("peer", peer))

    def flush_input(self, receive_timestamp: float) -> None:
        """Completes a frame a replayed capture left open."""
        self._call(self._library.motion_connector_flush_input, receive_timestamp)

    @property
    def state(self) -> ConnectorState:
        state = c_uint32()
        with self._lock:
            status = self._library.motion_connector_get_state(self._handle, byref(state))
        _check(self._library, status)
        return ConnectorState(state.value)

    @property
    def capabilities(self) -> Capability:
        capabilities = c_uint32()
        with self._lock:
            status = self._library.motion_connector_get_capabilities(self._handle,
                                                                     byref(capabilities))
        _check(self._library, status)
        return Capability(capabilities.value)

    @property
    def source_profile(self) -> str:
        """The configured profile, or the source's default before an open."""
        with self._lock:
            return self._read_view(self._library.motion_connector_get_source_profile)

    @property
    def endpoint(self) -> str:
        """The bound endpoint, or a connect role's peer. Empty before an open."""
        with self._lock:
            return self._read_view(self._library.motion_connector_get_endpoint)

    @property
    def last_error(self) -> str:
        with self._lock:
            return self._read_view(self._library.motion_connector_last_error)

    @property
    def stats(self) -> ConnectorStats:
        stats = _abi.ConnectorStats.new()
        with self._lock:
            status = self._library.motion_connector_get_stats(self._handle, byref(stats))
        _check(self._library, status)
        return ConnectorStats(pushed_frames=stats.pushed_frames,
                              polled_frames=stats.polled_frames,
                              dropped_frames=stats.dropped_frames,
                              skipped_frames=stats.skipped_frames,
                              rejected_frames=stats.rejected_frames,
                              refused_frames=stats.refused_frames,
                              dropped_diagnostics=stats.dropped_diagnostics)

    # A connector's views last until its next non-const call: read under the lock.
    def _read_view(self, function) -> str:
        view = _abi.StringView()
        _check(self._library, function(self._handle, byref(view)))
        return _text(view)

    def _call(self, function, *arguments) -> None:
        with self._lock:
            status = function(self._handle, *arguments)
            if status < 0:
                reason = self._read_view(self._library.motion_connector_last_error)
                raise _fail(self._library, status, reason)


def open_connector(source: str, *, port: int, **options) -> Connector:
    """Creates and opens a connector; `Connector.open` documents the options.

    To retry a failed open on the same connector, use `Connector` directly.
    """
    connector = Connector(source)
    connector.open(port=port, **options)
    return connector
