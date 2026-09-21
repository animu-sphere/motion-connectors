# Motion Connectors

[![CI](https://github.com/animu-sphere/motion-connectors/actions/workflows/ost-source-ci.yml/badge.svg)](https://github.com/animu-sphere/motion-connectors/actions/workflows/ost-source-ci.yml)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

Connectivity for live motion: devices, browsers, SDKs, streams and remote
services, recorded transport captures and replay sources, normalized into the
shared motion contract used by the OpenUSD avatar stack.

> **Status: v0.1.0 implementation in progress.** The native transport, OSC,
> tracking, VMC, mocopi and VRChat OSC Tracker implementations are in this
> repository. Current work is converging them on the shared connector contract,
> source profiles and the `motion_connect` CLI. The
> [capability matrix](docs/reference/CAPABILITY_MATRIX.md) is the only page
> that states current implementation status.

## The central rule

> **`motion-connectors` owns connectivity and normalization.
> `usd-motion-plugins` owns motion semantics and transformation.
> Avatar plugins own avatar-format semantics.**

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

A connector describes what it observed and nothing about avatars. It does not
retarget, filter, author USD or interpret VRM or MMD, and it never needs a
`UsdStage`. The pose it produces is `usd-motion-plugins`' `MotionPose`. This
repository depends on that one, and nothing depends on it except the runtime.

## Current milestone

Complete `motionConnectorCore`, adapt the imported source implementations to
that contract, add their profiles and finish the hardware-free replay and
package evidence for v0.1.0. The incomplete work and later releases are in the
[roadmap](docs/roadmap/README.md).

## Canonical conventions

Canonical motion is right-handed, +Y up, +Z forward, in metres and seconds.
Each source's basis is converted exactly once inside its connector. Timestamps
keep source and receive clocks separate, and live streams report disconnects
and frame loss through connector state ([connector contract](docs/design/CONNECTOR_CONTRACT.md)).

## Documentation

| | |
| --- | --- |
| [docs/design/](docs/design/) | The design policy, and the connector, coordinate-system and source-profile contracts |
| [docs/architecture/](docs/architecture/) | The workspace contract and external dependencies |
| [docs/reference/](docs/reference/) | What is implemented, and diagnostics |
| [docs/roadmap/](docs/roadmap/) | What comes next |
| [docs/contributing/](docs/contributing/) | How the documentation is maintained |

Changes are recorded in the [changelog](CHANGELOG.md).

## License

Apache-2.0. See [LICENSE](LICENSE).
