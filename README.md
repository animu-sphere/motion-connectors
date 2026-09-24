# Motion Connectors

[![CI](https://github.com/animu-sphere/motion-connectors/actions/workflows/ost-source-ci.yml/badge.svg)](https://github.com/animu-sphere/motion-connectors/actions/workflows/ost-source-ci.yml)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

Connectivity for live motion: devices, browsers, SDKs, streams and remote
services, recorded transport captures and replay sources, normalized into the
shared motion contract used by the OpenUSD avatar stack.

## Scope

> **`motion-connectors` owns connectivity and normalization.
> `usd-motion-plugins` owns motion semantics and transformation.
> Avatar plugins own avatar-format semantics.**

This repository owns connector interfaces, `MotionFrame`, source state and
timestamps, frame assembly and buffering, protocol and device input, source
profiles and source coordinate conversion, and tracker observations. It stops
at the canonical motion boundary.

It does not own:

| Responsibility | Owner |
| --- | --- |
| What `MotionPose`, `MotionStream` and retargeting mean | [`usd-motion-plugins`](https://github.com/animu-sphere/usd-motion-plugins) |
| VRM or MMD avatar semantics | [`usd-vrm-plugins`](https://github.com/animu-sphere/usd-vrm-plugins), `usd-mmd-plugins` |
| Runtime composition and the update loop | `usd-avatar-runtime` |

A connector describes what it observed and nothing about avatars. It does not
retarget, filter, author USD or interpret VRM or MMD, and it never needs a
`UsdStage`.

## Architecture

```text
mocopi · VMC · VRChat OSC · MediaPipe · WebXR · OpenXR · WebSocket · …
                               │
                               ▼
motion-connectors      connect · normalize · timestamp · identify · stream
                               │  MotionFrame (shared MotionPose, tracker observations)
                               ▼
usd-motion-plugins     filter · retarget · semantic recording · USD bridge
                               │
                               ▼
usd-vrm-plugins · usd-mmd-plugins · …            avatar-format semantics
                               │
                               ▼
usd-avatar-runtime     composition, the update loop
```

## Components

| Component | Responsibility |
| --- | --- |
| `motionConnectorCore` | `IMotionConnector`, `MotionFrame`, tracker observations, state, timing, the bounded frame buffer |
| `motionConnectorTransport` | UDP receiver, datagram queue, packet-capture files; knows no protocol |
| `motionConnectorOsc` | The OSC 1.0 wire format; knows no address semantics |
| `motionConnectorVmc` | VMC Protocol input |
| `motionConnectorMocopi` | mocopi native UDP input |
| `motionConnectorVrchatOsc` | VRChat OSC Trackers input |
| `motionConnectorTracking` | Tracker regions, assignment and the tracker solve |
| `motion_connect` | CLI over `MotionFrame`: `list`, `dump`, `inspect` |
| `vmc_record`, `mocopi_record`, `vrchat_osc_record` | CLIs: record or inspect a live session's packet capture |

Identities and dependency directions:
[docs/architecture/WORKSPACE.md](docs/architecture/WORKSPACE.md).

## Documentation

| | |
| --- | --- |
| [What is implemented](docs/reference/CAPABILITY_MATRIX.md) | The capability matrix, the only source for implementation status |
| [Incomplete work](docs/roadmap/current.md) | The current milestone and its completion criteria |
| [Release history](CHANGELOG.md) | The changelog |
| [docs/](docs/README.md) | Which document owns which subject |

Canonical motion is right-handed, +Y up, +Z forward, in metres and seconds;
each source is converted exactly once, inside its connector
([coordinate systems](docs/design/COORDINATE_SYSTEMS.md)).

## Build

[docs/guides/building.md](docs/guides/building.md) builds and tests the tree
with `ost` or with plain CMake against an OpenUSD 26.08 install and installed
`usd-motion-plugins` packages.

## License

Apache-2.0. See [LICENSE](LICENSE).
