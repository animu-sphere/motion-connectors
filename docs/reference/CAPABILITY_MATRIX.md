# Capability matrix

This is the only document that says what this repository implements. A
capability is listed as supported only when a test is behind it, and a source
counts as supported only when the test needs no hardware.

The source rows describe tested, source-specific decode, assembly and replay.
They imply `IMotionConnector` conformance only where the row names a shared
adapter test; source-specific tests alone do not.

Vocabulary: **supported** · **approximated** · **unsupported** · **—**
nothing implemented. The "Implemented elsewhere" column says where the
behaviour exists upstream when that context is relevant; it is not a second
status source.

The tables below are authoritative. Dated status prose does not override a
row's status. `vX.Y.Z` means available in that release; `unreleased`
means only in the working tree, and `—` means no implementation or release.
Intended milestones belong to [the roadmap](../roadmap/current.md), not this
column. Native library availability does not imply a published npm package
or CLI artifact; artifact kinds are governed by
[WORKSPACE §4.1](../architecture/WORKSPACE.md#41-distribution).

## 1. Contract

| Capability | Status | Contract | Implemented elsewhere | Release availability |
| --- | --- | --- | --- | --- |
| `IMotionConnector`, state, capabilities | supported — `motionConnectorVmc_connector`, `motionConnectorMocopi_connector`, `motionConnectorVrchatOsc_connector`, `motionConnectorWebSocket_connector`, `motionConnectorWebSocket_loopback` | [CONNECTOR §2, §5](../design/CONNECTOR_CONTRACT.md) | `motionConnectorCore`; all three imported sources are adapted | v0.1.0 |
| `MotionFrame`, `FrameTiming`, actors | supported — `motionConnectorVmc_connector`, `motionConnectorMocopi_connector`, `motionConnectorVrchatOsc_connector` | [CONNECTOR §3, §6, §11](../design/CONNECTOR_CONTRACT.md#3-motionframe) | `motionConnectorCore`; all three imported sources are adapted | v0.1.0 |
| Bounded frame buffer, `Latest` / `Ordered` / `Lossless` | supported — `motionConnectorCore_frameBuffer`, `motionConnectorVmc_connector`, `motionConnectorMocopi_connector`, `motionConnectorVrchatOsc_connector` | [CONNECTOR §8](../design/CONNECTOR_CONTRACT.md#8-buffering-push-and-pull) | — | v0.1.0 |
| Connector to motion-stream handoff | supported — `motionConnectorVmc_connector` polls `MotionFrame`, pushes actor poses into `LiveCaptureSource`, samples the result and checks source time | [CONNECTOR §8](../design/CONNECTOR_CONTRACT.md#8-buffering-push-and-pull) | `motionRecording` supplies the intake | v0.1.0 |
| VMC acquisition boundary | supported — `motionConnectorVmc_frameSource` preserves missing/stale/duplicate observations, restart timestamps, fallback timing and datagram diagnostics; `_boundaries` rejects downstream library dependencies; `workspace_installed_consumer` resolves and runs installed VMC with downstream package discovery disabled | [DESIGN_POLICY §47](../design/DESIGN_POLICY.md#47-external-acquisition-boundary) | `VmcLiveSource` intake composition is private to consumer integration tests; downstream tests are labelled `consumer-integration` | unreleased |
| mocopi acquisition boundary | supported — `motionConnectorMocopi_frameSource` preserves missing/loss/duplicate and restart facts, source timestamps and datagram diagnostics, and compares all captures through `MocopiConnector`; `_boundaries` rejects downstream dependencies; `workspace_installed_consumer` resolves and runs installed mocopi with downstream discovery disabled | [DESIGN_POLICY §47](../design/DESIGN_POLICY.md#47-external-acquisition-boundary) | `MocopiLiveSource` intake composition is private to consumer integration tests; downstream tests are labelled `consumer-integration` | unreleased |
| Source profiles | supported — `workspace_source_profiles` and installed-consumer profile check | [SOURCE_PROFILES](../design/SOURCE_PROFILES.md) | connector-owned JSON profiles under `share/motion-connectors/profiles/`; optional OpenXR observation profile added unreleased | v0.1.0 |
| The packet-capture file format, `p` peer lines included | supported — `motionConnectorTransport_packetCapture` | [CONNECTOR §12](../design/CONNECTOR_CONTRACT.md#12-raw-capture) | — (imported 2026-09-19) | v0.1.0 |
| The poll timeout mapping and wake-up predicate; the diagnostic vehicle | supported — `motionConnectorTransport_pollTimeout`, `_diagnostics` | [CONNECTOR §12](../design/CONNECTOR_CONTRACT.md#12-raw-capture) | — (imported 2026-09-19) | v0.1.0 |
| UDP receive and the opt-in datagram queue on a socket | supported — the VMC, mocopi and VRChat OSC receiver/loopback suites and their recorders' loopback names | [CONNECTOR §12](../design/CONNECTOR_CONTRACT.md#12-raw-capture) | — (arrived with the connectors) | v0.1.0 |
| Replay of a capture through a connector | supported — `motionConnectorVmc_connectorCorpus`, `motionConnectorMocopi_connectorCorpus`, `motionConnectorVrchatOsc_connectorCorpus`, `motionConnectorWebSocket_messageCorpus` plus the source-layer corpus and loopback readings | [CONNECTOR §12](../design/CONNECTOR_CONTRACT.md#12-raw-capture) | — (arrived with the connectors) | v0.1.0 |
| OSC 1.0 wire format: packets, nested bundles in wire order, type tags, arguments, a refusal naming the byte | supported — `motionConnectorOsc_oscPacket` | [WORKSPACE §1.1](../architecture/WORKSPACE.md#11-native-libraries) | — (imported 2026-09-19) | v0.1.0 |
| `MotionFrame` wire format `openstrata.motion.frame/v1`: encode, decode, byte-identical re-encoding, every §6 refusal as a `WIRE_*` code with a path | supported — `motionConnectorWire_frameWire`, `_corpus`, `_messageGen` | [FRAME_WIRE_FORMAT §3–§6](../design/FRAME_WIRE_FORMAT.md#3-the-message) | — (new 2026-10-04) | unreleased |
| WebSocket transport: the RFC 6455 subset, the handshake with its required subprotocol and `Origin` allow list, every §5 refusal as a close status and a `WEBSOCKET_*` code naming its byte | supported — `motionConnectorWebSocket_rfc6455`, `_framingCorpus`, `_loopback` | [WEBSOCKET §4, §5](../design/WEBSOCKET_CONNECTOR.md#4-the-opening-handshake) | — (new 2026-10-04) | unreleased |
| Sending `MotionFrame`s: one encoding to every peer, a bounded queue per peer, listening or connecting | supported — `motionConnectorWebSocket_loopback` | [WEBSOCKET §3, §7.1](../design/WEBSOCKET_CONNECTOR.md#71-sending) | — (new 2026-10-04) | unreleased |
| `TrackerObservation`, assignment, tracker solve | supported — `motionConnectorTracking_trackerAssignment`, `_trackerSolve`, and end to end through `vrchat_osc_record_export` | [CONNECTOR §4](../design/CONNECTOR_CONTRACT.md#4-trackerobservation) | — (imported 2026-09-20) | v0.1.0 |
| Assignment observation applicability | supported — `motionConnectorTracking_trackerAssignment` checks owned identity lifetime, order, replacements, additions/removals, unplaced identities and malformed bindings; `_trackerSolve` refuses mismatched observations before pose authoring, accepts updated geometry with unchanged identities; `_boundaries` keeps identity validation independent of geometry; `workspace_installed_consumer` composes installed core observation identities with assignment | [CONNECTOR §4](../design/CONNECTOR_CONTRACT.md#4-trackerobservation) | semantic solve remains the compatibility path under [DESIGN_POLICY §47.5.1](../design/DESIGN_POLICY.md#4751-tracking-ownership-review) | unreleased |
| Workspace acquisition boundary gates | supported — `workspace_boundaries`, `_selftest` inject forbidden dependencies, stage/UsdSkel/OpenExec headers, protocol literals and motion semantics; CMake checks actual library closures in workspace/standalone/installed builds; `workspace_installed_consumer` compiles all installed headers with downstream discovery disabled | [WORKSPACE §2.4](../architecture/WORKSPACE.md#24-enforcement) | tools and consumer integration tests retain deliberate downstream dependencies | unreleased |

## 2. Sources

| Source | Profile | Body | Hands | Face | Root | Trackers | Status | Implemented elsewhere | Release availability |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| VMC Protocol | `vmc.v1` | ✅ | ✅ | ✅ | ✅ | | supported — `motionConnectorVmc_connector` plus `motionConnectorVmc_vmcMessage`, `_frameAssembler`, `_liveSource`, `_skeletonMap`, `_corpus`, `_udpReceiver`, `_packetCapture`, and `vmc_record`'s inspect and loopback | — (imported 2026-09-21; shared-contract adapter 2026-09-21) | v0.1.0 |
| mocopi native UDP | `mocopi.body.v1` | ✅ | | | ✅ | | supported — `motionConnectorMocopi_connector` plus `motionConnectorMocopi_motionPacket`, `_skeletonMap`, `_frameAssembler`, `_liveSource`, `_corpus`, `_udpReceiver`, `_packetCapture`, and `mocopi_record`'s inspect, loopback, export and IPv6 names | — (imported and shared-contract adapter 2026-09-21) | v0.1.0 |
| VRChat OSC Trackers | `vrchat-osc.trackers.v1` | | | | | ✅ | supported — `motionConnectorVrchatOsc_connector` plus `_trackerMessage`, `_addressInventory`, `_trackingSpace`, `_frameAssembler`, `_udpReceiver`, `_packetCapture`, their corpus readings, and `vrchat_osc_record`'s four names | — (imported and shared-contract adapter 2026-09-21) | v0.1.0 |
| WebSocket (`MotionFrame`) | the sender's, or the configured one | ✅ | ✅ | ✅ | ✅ | ✅ | supported — `motionConnectorWebSocket_connector`, `_messageCorpus` and `_loopback`, both directions in both roles over loopback | — (new 2026-10-04) | unreleased |
| MediaPipe Tasks | `mediapipe.body.v1`, `mediapipe.hands.v1`, `mediapipe.face.v1` | | | ✅ | | ✅ | supported acquisition — `web/motionConnectorMediaPipe` Node fixtures check body/hand metric observations, assumed basis, visibility, handedness, face channels, invalid/missing input, timestamps, single-actor bounds and queues; `motionConnectorWire_mediapipeInterop` decodes its browser output | caller owns inference/assets; body/hand semantic solve and labelled axis measurement remain downstream/operator work | unreleased |
| WebXR | `webxr.observations.v1` | | | | | ✅ | supported acquisition — `web/motionConnectorWebXR` Node API fixtures check viewer/controller grip/25 hand joints, physical basis conversion, timestamps, availability, session failures, stable input IDs and bounded queues; `motionConnectorWire_webxrInterop` decodes its output with the native codec | caller owns session/frame loop; no semantic hand pose or measured hardware compatibility claim | unreleased |
| OpenXR | `openxr.observations.v1` | | | | | ✅ | supported acquisition — `motionConnectorOpenXR_connector` uses deterministic SDK calls for head/controllers, EXT hands and FB upper-body observations, normalization, validity, confidence, time, failures and buffering; `workspace_installed_consumer` with SDK enabled consumes installed headers/configs with downstream packages disabled | caller owns session/frame loop; no semantic body/hand pose, hardware validation or headless session creation claim | unreleased |
| Generation adapter (ARDY) | — | | | | | | — | nowhere | — |

An empty cell means that source cannot carry that part.

## 3. Tools and bindings

| Capability | Status | Implemented elsewhere | Release availability |
| --- | --- | --- | --- |
| `motion_connect dump` | supported — `motion_connect_dump_vmc`, `_mocopi`, `_vrchat_osc`; installed: `workspace_installed_consumer` | shared `MotionFrame` live dump | v0.1.0 |
| `motion_connect list`, `inspect` | supported — `motion_connect_list`, `motion_connect_inspect_vmc`, `_mocopi`, `_vrchat_osc`; installed: `workspace_installed_consumer` | shared `MotionFrame` inventory and packet-capture replay | v0.1.0 |
| WebSocket source in `motion_connect list`, `dump`, `inspect` | supported — `motion_connect_list`, `motion_connect_dump_websocket`, `motion_connect_inspect_websocket`; installed: `workspace_installed_consumer` | sender profiles retained per frame; explicit live port and raw message replay | unreleased |
| `vmc_record`, `mocopi_record`, `vrchat_osc_record` raw capture tools | supported — inspect/loopback evidence is listed in the source rows | — (imported 2026-09-21) | v0.1.0 |
| Recorder raw/export separation | supported — `vmc_record_inspect`, `mocopi_record_export`, `vrchat_osc_record_export` check that export preserves acquisition reports and produces deterministic trace bytes; existing loopback suites retain raw capture evidence | tools replay emitted observations and call `motionRecording` only for semantic export | unreleased |
| `motion_connect bridge` | supported — `motion_connect_bridge_vmc`, `_mocopi`, `_vrchat_osc`, `_websocket`, `_listen`, `_live`, `_limits`, `_errors`; `_arguments` covers usage refusals | unchanged acquisition frames to WebSocket, live or paced capture replay; independent client checks origins and close 1001 | unreleased |
| `motion_connect record` | supported — `motion_connect_record_vmc`, `_mocopi`, `_vrchat_osc`, `_websocket`, `_silent_*`, `_limits`, `_errors`, `motion_connect_capture_lifecycle`; `motion_connect_arguments` covers usage; installed: `workspace_installed_consumer` | concrete connector live capture before decoding; bytes, peers, receive times and replay parity, with no semantic export | unreleased |
| C ABI | — | no acquisition C ABI; CC-O7 owns the decision | — |
| Python bindings | — | no binding over the C ABI | — |
| `record_stream` example | — | no standalone downstream recording example under `examples/record_stream/` | — |
| Shared JS/TS consumer API (`bindings/js/`) | — | browser acquisition providers are separate source modules, not this binding | — |
| WASM-friendly data ABI | — | separate from native C ABI, JS objects and serialized wire format | — |

## 4. Not here by design

| Capability | Status | Reason | Where |
| --- | --- | --- | --- |
| Retargeting onto a skeleton | unsupported by design | design policy §20, Rule 3 | `usd-motion-plugins` |
| Filtering, smoothing, resampling | unsupported by design | design policy §26 | `usd-motion-plugins` |
| Canonical recording, motion file formats | unsupported by design | design policy §25 | `usd-motion-plugins` |
| VRM, VRMA, PMX, VMD semantics | unsupported by design | design policy §21, §22, Rule 4 | the format repositories |
| USD stage authoring | unsupported by design | design policy §24, Rule 5 | `usd-motion-plugins`, `usd-avatar-runtime` |
