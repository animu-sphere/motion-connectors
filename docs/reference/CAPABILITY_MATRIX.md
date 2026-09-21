# Capability matrix

This is the only document that says what this repository implements. A
capability is listed as supported only when a test is behind it, and a source
counts as supported only when the test needs no hardware.

The source rows describe tested, source-specific decode, assembly and replay.
They do **not** imply conformance to `IMotionConnector`; that shared contract
is tracked separately in the Contract table.

Vocabulary: **supported** · **approximated** · **unsupported** · **—**
nothing implemented. The "Implemented elsewhere" column says where the
behaviour exists upstream when that context is relevant; it is not a second
status source.

The tables below are authoritative. Dated status prose does not override a
row's status.

## 1. Contract

| Capability | Status | Contract | Implemented elsewhere | Release |
| --- | --- | --- | --- | --- |
| `IMotionConnector`, state, capabilities | — | [CONNECTOR §2, §5](../design/CONNECTOR_CONTRACT.md) | `usd-vrm-plugins` per adapter (`LiveSource`), not as one interface | v0.1.0 |
| `MotionFrame`, `FrameTiming`, actors | — | [CONNECTOR §3, §6, §11](../design/CONNECTOR_CONTRACT.md#3-motionframe) | source-specific frame types only; no shared envelope | v0.1.0 |
| Bounded frame buffer, `Latest` / `Ordered` / `Lossless` | — | [CONNECTOR §8](../design/CONNECTOR_CONTRACT.md#8-buffering-push-and-pull) | only its datagram half, as `motionConnectorTransport`'s queue (imported 2026-09-19) | v0.1.0 |
| Source profiles | — | [SOURCE_PROFILES](../design/SOURCE_PROFILES.md) | nowhere as one format | v0.1.0 |
| The packet-capture file format, `p` peer lines included | supported — `motionConnectorTransport_packetCapture` | [CONNECTOR §12](../design/CONNECTOR_CONTRACT.md#12-raw-capture) | — (imported 2026-09-19) | v0.1.0 |
| The poll timeout mapping and wake-up predicate; the diagnostic vehicle | supported — `motionConnectorTransport_pollTimeout`, `_diagnostics` | [CONNECTOR §12](../design/CONNECTOR_CONTRACT.md#12-raw-capture) | — (imported 2026-09-19) | v0.1.0 |
| UDP receive and the opt-in datagram queue on a socket | supported — the VMC, mocopi and VRChat OSC receiver/loopback suites and their recorders' loopback names | [CONNECTOR §12](../design/CONNECTOR_CONTRACT.md#12-raw-capture) | — (arrived with the connectors) | v0.1.0 |
| Replay of a capture through a connector | supported — VMC and mocopi live-source corpus suites plus VRChat OSC frame/tracker corpus and loopback readings | [CONNECTOR §12](../design/CONNECTOR_CONTRACT.md#12-raw-capture) | — (arrived with the connectors) | v0.1.0 |
| OSC 1.0 wire format: packets, nested bundles in wire order, type tags, arguments, a refusal naming the byte | supported — `motionConnectorOsc_oscPacket` | [WORKSPACE §1.1](../architecture/WORKSPACE.md#11-native-libraries) | — (imported 2026-09-19) | v0.1.0 |
| `TrackerObservation`, assignment, tracker solve | supported — `motionConnectorTracking_trackerAssignment`, `_trackerSolve`, and end to end through `vrchat_osc_record_export` | [CONNECTOR §4](../design/CONNECTOR_CONTRACT.md#4-trackerobservation) | — (imported 2026-09-20) | v0.1.0 |

## 2. Sources

| Source | Profile | Body | Hands | Face | Root | Trackers | Status | Implemented elsewhere | Release |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| VMC Protocol | `vmc.v1` | ✅ | ✅ | ✅ | ✅ | | supported — `motionConnectorVmc_vmcMessage`, `_frameAssembler`, `_liveSource`, `_skeletonMap`, `_corpus`, `_udpReceiver`, `_packetCapture`, and `vmc_record`'s inspect and loopback | — (imported 2026-09-21) | v0.1.0 |
| mocopi native UDP | `mocopi.body.v1` | ✅ | | | ✅ | | supported — `motionConnectorMocopi_motionPacket`, `_skeletonMap`, `_frameAssembler`, `_liveSource`, `_corpus`, `_udpReceiver`, `_packetCapture`, and `mocopi_record`'s inspect, loopback, export and IPv6 names | — (imported 2026-09-21) | v0.1.0 |
| VRChat OSC Trackers | `vrchat-osc.trackers.v1` | | | | | ✅ | supported — `motionConnectorVrchatOsc_trackerMessage`, `_addressInventory`, `_trackingSpace`, `_frameAssembler`, `_udpReceiver`, `_packetCapture`, their corpus readings, and `vrchat_osc_record`'s four names | — (imported 2026-09-21) | v0.1.0 |
| WebSocket (`MotionFrame`) | — | — | — | — | — | — | — | nowhere | v0.2.0 |
| MediaPipe | `mediapipe.*.v1` | — | — | — | | | — | nowhere | v0.3.0 |
| WebXR | `webxr.*.v1` | | — | | | | — | nowhere | v0.4.0 |
| OpenXR | `openxr.*.v1` | | — | | | | — | nowhere | later |
| Generation adapter (ARDY) | — | | | | | | — | nowhere | later |

An empty cell means that source cannot carry that part.

## 3. Tools and bindings

| Capability | Status | Implemented elsewhere | Release |
| --- | --- | --- | --- |
| `motion_connect dump` | — | nowhere | v0.1.0 |
| `motion_connect list`, `inspect` | — | nowhere | v0.1.0 |
| `vmc_record`, `mocopi_record`, `vrchat_osc_record` raw capture tools | supported — inspect/loopback evidence is listed in the source rows | — (imported 2026-09-21) | v0.1.0 |
| `motion_connect record`, `bridge` | — | nowhere | v0.2.0 |
| Python bindings | — | nowhere | v0.2.0 |
| JS / TS package, WASM data ABI | — | nowhere | v0.3.0 |

## 4. Not here by design

| Capability | Status | Reason | Where |
| --- | --- | --- | --- |
| Retargeting onto a skeleton | unsupported by design | design policy §20, Rule 3 | `usd-motion-plugins` |
| Filtering, smoothing, resampling | unsupported by design | design policy §26 | `usd-motion-plugins` |
| Canonical recording, motion file formats | unsupported by design | design policy §25 | `usd-motion-plugins` |
| VRM, VRMA, PMX, VMD semantics | unsupported by design | design policy §21, §22, Rule 4 | the format repositories |
| USD stage authoring | unsupported by design | design policy §24, Rule 5 | `usd-motion-plugins`, `usd-avatar-runtime` |
