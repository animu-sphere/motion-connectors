# SPDX-License-Identifier: Apache-2.0
"""What crosses into Python: immutable values that own all of their data.

`None` is the absent spelling everywhere (C_ABI §8), so an absent joint can never
be read as a rotation. What a frame means is CONNECTOR_CONTRACT's, and the pose
inside it is `usd-motion-plugins`' MOTION_CONTRACT's; neither is restated here.
"""
from __future__ import annotations

import enum
from dataclasses import dataclass

Vector3 = tuple[float, float, float]
# [w, x, y, z], as in the C ABI and the frame wire format.
Quaternion = tuple[float, float, float, float]


class ConnectorState(enum.IntEnum):
    DISCONNECTED = 0
    CONNECTING = 1
    CONNECTED = 2
    DEGRADED = 3
    ERROR = 4


class Capability(enum.IntFlag):
    BODY = 1 << 0
    HANDS = 1 << 1
    FACE = 1 << 2
    EYES = 1 << 3
    ROOT_MOTION = 1 << 4
    TRACKERS = 1 << 5
    CONTROLLERS = 1 << 6
    SOURCE_TIMESTAMPS = 1 << 7
    CONFIDENCE = 1 << 8
    MULTIPLE_ACTORS = 1 << 9


class BufferMode(enum.IntEnum):
    LATEST = 0
    ORDERED = 1
    LOSSLESS = 2


class ClockDomain(enum.IntEnum):
    DEVICE = 0
    LOCAL_MONOTONIC = 1
    WALL = 2
    NETWORK_SYNCHRONIZED = 3
    NONE = 4


class SourceKind(enum.IntEnum):
    CLIP = 0
    LIVE_CAPTURE = 1
    GENERATED = 2
    PROCEDURAL = 3
    SIMULATED = 4


class FootContact(enum.IntEnum):
    UNKNOWN = 0
    NOT_IN_CONTACT = 1
    IN_CONTACT = 2


class Severity(enum.IntEnum):
    INFO = 0
    WARNING = 1
    ERROR = 2


class WebSocketRole(enum.IntEnum):
    LISTEN = 0
    CONNECT = 1


@dataclass(frozen=True, slots=True)
class SourceInfo:
    id: str
    capabilities: Capability
    # Empty when frames carry their sender's profile.
    default_profile: str
    # 0 when it has none. Never applied implicitly.
    default_port: int


@dataclass(frozen=True, slots=True)
class ConnectorStats:
    pushed_frames: int
    polled_frames: int
    dropped_frames: int
    skipped_frames: int
    rejected_frames: int
    refused_frames: int
    dropped_diagnostics: int


@dataclass(frozen=True, slots=True)
class WebSocketOptions:
    """The WebSocket source's options; a zero numeric field selects its default."""
    role: WebSocketRole = WebSocketRole.LISTEN
    path: str | None = None
    allowed_origins: tuple[str, ...] = ()
    max_message_bytes: int = 0
    read_budget_messages: int = 0
    handshake_timeout_seconds: float = 0.0
    silence_timeout_seconds: float = 0.0
    reconnect_initial_seconds: float = 0.0
    reconnect_max_seconds: float = 0.0


@dataclass(frozen=True, slots=True)
class Pose:
    source_kind: SourceKind
    timestamp: float
    root_position: Vector3 | None
    root_orientation: Quaternion | None
    root_linear_velocity: Vector3 | None
    root_angular_velocity: Vector3 | None
    look_at_target: Vector3 | None
    # Indexed by joint_names(). An absent joint is None, never identity.
    local_rotations: tuple[Quaternion | None, ...]
    # Indexed by joint_names(); None when the pose carries no confidence.
    confidence: tuple[float, ...] | None
    # Each None when the pose carries no contacts.
    left_foot_contact: FootContact | None
    right_foot_contact: FootContact | None
    # (name, value), sorted by name, each name once. Values are not clamped.
    channels: tuple[tuple[str, float], ...]
    # Provenance: reported, never a branch condition.
    provider: str
    protocol: str
    source_id: str
    metadata_source_timestamp: float | None
    metadata_sequence_number: int | None


@dataclass(frozen=True, slots=True)
class Tracker:
    # As the source names it. A tracker id is not a body role.
    tracker_id: str
    position: Vector3 | None
    rotation: Quaternion | None
    confidence: float | None


@dataclass(frozen=True, slots=True)
class Actor:
    actor_id: str
    pose: Pose | None
    trackers: tuple[Tracker, ...]


@dataclass(frozen=True, slots=True)
class Frame:
    # This connector's, monotone.
    frame_number: int
    # Seconds, local monotonic clock.
    receive_timestamp: float
    # Seconds, in source_clock.
    source_timestamp: float | None
    sequence: int | None
    source_clock: ClockDomain
    source_profile: str
    actors: tuple[Actor, ...]


@dataclass(frozen=True, slots=True)
class Diagnostic:
    # The stable string, DIAGNOSTICS.md's catalog.
    code: str
    severity: Severity
    recoverable: bool
    source: str
    subject: str
    detail: str
    # Seconds, in the source's clock.
    timestamp: float | None
    sequence: int | None
