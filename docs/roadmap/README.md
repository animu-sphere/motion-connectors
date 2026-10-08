# Roadmap

The roadmap holds only **incomplete** work. When something lands, its detail
leaves this directory: shipped scope goes to the changelog and a release
record, and the implemented state to [architecture/](../architecture/) and
[reference/](../reference/). Rationale lives in [design/](../design/).

Legend: ✅ done · 🚧 in progress · ⬜ not started · ⛔ blocked

| Document | Contents |
| --- | --- |
| [current.md](current.md) | What v0.1.0 carried forward, and the decisions v0.2.0 needs first. |
| [boundary-implementation.md](boundary-implementation.md) | Remaining generic tracking ownership review; acquisition reference evidence linked. |

Shipped releases are recorded in [releases/](../releases/README.md). The
completed import is recorded in the [changelog](../../CHANGELOG.md) and
the current component identities are recorded in
[architecture/WORKSPACE.md](../architecture/WORKSPACE.md). It is not an open
roadmap sequence.

## Status at a glance

**This table is the single source of truth for the incomplete release scope.**

| Release | Incomplete scope | Depends on | Status |
| --- | --- | --- | --- |
| boundary implementation: release assignment pending | generic tracking review | browser Boundary D reference implementations | 🚧 |
| v0.2.0: transport and bindings | capture/bridge commands; Python bindings; record-stream example | CLI-O1 and CC-O7 | 🚧 |
| v0.3.0: browser tracking | shared JS/TS consumer API; WASM-friendly data ABI | browser acquisition references | ⬜ |
| v0.4.0: XR and integration | integration examples with `usd-motion-plugins` | `usd-motion-plugins` v0.2.0 | ⬜ |
| later | generation adapter; advanced devices; C ABI | generator interface in `usd-motion-plugins`; CC-O7 | ⬜ |

## Open decisions

Every open question the design documents carry, in the order they block work.
The owning document holds the question; this list only schedules it.

| Id | Question | Owner | Blocks |
| --- | --- | --- | --- |
| CLI-O1 | How `motion_connect record` captures a UDP source | [MOTION_CONNECT §7](../design/MOTION_CONNECT.md#7-open-questions) | `record`, v0.2.0 |
| CC-O7 | A C ABI | [CONNECTOR §13](../design/CONNECTOR_CONTRACT.md#13-open-questions) | Python bindings, v0.2.0 |
| CC-O1 | Joint data beyond `MotionPose` (feeds MC-O1, MC-O2) | [CONNECTOR §13](../design/CONNECTOR_CONTRACT.md#13-open-questions) | v0.3.0 |
| CS-O2 | VMC's two translation channels (`usd-motion-plugins` MC-O3) | [COORDINATES §6](../design/COORDINATE_SYSTEMS.md#6-open-questions) | a recorded session from two senders |
| CC-O5 | `ActorId` type | [CONNECTOR §13](../design/CONNECTOR_CONTRACT.md#13-open-questions) | the first multi-actor source |
| WSC-O2 | More than one receiving WebSocket peer | [WEBSOCKET §11](../design/WEBSOCKET_CONNECTOR.md#11-open-questions) | the first multi-actor source |
| WSC-O1 | TLS for the WebSocket connector | [WEBSOCKET §11](../design/WEBSOCKET_CONNECTOR.md#11-open-questions) | a session across an untrusted network |
| FW-O1 | A binary encoding of `MotionFrame` | [WIRE §9](../design/FRAME_WIRE_FORMAT.md#9-open-questions) | a measured session JSON does not meet |
