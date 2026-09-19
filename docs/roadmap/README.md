# Roadmap

The roadmap holds only **incomplete** work. When something lands, its detail
leaves this directory: shipped scope goes to the changelog and a release
record, and the implemented state to [architecture/](../architecture/) and
[reference/](../reference/). Rationale lives in [design/](../design/).

Legend: ✅ done · 🚧 in progress · ⬜ not started · ⛔ blocked

| Document | Contents |
| --- | --- |
| [current.md](current.md) | The current milestone (the scaffold and the connector contract), then v0.1.0, and what each waits for. |

## Three sequences

| Sequence | What it tracks | Source of truth |
| --- | --- | --- |
| **Releases v0.1.0–v0.4.0** | what this repository delivers | [DESIGN_POLICY.md §31–§34](../design/DESIGN_POLICY.md#31-suggested-first-release-scope), as mapped below |
| **Connector Phase 1–8** | the order sources are added | [DESIGN_POLICY.md §30](../design/DESIGN_POLICY.md#30-recommended-initial-implementation-order) |
| **Migration Phase E** | the live inputs leaving `usd-vrm-plugins` (its MIG-4) | `usd-motion-plugins` design policy §37; `usd-vrm-plugins` WORKSPACE §9 |

Phases are always written with their qualifier
([DESIGN_POLICY.md §46.6](../design/DESIGN_POLICY.md#466-phases-are-always-qualified)).

## Status at a glance

**This table is the single source of truth for which release carries what.**

| Release | Scope | Imports from `usd-vrm-plugins` | Connector Phase | Waits for | Status |
| --- | --- | --- | --- | --- | --- |
| v0.1.0: contract and VMC | `motionConnectorCore` (`IMotionConnector`, `MotionFrame`, state, capabilities, timing, the bounded buffer); source profiles; `motionConnectorTransport`, `motionConnectorOsc`, `motionConnectorVmc`; `motion-connect dump`, `list`, `inspect`; generated corpus; hardware-free CI | `liveTransport`, `osc`, `vrmAdapterVmc`, `vmcRecord` | 1, 2 | `usd-motion-plugins` v0.1.0 (`motion-core`, `motion-recording` installed) | ⛔ |
| v0.2.0: transport and the remaining imports | `motionConnectorWebSocket`; `motion-connect record`, `bridge`; `examples/record_stream`; Python bindings; `motionConnectorMocopi`, `motionConnectorVrchatOsc`, `motionConnectorTracking` | `vrmAdapterMocopi`, `vrmAdapterVrchatOsc`, `motionTracking`, their record tools | 3, and 6 as an import | v0.1.0 | ⬜ |
| v0.3.0: browser tracking | `motionConnectorMediaPipe` (body, hands, face); the JS / TS package; the WASM-friendly data ABI | none | 4 | WS-O4, CC-O2 | ⬜ |
| v0.4.0: XR and integration | `motionConnectorWebXR`; `examples/usd_avatar_live` and integration examples with `usd-motion-plugins` | none | 5 | `usd-motion-plugins` v0.2.0 (retargeting) | ⬜ |
| later | `motionConnectorOpenXR`; the generation adapter (ARDY); advanced devices; the C ABI | none | 7, 8 | the generator interface in `usd-motion-plugins`; CC-O7 | ⬜ |

**Where this departs from the design policy's §31–§34, and why.**

- **mocopi moves from v0.4.x to v0.2.0, and VRChat OSC Trackers is added
  there.** Both are imports of measured code, not new designs. `usd-vrm-plugins`
  freezes each identity until it moves, and cannot finish its migration (MIG-5)
  while they wait here. §30's concern, that mocopi should not define the core
  API, is met because v0.1.0 fixes the contract before either arrives
  ([DESIGN_POLICY.md §46.7](../design/DESIGN_POLICY.md#467-the-release-order-follows-the-imports)).
- **`motionConnectorOsc` is the OSC wire format, not a connector.** §31 lists
  it beside VMC. The imported `osc` library is a wire format shared by VMC and
  VRChat OSC, and no generic "any OSC" connector is planned: a generic OSC
  router is excluded in `usd-vrm-plugins`' OSC track §12, and nothing here
  reopens it.
- **v0.1.0 needs OpenUSD's foundation types**, through `motion-core`
  ([DESIGN_POLICY.md §46.1](../design/DESIGN_POLICY.md#461-the-pose-is-usd-motion-plugins-motionpose)).
  §31's "without requiring USD integration" still holds, because nothing opens
  a stage. But v0.1.0 cannot ship before `usd-motion-plugins` v0.1.0, and that
  release waits for `usd-vrm-plugins` v0.9.0.

## Open decisions

Every open question the design documents carry, in the order they block work.
The owning document holds the question; this list only schedules it.

| Id | Question | Owner | Blocks |
| --- | --- | --- | --- |
| WS-O1 | Names, directories, target and C++ namespaces | [WORKSPACE §7](../architecture/WORKSPACE.md#7-open-questions) | the scaffold |
| WS-O7 | Import order against the one-copy rule | [WORKSPACE §7](../architecture/WORKSPACE.md#7-open-questions) | the first import |
| CC-O6 | `Poll` semantics and skip reporting | [CONNECTOR §13](../design/CONNECTOR_CONTRACT.md#13-open-questions) | v0.1.0 |
| SP-O1 | Profile identifier scheme | [PROFILES §6](../design/SOURCE_PROFILES.md#6-open-questions) | v0.1.0 |
| SP-O2 | Profiles as data or code | [PROFILES §6](../design/SOURCE_PROFILES.md#6-open-questions) | v0.1.0 |
| DIAG-O1 | Diagnostic code style, and renaming `VRM_VMC_*` and the rest | [DIAGNOSTICS §4](../reference/DIAGNOSTICS.md#4-open-questions) | the first import |
| CC-O3 | `MotionStream` shape (feeds `usd-motion-plugins` MC-O5) | [CONNECTOR §13](../design/CONNECTOR_CONTRACT.md#13-open-questions) | v0.1.0 |
| CS-O1 | Where the change-of-basis primitive lives | [COORDINATES §6](../design/COORDINATE_SYSTEMS.md#6-open-questions) | the VRChat OSC import |
| CS-O3 | Conversion as code or profile data | [COORDINATES §6](../design/COORDINATE_SYSTEMS.md#6-open-questions) | CS-O1 |
| WS-O2 | Where the tracker solve lives | [WORKSPACE §7](../architecture/WORKSPACE.md#7-open-questions) | the VRChat OSC import |
| CC-O4 | Per-joint tracking loss (feeds MC-O6) | [CONNECTOR §13](../design/CONNECTOR_CONTRACT.md#13-open-questions) | the mocopi import |
| CC-O7 | A C ABI | [CONNECTOR §13](../design/CONNECTOR_CONTRACT.md#13-open-questions) | Python bindings, v0.2.0 |
| CC-O8 | `MotionFrame` wire representation | [CONNECTOR §13](../design/CONNECTOR_CONTRACT.md#13-open-questions) | v0.2.0 |
| WS-O3 | Record tools vs `motion-connect record` | [WORKSPACE §7](../architecture/WORKSPACE.md#7-open-questions) | after the last import |
| WS-O4 | `motionConnectorCore`'s closure and WASM | [WORKSPACE §7](../architecture/WORKSPACE.md#7-open-questions) | v0.3.0 |
| WS-O6 | Web module layout | [WORKSPACE §7](../architecture/WORKSPACE.md#7-open-questions) | v0.3.0 |
| CC-O1 | Joint data beyond `MotionPose` (feeds MC-O1, MC-O2) | [CONNECTOR §13](../design/CONNECTOR_CONTRACT.md#13-open-questions) | v0.3.0 |
| CC-O2 | Landmark sources: observation or solved pose | [CONNECTOR §13](../design/CONNECTOR_CONTRACT.md#13-open-questions) | v0.3.0 |
| SP-O3 | Landmark profiles | [PROFILES §6](../design/SOURCE_PROFILES.md#6-open-questions) | v0.3.0 |
| CS-O2 | VMC's two translation channels (`usd-motion-plugins` MC-O3) | [COORDINATES §6](../design/COORDINATE_SYSTEMS.md#6-open-questions) | a recorded session from two senders |
| CC-O5 | `ActorId` type | [CONNECTOR §13](../design/CONNECTOR_CONTRACT.md#13-open-questions) | the first multi-actor source |
| WS-O5 | Distribution (`usd-vrm-plugins` BND-2) | [WORKSPACE §7](../architecture/WORKSPACE.md#7-open-questions) | the first release |
