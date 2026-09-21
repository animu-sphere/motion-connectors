# Roadmap

The roadmap holds only **incomplete** work. When something lands, its detail
leaves this directory: shipped scope goes to the changelog and a release
record, and the implemented state to [architecture/](../architecture/) and
[reference/](../reference/). Rationale lives in [design/](../design/).

Legend: ✅ done · 🚧 in progress · ⬜ not started · ⛔ blocked

| Document | Contents |
| --- | --- |
| [current.md](current.md) | The current v0.1.0 convergence milestone and its completion criteria. |

The completed import is recorded in the [changelog](../../CHANGELOG.md) and
the current component identities are recorded in
[architecture/WORKSPACE.md](../architecture/WORKSPACE.md). It is not an open
roadmap sequence.

## Status at a glance

**This table is the single source of truth for the incomplete release scope.**

| Release | Incomplete scope | Depends on | Status |
| --- | --- | --- | --- |
| v0.1.0: shared contract convergence | `motionConnectorCore`; source profiles; VMC, mocopi and VRChat OSC adaptation; diagnostics; `motion_connect dump`, `list`, `inspect`; replay and installed-package evidence | `usd-motion-plugins` motion contract decisions; CC-O6, SP-O1, SP-O2 and DIAG-O1 | 🚧 |
| v0.2.0: transport and bindings | `motionConnectorWebSocket`; capture/bridge commands; Python bindings; record-stream example | v0.1.0, CC-O7 and CC-O8 | ⬜ |
| v0.3.0: browser tracking | `motionConnectorMediaPipe`; JS/TS package; WASM-friendly data ABI | WS-O4 and CC-O2 | ⬜ |
| v0.4.0: XR and integration | `motionConnectorWebXR`; integration examples with `usd-motion-plugins` | `usd-motion-plugins` v0.2.0 | ⬜ |
| later | `motionConnectorOpenXR`; generation adapter; advanced devices; C ABI | generator interface in `usd-motion-plugins`; CC-O7 | ⬜ |

## Open decisions

Every open question the design documents carry, in the order they block work.
The owning document holds the question; this list only schedules it.

| Id | Question | Owner | Blocks |
| --- | --- | --- | --- |
| CC-O6 | `Poll` semantics and skip reporting | [CONNECTOR §13](../design/CONNECTOR_CONTRACT.md#13-open-questions) | v0.1.0 |
| SP-O1 | Profile identifier scheme | [PROFILES §6](../design/SOURCE_PROFILES.md#6-open-questions) | v0.1.0 |
| SP-O2 | Profiles as data or code | [PROFILES §6](../design/SOURCE_PROFILES.md#6-open-questions) | v0.1.0 |
| DIAG-O1 | Diagnostic code style, and renaming `VRM_VMC_*`, `VRM_MOCOPI_*` and `VRM_VRCHAT_OSC_*` | [DIAGNOSTICS §4](../reference/DIAGNOSTICS.md#4-open-questions) | v0.1.0 convergence |
| CC-O3 | `MotionStream` shape (feeds `usd-motion-plugins` MC-O5) | [CONNECTOR §13](../design/CONNECTOR_CONTRACT.md#13-open-questions) | v0.1.0 |
| CS-O1 | Where the change-of-basis primitive lives | [COORDINATES §6](../design/COORDINATE_SYSTEMS.md#6-open-questions) | v0.1.0 convergence |
| CS-O3 | Conversion as code or profile data | [COORDINATES §6](../design/COORDINATE_SYSTEMS.md#6-open-questions) | CS-O1 |
| WS-O2 | Where the tracker solve lives | [WORKSPACE §7](../architecture/WORKSPACE.md#7-open-questions) | v0.1.0 convergence |
| CC-O4 | Per-joint tracking loss (feeds MC-O6) | [CONNECTOR §13](../design/CONNECTOR_CONTRACT.md#13-open-questions) | v0.1.0 convergence |
| CC-O7 | A C ABI | [CONNECTOR §13](../design/CONNECTOR_CONTRACT.md#13-open-questions) | Python bindings, v0.2.0 |
| CC-O8 | `MotionFrame` wire representation | [CONNECTOR §13](../design/CONNECTOR_CONTRACT.md#13-open-questions) | v0.2.0 |
| WS-O3 | Record tools vs `motion_connect record` | [WORKSPACE §7](../architecture/WORKSPACE.md#7-open-questions) | v0.2.0 planning |
| WS-O4 | `motionConnectorCore`'s closure and WASM | [WORKSPACE §7](../architecture/WORKSPACE.md#7-open-questions) | v0.3.0 |
| WS-O6 | Web module layout | [WORKSPACE §7](../architecture/WORKSPACE.md#7-open-questions) | v0.3.0 |
| CC-O1 | Joint data beyond `MotionPose` (feeds MC-O1, MC-O2) | [CONNECTOR §13](../design/CONNECTOR_CONTRACT.md#13-open-questions) | v0.3.0 |
| CC-O2 | Landmark sources: observation or solved pose | [CONNECTOR §13](../design/CONNECTOR_CONTRACT.md#13-open-questions) | v0.3.0 |
| SP-O3 | Landmark profiles | [PROFILES §6](../design/SOURCE_PROFILES.md#6-open-questions) | v0.3.0 |
| CS-O2 | VMC's two translation channels (`usd-motion-plugins` MC-O3) | [COORDINATES §6](../design/COORDINATE_SYSTEMS.md#6-open-questions) | a recorded session from two senders |
| CC-O5 | `ActorId` type | [CONNECTOR §13](../design/CONNECTOR_CONTRACT.md#13-open-questions) | the first multi-actor source |
| WS-O5 | Distribution (`usd-vrm-plugins` BND-2) | [WORKSPACE §7](../architecture/WORKSPACE.md#7-open-questions) | the first release |
