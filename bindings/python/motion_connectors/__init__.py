# SPDX-License-Identifier: Apache-2.0
"""Acquisition over the motion-connectors C ABI, ending at `Frame`.

Filtering, solving, retargeting, recording and USD belong downstream, in
`usd-motion-plugins` and above. See bindings/python/README.md.
"""
from ._abi import LibraryError
from ._connector import (Connector, MotionConnectorError, OpenFailedError, abi_version,
                         joint_names, joint_vocabulary_version, open_connector, sources)
from ._values import (Actor, BufferMode, Capability, ClockDomain, ConnectorState, ConnectorStats,
                      Diagnostic, FootContact, Frame, Pose, Quaternion, Severity, SourceInfo,
                      SourceKind, Tracker, Vector3, WebSocketOptions, WebSocketRole)

# Mirrors the repository-root VERSION file (WORKSPACE.md §4).
__version__ = "0.1.0"

__all__ = [
    "Actor", "BufferMode", "Capability", "ClockDomain", "Connector", "ConnectorState",
    "ConnectorStats", "Diagnostic", "FootContact", "Frame", "LibraryError",
    "MotionConnectorError", "OpenFailedError", "Pose", "Quaternion", "Severity", "SourceInfo",
    "SourceKind", "Tracker", "Vector3", "WebSocketOptions", "WebSocketRole", "abi_version",
    "joint_names", "joint_vocabulary_version", "open_connector", "sources",
]
