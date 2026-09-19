# motion-connectors

Connectivity for live motion: devices, browsers, SDKs, streams and remote
services, normalized into the one motion contract the OpenUSD avatar stack
shares.

> **Status: documentation and an empty build scaffold.** The design policy is
> accepted and the contracts are proposed. The first connectors (VMC, mocopi, VRChat OSC
> Trackers) are implemented and measured today in
> [`usd-vrm-plugins`](https://github.com/animu-sphere/usd-vrm-plugins), and
> they arrive here once `usd-motion-plugins` publishes its core
> ([roadmap](docs/roadmap/current.md)). The
> [capability matrix](docs/reference/CAPABILITY_MATRIX.md) is the only page
> that says what is implemented here.

## The central rule

> **`motion-connectors` owns connectivity and normalization.
> `usd-motion-plugins` owns motion semantics and transformation.
> Avatar plugins own avatar-format semantics.**

```text
mocopi · VMC · VRChat OSC · MediaPipe · WebXR · OpenXR · WebSocket · …
                               │
                               ▼
motion-connectors      connect · normalize · timestamp · identify · stream
                               │  MotionFrame (MotionPose, tracker observations)
                               ▼
usd-motion-plugins     filter · retarget · record · UsdSkelAnimation
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

## Planned components

| Component | Role | Release |
| --- | --- | --- |
| `motionConnectorCore` | `IMotionConnector`, `MotionFrame`, state, capabilities, timing, the bounded frame buffer | v0.1.0 |
| `motionConnectorTransport`, `motionConnectorOsc` | UDP and packet capture (**imported**); the OSC 1.0 wire format | v0.1.0 |
| `motionConnectorVmc` | VMC Protocol | v0.1.0 |
| `motion_connect` | `list`, `dump`, `inspect`, then `record`, `bridge` | v0.1.0, v0.2.0 |
| `motionConnectorMocopi`, `motionConnectorVrchatOsc`, `motionConnectorTracking` | mocopi native UDP; VRChat OSC Trackers; tracker assignment and solve | v0.1.0 |
| `motionConnectorWebSocket`, Python bindings | `MotionFrame` over the network; `open_connector` | v0.2.0 |
| `motionConnectorMediaPipe`, JS / TS package | browser body, hand and face tracking | v0.3.0 |
| `motionConnectorWebXR` | browser XR head, controllers and hands | v0.4.0 |
| `motionConnectorOpenXR` | native XR | later |

Every connector is an optional module: a build that wants VMC needs no OpenXR,
no browser and no vendor SDK. Identities and dependency directions are fixed
in [docs/architecture/WORKSPACE.md](docs/architecture/WORKSPACE.md). Which
release carries what is in the
[roadmap](docs/roadmap/README.md#status-at-a-glance).

## Canonical conventions

Canonical motion is right-handed, +Y up, +Z forward, in metres and seconds.
Each source's basis is converted exactly once, inside its connector, and each
basis is declared as *measured*, *documented* or *assumed*
([COORDINATE_SYSTEMS.md](docs/design/COORDINATE_SYSTEMS.md)). Timestamps
carry the source clock and the receive clock separately, never mixed. Live
streams tolerate disconnects and frame loss, and they report state instead of
failing the runtime ([CONNECTOR_CONTRACT.md](docs/design/CONNECTOR_CONTRACT.md)).

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
